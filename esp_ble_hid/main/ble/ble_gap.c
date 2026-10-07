#include "ble_gap.h"
#include "esp_bt.h"
#include "esp_hid_common.h"
#include "esp_log.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "hid_event.h"

static const char *TAG = "ble_gap";

uint16_t current_conn_handle = 0; // 初始化为0（无连接）
#define GATT_SVR_SVC_HID_UUID 0x1812

static SemaphoreHandle_t bt_hidh_cb_semaphore = NULL;
static SemaphoreHandle_t ble_hidh_cb_semaphore = NULL;
static struct ble_hs_adv_fields fields; // 广播字段

static int nimble_hid_gap_event(struct ble_gap_event *event, void *arg) {
  struct ble_gap_conn_desc desc;
  int rc;

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    /* A new connection was established or a connection attempt failed. */
    ESP_LOGI(TAG, "connection %s; status=%d",
             event->connect.status == 0 ? "established" : "failed",
             event->connect.status);
    if (event->connect.status == 0) {
      current_conn_handle = event->connect.conn_handle; // 更新当前连接句柄
    }
    return 0;
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    current_conn_handle = 0;
    ESP_LOGI(TAG, "disconnect; reason=%d (0x%04x)", event->disconnect.reason, event->disconnect.reason);
    return 0;
  case BLE_GAP_EVENT_CONN_UPDATE:
    /* The central has updated the connection parameters. */
    ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);
    return 0;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG,
             "subscribe event; conn_handle=%d attr_handle=%d "
             "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.reason, event->subscribe.prev_notify,
             event->subscribe.cur_notify, event->subscribe.prev_indicate,
             event->subscribe.cur_indicate);
    return 0;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
             event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
    return 0;

  case BLE_GAP_EVENT_ENC_CHANGE:
    /* Encryption has been enabled or disabled for this connection. */
    MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                event->enc_change.status);
    rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
    assert(rc == 0);
    // ble_hid_task_start_up();
    return 0;

  case BLE_GAP_EVENT_NOTIFY_TX:
    MODLOG_DFLT(INFO,
                "notify_tx event; conn_handle=%d attr_handle=%d "
                "status=%d is_indication=%d",
                event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                event->notify_tx.status, event->notify_tx.indication);
    return 0;

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    /* We already have a bond with the peer, but it is attempting to
     * establish a new secure link.  This app sacrifices security for
     * convenience: just throw away the old bond and accept the new link.
     */

    /* Delete the old bond. */
    rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    assert(rc == 0);
    ble_store_util_delete_peer(&desc.peer_id_addr);

    /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
     * continue with the pairing operation.
     */
    return BLE_GAP_REPEAT_PAIRING_RETRY;

  case BLE_GAP_EVENT_PASSKEY_ACTION:
    // PIN code functionality has been removed
    ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT received but PIN code functionality is disabled");
    return 0;
  }
  return 0;
}

static esp_err_t init_low_level() {
  esp_err_t ret;
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
// #if CONFIG_IDF_TARGET_ESP32
//   bt_cfg.mode = mode;
// #endif
  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret) {
    ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
    return ret;
  }
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret) {
    ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
    return ret;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret) {
    ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
    return ret;
  }

  ret = esp_nimble_init();
  if (ret) {
    ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
    return ret;
  }

  return ret;
}

esp_err_t hid_ble_gap_init(void) {
  esp_err_t ret;

  if (bt_hidh_cb_semaphore != NULL) {
    ESP_LOGE(TAG, "Already initialised");
    return ESP_FAIL;
  }

  bt_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (bt_hidh_cb_semaphore == NULL) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    return ESP_FAIL;
  }

  ble_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (ble_hidh_cb_semaphore == NULL) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    vSemaphoreDelete(bt_hidh_cb_semaphore);
    bt_hidh_cb_semaphore = NULL;
    return ESP_FAIL;
  }

  ret = init_low_level();
  if (ret != ESP_OK) {
    vSemaphoreDelete(bt_hidh_cb_semaphore);
    bt_hidh_cb_semaphore = NULL;
    vSemaphoreDelete(ble_hidh_cb_semaphore);
    ble_hidh_cb_semaphore = NULL;
    return ret;
  }

  return ESP_OK;
}



esp_err_t esp_hid_gap_init(uint8_t mode) {
  esp_err_t ret;
  if (!mode || mode > ESP_BT_MODE_BTDM) {
    ESP_LOGE(TAG, "Invalid mode given!");
    return ESP_FAIL;
  }

  if (bt_hidh_cb_semaphore != NULL) {
    ESP_LOGE(TAG, "Already initialised");
    return ESP_FAIL;
  }

  bt_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (bt_hidh_cb_semaphore == NULL) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    return ESP_FAIL;
  }

  ble_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (ble_hidh_cb_semaphore == NULL) {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    vSemaphoreDelete(bt_hidh_cb_semaphore);
    bt_hidh_cb_semaphore = NULL;
    return ESP_FAIL;
  }

  ret = init_low_level();
  if (ret != ESP_OK) {
    vSemaphoreDelete(bt_hidh_cb_semaphore);
    bt_hidh_cb_semaphore = NULL;
    vSemaphoreDelete(ble_hidh_cb_semaphore);
    ble_hidh_cb_semaphore = NULL;
    return ret;
  }

  return ESP_OK;
}

esp_err_t hid_ble_gap_adv_init(uint16_t appearance,
                                   const char *device_name) {
  ble_uuid16_t *uuid16, *uuid16_1;
  /**
   *  Set the advertisement data included in our advertisements:
   *     o Flags (indicates advertisement type and other general info).
   *     o Advertising tx power.
   *     o Device name.
   *     o 16-bit service UUIDs (HID).
   */

  memset(&fields, 0, sizeof fields);

  /* Advertise two flags:
   *     o Discoverability in forthcoming advertisement (general)
   *     o BLE-only (BR/EDR unsupported).
   */
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  fields.appearance = ESP_HID_APPEARANCE_GENERIC;
  fields.appearance_is_present = 1;

  /* Indicate that the TX power level field should be included; have the
   * stack fill this value automatically.  This is done by assigning the
   * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
   */
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  uuid16 = (ble_uuid16_t *)malloc(sizeof(ble_uuid16_t));
  uuid16_1 = (ble_uuid16_t[]){BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID)};
  memcpy(uuid16, uuid16_1, sizeof(ble_uuid16_t));
  fields.uuids16 = uuid16;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1; 
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0;
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist |=
      BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

  return ESP_OK;
}

esp_err_t hid_ble_gap_adv_start(void) {
  int rc;
  struct ble_gap_adv_params adv_params;
  int32_t adv_duration_ms = 180000;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
    return rc;
  }
  /* Begin advertising. */
  memset(&adv_params, 0, sizeof adv_params);
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min =
      BLE_GAP_ADV_ITVL_MS(30); /* Recommended interval 30ms to 50ms */
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, adv_duration_ms,
                         &adv_params, nimble_hid_gap_event, NULL);
  if (rc != 0) {
    MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
    return rc;
  }
  return rc;
}

// void ble_hid_device_host_task(void *param) {
//   ESP_LOGI(TAG, "BLE Host Task Started");
//   nimble_port_run();
//   nimble_port_freertos_deinit();
// }
#include "ble_gap.h"
#include "esp_err.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include "hid_usage.h"  
#include "hid_param.h"  

static const char *TAG = "BLE_GATT_SVCS";

static const ble_uuid128_t hid_control_svc_uuid = 
    BLE_UUID128_INIT(0x46, 0xe0, 0x7d, 0x77, 0xd2, 0xb4, 0xb7, 0x87, 0x81, 0x45, 0x3b, 0x64, 0x4a, 0x2a, 0xed, 0x24);

static const ble_uuid128_t hid_command_chr_uuid = 
    BLE_UUID128_INIT(0x65, 0x8d, 0x40, 0x15, 0xf4, 0xf6, 0x62, 0xb2, 0xb2, 0x45, 0x89, 0xf2, 0x34, 0xe5, 0x4a, 0xc3);


static int hid_command_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t *data = ctxt->om->om_data;
        uint16_t data_len = ctxt->om->om_len;

        char cmd_buf[64] = {0};
        if (data_len >= sizeof(cmd_buf)) {
            ESP_LOGE(TAG, "Command too long (max 63 bytes)");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        memcpy(cmd_buf, data, data_len);
        cmd_buf[data_len] = '\0';
        ESP_LOGI(TAG, "Received GATT command: '%s'", cmd_buf);

        esp_err_t ret = parse_and_execute_command(cmd_buf);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Command execution failed: %s", esp_err_to_name(ret));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        return 0;  // 成功
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// GATT 服务定义
static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &hid_control_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &hid_command_chr_uuid.u,
                .access_cb = hid_command_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0},  // 特性结束
        }
    },
    {0},  // 服务结束
};

esp_err_t gatt_svc_init(void) {
    ble_svc_gatt_init();  // 初始化 GATT 服务
    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Custom GATT service initialized successfully");
    return ESP_OK;
}
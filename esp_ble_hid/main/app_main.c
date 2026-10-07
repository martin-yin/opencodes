// app_main.c
#include "ble_gap.h"
#include "driver/uart.h"
#include "hid_event.h"
#include "hid_param.h"
#include "hid_report_map.h"
#include "esp_hidd.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "gatt_services.h"

#define TAG "app_main"

static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "NIMBLE HID",
    .manufacturer_name = "Martin-Yin NIMBLE HID",
    .serial_number = "1234567890",
    .report_maps = hid_raw_report_map,
    .report_maps_len = 1
};

void ble_hid_device_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

void app_main(void) {
  esp_err_t ret;
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ret = hid_ble_gap_init();
  ESP_ERROR_CHECK(ret);

  ret = hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC,
                                 hid_config.device_name);
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE,
                                    hid_event_callback,
                                    &s_ble_hid_param.hid_dev));

  gatt_svc_init(); 
  ble_store_config_init();
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ret = esp_nimble_enable(ble_hid_device_host_task);


  
  if (ret) {
    ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
  }

// 初始化 UART0
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 安装 UART 驱动，优化缓冲区大小
    esp_err_t ret_uart = uart_driver_install(UART_NUM_0, 256, 256, 10, NULL, 0);  // RX 256 字节，TX 256 字节，队列 10
    if (ret_uart != ESP_OK) {
        ESP_LOGE(TAG, "UART0 install failed: %s", esp_err_to_name(ret_uart));
        return;
    }

    ret_uart = uart_param_config(UART_NUM_0, &uart_config);
    if (ret_uart != ESP_OK) {
        ESP_LOGE(TAG, "UART0 config failed: %s", esp_err_to_name(ret_uart));
        uart_driver_delete(UART_NUM_0);
        return;
    }

    ESP_LOGI(TAG, "UART0 initialized for Type-C USB");
    // 启动 UART 任务
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 5, NULL);
}
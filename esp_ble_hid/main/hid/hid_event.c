// hid/hid_event.c
#include "hid_event.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "ble_gap.h"
#include "hal/uart_types.h"
#include "hid_usage.h"
#include "host/ble_gap.h"
#include <stdio.h>
#include "driver/uart.h"  // 添加 UART 驱动
#include <string.h>       // 为 strncmp 等
#include <stdio.h>        // 为 sscanf

#define UART_BUF_SIZE 128
// 如果连接的是手机这里可以修改成手机的屏幕分辨率
#define SCREEN_WIDTH 1400  
#define SCREEN_HEIGHT 3400 
static const char *TAG = "hid_event";

void hid_event_callback(void *handler_args, esp_event_base_t base,
                       int32_t id, void *event_data) {
  esp_hidd_event_t event = (esp_hidd_event_t)id;
  esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
  static const char *TAG = "HID_DEV_BLE";

  switch (event) {
  case ESP_HIDD_START_EVENT: {
    ESP_LOGI(TAG, "START");
    hid_ble_gap_adv_start();
    break;
  }
  case ESP_HIDD_CONNECT_EVENT: {
    ESP_LOGI(TAG, "CONNECT");
    break;
  }
  case ESP_HIDD_PROTOCOL_MODE_EVENT: {
    ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index,
             param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
    break;
  }
  case ESP_HIDD_CONTROL_EVENT: {
    ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index,
             param->control.control ? "EXIT_" : "");
    break;
  }
  case ESP_HIDD_OUTPUT_EVENT: {
    ESP_LOGI(TAG,
             "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index,
             esp_hid_usage_str(param->output.usage), param->output.report_id,
             param->output.length);
    ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
    break;
  }
  case ESP_HIDD_FEATURE_EVENT: {
    ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:",
             param->feature.map_index, esp_hid_usage_str(param->feature.usage),
             param->feature.report_id, param->feature.length);
    ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
    break;
  }
  case ESP_HIDD_DISCONNECT_EVENT: {
    ESP_LOGI(TAG, "DISCONNECT: %s",
             esp_hid_disconnect_reason_str(
                 esp_hidd_dev_transport_get(param->disconnect.dev),
                 param->disconnect.reason));
    hid_ble_gap_adv_start();
    break;
  }
  case ESP_HIDD_STOP_EVENT: {
    ESP_LOGI(TAG, "STOP");
    break;
  }
  default:
    ESP_LOGI(TAG, "default event: %d", event);
    break;
  }
  return;
}


void uart_task(void *pvParameters) {
    uint8_t buf[UART_BUF_SIZE];
    int len;
    ESP_LOGI(TAG, "UART task started");
    uart_flush_input(UART_NUM_0);

    while (1) {
        memset(buf, 0, UART_BUF_SIZE);
        len = uart_read_bytes(UART_NUM_0, buf, UART_BUF_SIZE - 1, pdMS_TO_TICKS(10));
        if (len > 0) {
            // 移除换行符
            for (int i = 0; i < len; i++) {
                if (buf[i] == '\r' || buf[i] == '\n') {
                    buf[i] = '\0';
                    len = i;
                    break;
                }
            }
            char *cmd = (char *)buf;
            ESP_LOGI(TAG, "Received UART command: '%s' (len=%d)", cmd, len);
            esp_err_t ret = parse_and_execute_command(cmd);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Command execution failed: %s", esp_err_to_name(ret));
            }
        } else if (len < 0) {
            ESP_LOGE(TAG, "UART read error: %s", esp_err_to_name(len));
        }
    }
}
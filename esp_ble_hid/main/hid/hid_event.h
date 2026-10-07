// hid/hid_event.h
#ifndef HID_EVENT_H
#define HID_EVENT_H

#include "esp_event_base.h"
/**
 * @brief HID设备事件回调（处理连接、断开、报告等业务事件）
 */
void hid_event_callback(void *handler_args, esp_event_base_t base,
                       int32_t id, void *event_data);

// void ble_hid_task_start_up(void);

void uart_task(void *pvParameters);
#endif // HID_EVENT_H
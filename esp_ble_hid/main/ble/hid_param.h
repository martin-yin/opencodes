#ifndef HID_PARAM_H
#define HID_PARAM_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_hidd.h" // 包含 esp_hidd_dev_t 类型定义

/**
 * @brief HID设备本地参数结构体（存储任务句柄、设备指针等全局状态）
 */
typedef struct {
    TaskHandle_t task_hdl;         // BLE主机任务句柄
    esp_hidd_dev_t *hid_dev;       // HID设备句柄（由esp_hidd_dev_init返回）
    uint8_t protocol_mode;         // 协议模式（BOOT/REPORT）
    uint8_t *buffer;               // 数据缓冲区（用于HID报告传输）
} local_param_t;

/**
 * @brief 全局HID设备参数实例（多文件共享）
 * 外部文件需通过 #include "hid/hid_param.h" 访问
 */
extern local_param_t s_ble_hid_param;

#endif // HID_PARAM_H
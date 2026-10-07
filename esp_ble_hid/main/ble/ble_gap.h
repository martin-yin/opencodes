#ifndef BLE_GAP_H
#define BLE_GAP_H

#include "esp_err.h"
extern uint16_t current_conn_handle;

/**
 * @brief 初始化BLE GAP层（信号量、基础配置）
 * @return ESP_OK 成功；其他值 失败
 */
esp_err_t hid_ble_gap_init(void);

/**
 * @brief 配置BLE广播参数（名称、外观、UUID等）
 * @param appearance 设备外观（如ESP_HID_APPEARANCE_GENERIC）
 * @param device_name 广播名称
 * @return ESP_OK 成功；其他值 失败
 */
esp_err_t hid_ble_gap_adv_init(uint16_t appearance, const char *device_name);

/**
 * @brief 启动BLE广播
 * @return ESP_OK 成功；其他值 失败
 */
esp_err_t hid_ble_gap_adv_start(void);

// void ble_hid_device_host_task(void *param);
void ble_host_task(void *param);
#endif // BLE_GAP_H
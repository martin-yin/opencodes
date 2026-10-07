#ifndef HID_REPORT_MAP_H
#define HID_REPORT_MAP_H

#include "esp_hid_common.h"

/**
 * @brief HID报告描述符（包含触摸屏、键盘、Consumer Control等功能）
 * 报告ID说明：
 * - 1: 触摸屏报告（绝对定位）
 * - 3: 键盘报告（修饰键+按键数组）
 * - 4: Consumer Control报告（Android系统按键）
 */
extern const unsigned char hid_report_map[];

/**
 * @brief ESP-IDF HID组件所需的原始报告描述符结构体（包含报告数据和长度）
 */
extern esp_hid_raw_report_map_t hid_raw_report_map[];

#endif // HID_REPORT_MAP_H
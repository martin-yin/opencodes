#include "esp_hid_common.h"

const unsigned char hid_report_map[] = {
    0x05, 0x0D,                    // USAGE_PAGE (Digitizers)
    0x09, 0x04,                    // USAGE (Touch Screen)
    0xA1, 0x01,                    // COLLECTION (Application)
    0x85, 0x01,                    //   REPORT_ID (1)  // 添加Report ID以标准

    0x09, 0x22,                    //   USAGE (Finger)
    0xA1, 0x00,                    //   COLLECTION (Physical)  // 改为Physical，更标准

    0x09, 0x42,                    //     USAGE (Tip Switch)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

    0x09, 0x32,                    //     USAGE (In Range)  // 添加In Range，通常与Tip同步
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

    0x95, 0x06,                    //     REPORT_COUNT (6)  // 调整padding为6 bits
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)  // 修正为标准Const Array Abs

    0x09, 0x51,                    //     USAGE (Contact Identifier)  // 添加Contact ID
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0xFF,              //     LOGICAL_MAXIMUM (65535) 
    0x75, 0x10,                    //     REPORT_SIZE (16)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x09, 0x30,                    //     USAGE (X)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x31,                    //     USAGE (Y)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

    0xC0,                          //   END_COLLECTION

    0x05, 0x0D,                    // USAGE_PAGE (Digitizers)
    0x09, 0x54,                    //   USAGE (Contact Count)  // 添加当前触点数
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)  // 单点
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)

    0x09, 0x55,                    //   USAGE (Contact Count Maximum)  // 添加特征报告
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0xB1, 0x02,                    //   FEATURE (Data,Var,Abs)  // 常量特征

    0xC0 ,                          // END_COLLECTION
    // 键盘报告 (Report ID 3)
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xA1, 0x01, // COLLECTION (Application)
    0x85, 0x03, //   REPORT_ID (3)

    // 修饰键 (Ctrl, Shift, Alt, GUI等)
    0x05, 0x07, //   USAGE_PAGE (Keyboard/Keypad)
    0x19, 0xE0, //   USAGE_MINIMUM (Left Control)
    0x29, 0xE7, //   USAGE_MAXIMUM (Right GUI)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x95, 0x08, //   REPORT_COUNT (8)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // 保留字节
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x81, 0x01, //   INPUT (Cnst,Ary,Abs)  // 修正为标准Const Array Abs

    // 按键数组 (最多6个同时按下的按键)
    0x95, 0x06, //   REPORT_COUNT (6)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x65, //   LOGICAL_MAXIMUM (101)
    0x05, 0x07, //   USAGE_PAGE (Keyboard/Keypad)
    0x19, 0x00, //   USAGE_MINIMUM (0)
    0x29, 0x65, //   USAGE_MAXIMUM (101)
    0x81, 0x00, //   INPUT (Data,Ary,Abs)

    0xC0, // END_COLLECTION

    // Consumer Control报告 (Report ID 4) - 用于Android系统按键
    0x05, 0x0C,                    // USAGE_PAGE (Consumer)
    0x09, 0x01,                    // USAGE (Consumer Control)
    0xA1, 0x01,                    // COLLECTION (Application)
    0x85, 0x04,                    //   REPORT_ID (4)
    
    // Android Back键
    0x0A, 0x24, 0x02,              //   USAGE (AC Back - 0x0224)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    
    // Android Home键
    0x0A, 0x23, 0x02,              //   USAGE (AC Home - 0x0223)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    
    // Android Menu键 (作为通知面板或上下文菜单的替代)
    0x0A, 0x40, 0x00,              //   USAGE (Menu - 0x0040)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    
    // Android App Switch键 (最近应用/Recents)
    0x0A, 0xA2, 0x01,              //   USAGE (AL Select Task/Application - 0x01A2)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    
    // 填充位 (4位)
    0x75, 0x04,                    //   REPORT_SIZE (4)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x81, 0x01,                    //   INPUT (Cnst,Ary,Abs)  // 修正为标准Const Array Abs
    
    0xC0                           // END_COLLECTION
};

esp_hid_raw_report_map_t hid_raw_report_map[] = {
    {.data = hid_report_map, .len = sizeof(hid_report_map)},
};
// hid/hid_usage.h
#ifndef HID_USAGE_H
#define HID_USAGE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 键盘修饰键（位掩码）
enum hid_modifier {
    KEY_MOD_LCTRL  = 0x01,  // 左Ctrl
    KEY_MOD_LSHIFT = 0x02,  // 左Shift
    KEY_MOD_LALT   = 0x04,  // 左Alt
    KEY_MOD_LGUI   = 0x08,  // 左GUI（通常是Windows键）
    KEY_MOD_RCTRL  = 0x10,  // 右Ctrl
    KEY_MOD_RSHIFT = 0x20,  // 右Shift
    KEY_MOD_RALT   = 0x40,  // 右Alt
    KEY_MOD_RGUI   = 0x80   // 右GUI
};

// HID 键盘扫描码（基于 USB HID Usage Tables）
enum hid_keycode {
    KEY_NONE       = 0x00,
    KEY_A          = 0x04,
    KEY_B          = 0x05,
    KEY_C          = 0x06,
    KEY_D          = 0x07,
    KEY_E          = 0x08,
    KEY_F          = 0x09,
    KEY_G          = 0x0A,
    KEY_H          = 0x0B,
    KEY_I          = 0x0C,
    KEY_J          = 0x0D,
    KEY_K          = 0x0E,
    KEY_L          = 0x0F,
    KEY_M          = 0x10,
    KEY_N          = 0x11,
    KEY_O          = 0x12,
    KEY_P          = 0x13,
    KEY_Q          = 0x14,
    KEY_R          = 0x15,
    KEY_S          = 0x16,
    KEY_T          = 0x17,
    KEY_U          = 0x18,
    KEY_V          = 0x19,
    KEY_W          = 0x1A,
    KEY_X          = 0x1B,
    KEY_Y          = 0x1C,
    KEY_Z          = 0x1D,
    KEY_1          = 0x1E,
    KEY_2          = 0x1F,
    KEY_3          = 0x20,
    KEY_4          = 0x21,
    KEY_5          = 0x22,
    KEY_6          = 0x23,
    KEY_7          = 0x24,
    KEY_8          = 0x25,
    KEY_9          = 0x26,
    KEY_0          = 0x27,
    KEY_ENTER      = 0x28,    // 回车键
    KEY_ESC        = 0x29,    // ESC键
    KEY_BACKSPACE  = 0x2A,    // 退格键
    KEY_TAB        = 0x2B,    // Tab键
    KEY_SPACE      = 0x2C,    // 空格键
    KEY_MINUS      = 0x2D,    // 减号键
    KEY_EQUAL      = 0x2E,    // 等号键
    KEY_LEFTBRACE  = 0x2F,    // 左方括号键
    KEY_RIGHTBRACE = 0x30,    // 右方括号键
    KEY_BACKSLASH  = 0x31,    // 反斜杠键
    KEY_SEMICOLON  = 0x33,    // 分号键
    KEY_APOSTROPHE = 0x34,    // 单引号键
    KEY_GRAVE      = 0x35,    // 反引号键
    KEY_COMMA      = 0x36,    // 逗号键
    KEY_DOT        = 0x37,    // 句号键
    KEY_SLASH      = 0x38,    // 斜杠键
    KEY_F1         = 0x3A,
    KEY_F2         = 0x3B,
    KEY_F3         = 0x3C,
    KEY_F4         = 0x3D,
    KEY_F5         = 0x3E,
    KEY_F6         = 0x3F,
    KEY_F7         = 0x40,
    KEY_F8         = 0x41,
    KEY_F9         = 0x42,
    KEY_F10        = 0x43,
    KEY_F11        = 0x44,
    KEY_F12        = 0x45,
    KEY_RIGHT      = 0x4F,    // 右方向键
    KEY_LEFT       = 0x50,    // 左方向键
    KEY_DOWN       = 0x51,    // 下方向键
    KEY_UP         = 0x52     // 上方向键
};

// Android 系统消费者控制用法码
enum hid_consumer {
    CONSUMER_BACK        = 0x0224,  // Android 返回键 (AC Back)
    CONSUMER_HOME        = 0x0223,  // Android Home 键 (AC Home)
    CONSUMER_MENU        = 0x0194,  // Android 通知面板键 (AL Local Machine Browser)
    CONSUMER_APP_SWITCH  = 0x01A2   // Android 最近任务键 (AC Desktop Show All Applications)
};

// 常量定义
#define HID_KEYBOARD_REPORT_SIZE 8  // 标准键盘报告大小
#define HID_TOUCH_REPORT_SIZE    7  // 触摸报告大小
#define HID_CONSUMER_REPORT_SIZE 1  // 消费者报告大小（uint16_t）

// 函数声明
void send_keyboard_report(uint8_t modifier, uint8_t key1, uint8_t key2, uint8_t key3);
void send_touch_report(uint8_t state, uint16_t x, uint16_t y);
bool char_needs_shift(char c);
uint8_t char_to_scancode(char c);
void press_keys(const char *str);
void press_key_combination(uint8_t modifier, uint8_t key);
void send_consumer_key_report(uint16_t usage_code);
void touch(uint8_t state, int16_t hid_x, int16_t hid_y);
void long_touch(uint8_t state, int16_t hid_x, int16_t hid_y, uint32_t delay_ms);
esp_err_t parse_and_execute_command(const char *cmd);
#endif // HID_USAGE_H
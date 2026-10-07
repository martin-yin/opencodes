// hid/hid_usage.c
#include "hid_usage.h"
#include "ble_gap.h"
#include "esp_hidd.h"
#include "hid_param.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include <string.h>  // 为 memset 和 strchr

static const char *TAG = "hid_usage";

// 发送 HID 键盘报告
void send_keyboard_report(uint8_t modifier, uint8_t key1, uint8_t key2, uint8_t key3) {
    uint8_t buffer[HID_KEYBOARD_REPORT_SIZE];
    memset(buffer, 0, sizeof(buffer));  // 清零缓冲区
    buffer[0] = modifier;
    buffer[2] = key1;  // buffer[1] 保留
    buffer[3] = key2;
    buffer[4] = key3;

    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 3, buffer, sizeof(buffer));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send keyboard report: %d", ret);
        return;
    }
    ESP_LOGD(TAG, "Sent keyboard: mod=0x%02X, keys=[0x%02X,0x%02X,0x%02X]", modifier, key1, key2, key3);
}

// 发送 HID 触摸报告（添加边界检查）
void send_touch_report(uint8_t state, uint16_t x, uint16_t y) {
    if (x > 65535 || y > 65535) {
        ESP_LOGW(TAG, "Invalid touch coords: x=%d, y=%d", x, y);
        return;
    }

    uint8_t buffer[HID_TOUCH_REPORT_SIZE];
    memset(buffer, 0, sizeof(buffer));
    if (state > 0) {
        buffer[0] = 0x03; 
        buffer[1] = 0;
        buffer[2] = x & 0xFF;
        buffer[3] = (x >> 8) & 0xFF;
        buffer[4] = y & 0xFF;
        buffer[5] = (y >> 8) & 0xFF;
        buffer[6] = 1;
    } else {
        buffer[0] = 0x00;
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = 0;
        buffer[4] = 0;
        buffer[5] = 0;
        buffer[6] = 0;    // Contact Count=0
    }
    ESP_LOG_BUFFER_HEX(TAG, buffer, sizeof(buffer));
    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, sizeof(buffer));
    ESP_LOGI(TAG, "esp_hidd_dev_input_set returned: %d", ret);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to send touch report: %d", ret);
        return;
    }
    ESP_LOGI(TAG, "Sent touch: state=0x%02X, x=%d, y=%d, count=%d", buffer[0], x, y, buffer[6]);
}

bool char_needs_shift(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c == '!' || c == '@' || c == '#' || c == '$' || c == '%' ||
            c == '^' || c == '&' || c == '*' || c == '(' || c == ')' ||
            c == '_' || c == '+' || c == '{' || c == '}' || c == '|' ||
            c == ':' || c == '"' || c == '<' || c == '>' || c == '?' ||
            c == '~');
}

// 字符到扫描码的映射（扩展支持移位符号）
uint8_t char_to_scancode(char c) {
    if (c >= 'a' && c <= 'z') {
        return KEY_A + (c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return KEY_A + (c - 'A');
    }
    if (c >= '1' && c <= '9') {
        return KEY_1 + (c - '1');
    }
    if (c == '0') return KEY_0;

    switch (c) {
    case ' ' : return KEY_SPACE;
    case '\n': return KEY_ENTER;
    case '\t': return KEY_TAB;
    case '\b': return KEY_BACKSPACE;
    case '-' : case '_' : return KEY_MINUS;
    case '=' : case '+' : return KEY_EQUAL;
    case '[' : case '{' : return KEY_LEFTBRACE;
    case ']' : case '}' : return KEY_RIGHTBRACE;
    case '\\': case '|' : return KEY_BACKSLASH;
    case ';' : case ':' : return KEY_SEMICOLON;
    case '\'': case '"' : return KEY_APOSTROPHE;
    case '`' : case '~' : return KEY_GRAVE;
    case ',' : case '<' : return KEY_COMMA;
    case '.' : case '>' : return KEY_DOT;
    case '/' : case '?' : return KEY_SLASH;
    case '!' : return KEY_1;
    case '@' : return KEY_2;
    case '#' : return KEY_3;
    case '$' : return KEY_4;
    case '%' : return KEY_5;
    case '^' : return KEY_6;
    case '&' : return KEY_7;
    case '*' : return KEY_8;
    case '(' : return KEY_9;
    case ')' : return KEY_0;
    default  : return 0;
    }
}


void press_keys(const char *str) {
    if (!str) return;
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        uint8_t scancode = char_to_scancode(c);
        if (scancode != 0) {
            uint8_t modifier = char_needs_shift(c) ? KEY_MOD_LSHIFT : 0;
            send_keyboard_report(modifier, scancode, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(10));  // 短暂延迟
            send_keyboard_report(0, 0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// 发送键组合
void press_key_combination(uint8_t modifier, uint8_t key) {
    send_keyboard_report(modifier, key, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    send_keyboard_report(0, 0, 0, 0);
}

void send_consumer_key_report(uint16_t usage_code) {
    uint8_t buffer = 0;  
    switch (usage_code) {
        case CONSUMER_BACK:       buffer |= (1 << 0); break;  // bit0
        case CONSUMER_HOME:       buffer |= (1 << 1); break;  // bit1
        case CONSUMER_MENU:       buffer |= (1 << 2); break;  // bit2
        case CONSUMER_APP_SWITCH: buffer |= (1 << 3); break;  // bit3
        default:
            ESP_LOGW(TAG, "Unknown consumer key: 0x%04X", usage_code);
            return;
    }

    uint8_t buf_bytes[HID_CONSUMER_REPORT_SIZE];  // 假设HID_CONSUMER_REPORT_SIZE=1
    buf_bytes[0] = buffer;

    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 4, buf_bytes, sizeof(buf_bytes));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send consumer report: %d", ret);
    }
    ESP_LOGD(TAG, "Sent consumer key: 0x%04X (buffer: 0x%02X)", usage_code, buffer);

    vTaskDelay(pdMS_TO_TICKS(50));  // 按键持续50ms

    buf_bytes[0] = 0;  // 释放所有键
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 4, buf_bytes, sizeof(buf_bytes));
}

// 发送 HID 坐标触控屏幕
void touch(uint8_t state, int16_t hid_x, int16_t hid_y) {
    send_touch_report(state, hid_x, hid_y);
    if (state > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        send_touch_report(0, hid_x, hid_y);
    }
}

// 长按触摸
void long_touch(uint8_t state, int16_t hid_x, int16_t hid_y, uint32_t delay_ms) {
    send_touch_report(state, hid_x, hid_y);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    send_touch_report(0, hid_x, hid_y);
}

esp_err_t parse_and_execute_command(const char *cmd) {
    const char *TAG = "command_parser";

    if (!cmd || cmd[0] == '\0') {
        ESP_LOGW(TAG, "Empty command");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Parsing command: '%s'", cmd);

    // 处理 press 命令
    if (strncmp(cmd, "press:", 6) == 0) {
        const char* text = cmd + 6;
        if (strlen(text) == 0) {
            ESP_LOGW(TAG, "Invalid press: empty text");
            return ESP_ERR_INVALID_ARG;
        }
        if (strlen(text) > 64) {
            ESP_LOGW(TAG, "Press text too long (max 64)");
            return ESP_ERR_INVALID_SIZE;
        }
        ESP_LOGI(TAG, "Executing press: %s", text);
        press_keys(text);
        return ESP_OK;
    }

    // 处理 touch 命令
    if (strncmp(cmd, "touch:", 6) == 0) {
        int state, x, y;
        if (sscanf(cmd + 6, "%d,%d,%d", &state, &x, &y) != 3) {
            ESP_LOGW(TAG, "Invalid touch: format touch:state,x,y");
            return ESP_ERR_INVALID_ARG;
        }
        if (state < 0 || state > 1) {
            ESP_LOGW(TAG, "Invalid state (0/1)");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Executing touch: state=%d, x=%d, y=%d", state, x, y);
        touch((uint8_t)state, (int16_t)x, (int16_t)y);
        return ESP_OK;
    }

    // 处理 consumer 命令
    if (strncmp(cmd, "consumer:", 9) == 0) {
        uint16_t code;
        if (sscanf(cmd + 9, "0x%hx", &code) != 1 && sscanf(cmd + 9, "%hu", &code) != 1) {
            ESP_LOGW(TAG, "Invalid consumer: format consumer:0xHH or consumer:NN");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Executing consumer: code=0x%04X", code);
        send_consumer_key_report(code);
        return ESP_OK;
    }

    // 处理 longtouch 命令
    if (strncmp(cmd, "longtouch:", 10) == 0) {
        int state, x, y, delay_ms;
        if (sscanf(cmd + 10, "%d,%d,%d,%d", &state, &x, &y, &delay_ms) != 4) {
            ESP_LOGW(TAG, "Invalid longtouch: format longtouch:state,x,y,delay_ms");
            return ESP_ERR_INVALID_ARG;
        }
        if (state < 0 || state > 1) {
            ESP_LOGW(TAG, "Invalid state (0/1)");
            return ESP_ERR_INVALID_ARG;
        }
        if (delay_ms <= 0) {
            ESP_LOGW(TAG, "Invalid delay_ms (must be >0)");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Executing longtouch: state=%d, x=%d, y=%d, delay=%d", state, x, y, delay_ms);
        long_touch((uint8_t)state, (int16_t)x, (int16_t)y, (uint32_t)delay_ms);
        return ESP_OK;
    }

    // 处理 combination 命令
    if (strncmp(cmd, "combination:", 12) == 0) {
        uint8_t modifier, key;
        if (sscanf(cmd + 12, "%hhu,%hhu", &modifier, &key) != 2) {
            ESP_LOGW(TAG, "Invalid combination: format combination:modifier,key");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Executing combination: modifier=0x%02X, key=0x%02X", modifier, key);
        press_key_combination(modifier, key);
        return ESP_OK;
    }

    if (strncmp(cmd, "disconnect", 1) == 0) {
        if (current_conn_handle != 0) {
            int rc = ble_gap_terminate(current_conn_handle, BLE_GAP_EVENT_DISCONNECT);
            if (rc != 0) {
                ESP_LOGE(TAG, "Disconnect failed: %d", rc);
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "Disconnect success");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "No active BLE connection");
        return ESP_ERR_NOT_FOUND;
    }

    // 未知命令
    ESP_LOGW(TAG, "Unknown command: '%s'", cmd);
    return ESP_ERR_NOT_SUPPORTED;
}
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include <stdlib.h>
#include <string.h>
#include "class/hid/hid_device.h"
#include <stdio.h>
#include "esp_task_wdt.h"

#define RXD_PIN (GPIO_NUM_18)
#define TXD_PIN (GPIO_NUM_17)
#define UART_PORT UART_NUM_1
#define MAX_HID_INTERFACES 5
// static uint32_t dev_recv_count = 0; // Device 成功解析的 UART 包总数
// static uint32_t dev_last_tick = 0;
static QueueHandle_t uart_event_queue = NULL;

static uint8_t hid_report_desc[MAX_HID_INTERFACES][512];
static uint16_t hid_report_desc_len[MAX_HID_INTERFACES] = {0};
static uint8_t actual_itf_count = 0; // 动态记录实际接口数

static uint8_t last_report_cache[MAX_HID_INTERFACES][64];
static uint8_t last_report_len[MAX_HID_INTERFACES] = {0};

static tusb_desc_device_t hid_device_descriptor;
static uint8_t hid_configuration_descriptor[1024];
static uint16_t cloned_cfg_len = 0;

static char converted_strings[5][64];

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "",
    "",
    "",
    "",
};

static uint8_t hid_string_desc[4][64];
static uint16_t hid_string_desc_len[4] = {0};
static bool usb_initialized = false;

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (instance < actual_itf_count)
    {
        return hid_report_desc[instance];
    }
    return NULL;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    if (instance < MAX_HID_INTERFACES && last_report_len[instance] > 0)
    {
        uint16_t len = last_report_len[instance] > reqlen ? reqlen : last_report_len[instance];
        memcpy(buffer, last_report_cache[instance], len);
        return len;
    }
    memset(buffer, 0xFF, reqlen);
    return reqlen;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    uint8_t packet[4 + bufsize];
    packet[0] = 0xEE;
    packet[1] = instance;
    packet[2] = report_id;
    packet[3] = (uint8_t)bufsize;
    memcpy(packet + 4, buffer, bufsize);
    uart_write_bytes(UART_PORT, (const char *)packet, 4 + bufsize);
}

// --- 启动 TinyUSB 的函数 ---
void start_usb_emulation(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    uint16_t original_bcd = hid_device_descriptor.bcdUSB;
    if (original_bcd < 0x0200)
    {
        hid_device_descriptor.bcdUSB = 0x0200;
    }

    if (hid_device_descriptor.bMaxPacketSize0 < 0x40)
    {
        hid_device_descriptor.bMaxPacketSize0 = 0x40;
    }
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &hid_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = 5;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    usb_initialized = true;
}

void parse_usb_profile_verbose(uint8_t *buf, int total_len)
{
    int p = 2; // 跳过 AA BB

    while (p < total_len - 2)
    {
        uint8_t tag = buf[p++];

        switch (tag)
        {
        case 0x01:
        {                           // Device Descriptor
            uint8_t len = buf[p++]; // 长度字节 (应该是 18)
            if (len == 18)
            {
                memcpy(&hid_device_descriptor, &buf[p], 18);
            }
            p += len;
            break;
        }

        case 0x02:
        { // Configuration Bundle
            uint16_t len = (buf[p] << 8) | buf[p + 1];
            p += 2;
            if (len <= sizeof(hid_configuration_descriptor))
            {
                cloned_cfg_len = len;
                memcpy(hid_configuration_descriptor, &buf[p], len);
                actual_itf_count = hid_configuration_descriptor[4];
            }
            p += len;
            break;
        }

        case 0x03:
        { // HID Report Descriptors
            uint8_t itf_idx = buf[p++];
            uint16_t r_len = (buf[p] << 8) | buf[p + 1];
            p += 2;
            if (itf_idx < MAX_HID_INTERFACES)
            {
                memcpy(hid_report_desc[itf_idx], &buf[p], r_len);
                hid_report_desc_len[itf_idx] = r_len;
            }
            p += r_len;
            break;
        }

        case 0x04:
        { // String Descriptors
            uint8_t str_idx = buf[p++];
            uint8_t str_len = buf[p++];
            if (str_idx < 4)
            {
                // 存原始数据备用
                memcpy(hid_string_desc[str_idx], &buf[p], str_len);
                hid_string_desc_len[str_idx] = str_len;

                // --- 关键：将 UTF-16 (带Header) 转换为 TinyUSB 想要的 C 字符串 ---
                if (str_idx > 0)
                { // Index 0 是语言ID，不转换
                    int char_count = 0;
                    // USB 字符串描述符：byte0=长, byte1=0x03, 之后是 UTF-16LE
                    for (int j = 2; j < str_len && char_count < 63; j += 2)
                    {
                        converted_strings[str_idx][char_count++] = (char)buf[p + j];
                    }
                    converted_strings[str_idx][char_count] = '\0'; // 闭合字符串
                    hid_string_descriptor[str_idx] = converted_strings[str_idx];
                }
            }
            p += str_len;
            break;
        }

        case 0xEE: // 正常结尾
            return;

        default:
            return;
        }
    }
}

void usb_device_task(void *param)
{
        while (1)
    {
        tud_task(); 
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}


void uart_parse_task(void *param)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(1024); // 临时数据缓冲区
    
    // 状态机变量
    static enum {
        S_SEARCH,
        S_HID_ITF,
        S_HID_LEN,
        S_HID_DATA,
        S_PROFILE_COLLECT
    } state = S_SEARCH;

    static uint8_t itf_idx = 0;
    static uint8_t data_len = 0;
    static uint8_t data_cnt = 0;
    static uint8_t packet_payload[64];
    
    static uint8_t profile_buf[1024];
    static int profile_idx = 0;

    while (1)
    {
        if (xQueueReceive(uart_event_queue, (void *)&event, portMAX_DELAY))
        {
            switch (event.type)
            {
                case UART_DATA:
                    int read_len = uart_read_bytes(UART_PORT, dtmp, event.size, 0);
                    
                    for (int i = 0; i < read_len; i++)
                    {
                        uint8_t b = dtmp[i];
                        switch (state)
                        {
                            case S_SEARCH:
                                if (b == 0xEE) {
                                    state = S_HID_ITF;
                                } else if (b == 0xAA) {
                                    state = S_PROFILE_COLLECT;
                                    profile_idx = 0;
                                    profile_buf[profile_idx++] = b;
                                }
                                break;

                            case S_HID_ITF:
                                itf_idx = b;
                                state = S_HID_LEN;
                                break;

                            case S_HID_LEN:
                                data_len = b;
                                data_cnt = 0;
                                if (data_len > 0 && data_len <= 64) {
                                    state = S_HID_DATA;
                                } else {
                                    state = S_SEARCH;
                                }
                                break;

                            case S_HID_DATA:
                                packet_payload[data_cnt++] = b;
                                if (data_cnt >= data_len) {
                                    if (tud_hid_n_ready(itf_idx)) {
                                        tud_hid_n_report(itf_idx, 0, packet_payload, data_len);
                                        memcpy(last_report_cache[itf_idx], packet_payload, data_len);
                                        last_report_len[itf_idx] = data_len;
                                        // dev_recv_count++;
                                    }
                                    // // 每秒打印一次
                                    // uint32_t now = xTaskGetTickCount();
                                    // if (now - dev_last_tick >= pdMS_TO_TICKS(1000)) {
                                    //     printf("UART -> DEVICE Rate: %ld Hz\n", dev_recv_count);
                                    //     dev_recv_count = 0;
                                    //     dev_last_tick = now;
                                    // }
                                    // state = S_SEARCH;
                                    state = S_SEARCH;
                                }
                                break;

                            case S_PROFILE_COLLECT:
                                if (profile_idx < 1024) {
                                    profile_buf[profile_idx++] = b;
                                }
                                if (b == 0xFF && profile_idx > 1 && profile_buf[profile_idx - 2] == 0xEE) {
                                    if (!usb_initialized) {
                                        parse_usb_profile_verbose(profile_buf, profile_idx);
                                        start_usb_emulation();
                                        xTaskCreatePinnedToCore(usb_device_task, "usbd", 4096, NULL, 24, NULL, 0);
                                        uint8_t ack = 0x55;
                                        uart_write_bytes(UART_PORT, (const char *)&ack, 1);
                                    }
                                    state = S_SEARCH;
                                }
                                break;
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                    uart_flush_input(UART_PORT);
                    xQueueReset(uart_event_queue);
                    state = S_SEARCH;
                    break;

                case UART_BUFFER_FULL:
                    uart_flush_input(UART_PORT);
                    state = S_SEARCH;
                    break;

                default:
                    break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

void app_main(void) {
    // 串口配置
    const uart_config_t uart_config = {
        .baud_rate = 3000000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 8192, 8192, 4, &uart_event_queue, 0));
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, -1, -1);
    xTaskCreatePinnedToCore(uart_parse_task, "uart_svc", 8192, NULL, 24, NULL, 1);
}
/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "freertos/ringbuf.h"
#include "driver/uart.h"

static RingbufHandle_t s_uart_rb = NULL;
#define CLIENT_NUM_EVENT_MSG        8

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_GET_DEV_INFO     = (1 << 1),
    ACTION_GET_DEV_DESC     = (1 << 2),
    ACTION_GET_CONFIG_DESC  = (1 << 3),
    ACTION_GET_STR_DESC     = (1 << 4),
    ACTION_GET_HID_DESC     = (1 << 5),
    ACTION_START_HID        = (1 << 6),    
    ACTION_CLOSE_DEV        = (1 << 7),    
} action_t;

#define DEV_MAX_COUNT           1

typedef struct {
    uint8_t ep_addr;    
    uint16_t mps;     
    uint8_t itf_num;  
    uint16_t report_desc_len;
} hid_ep_info_t;

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    action_t actions;
    hid_ep_info_t hid_eps[5]; 
    uint8_t hid_ep_count;
    uint8_t current_itf_idx;
    usb_transfer_t *hid_transfers[5]; 

    usb_device_desc_t dev_desc;        
    uint8_t config_desc[512];          
    uint16_t config_desc_len;
    uint8_t report_desc[5][512];
    uint16_t report_desc_len[5];


    char manufacturer[32];              
    char product[32];                 
    uint8_t str_desc_raw[4][64]; 
    uint8_t str_desc_len[4];

    bool profile_sent;  
    bool hid_started;   
} usb_device_t;

typedef struct {
    struct {
        union {
            struct {
                uint8_t unhandled_devices: 1;   /**< Device has unhandled devices */
                uint8_t shutdown: 1;            /**<  */
                uint8_t reserved6: 6;           /**< Reserved */
            };
            uint8_t val;                        /**< Class drivers' flags value */
        } flags;                                /**< Class drivers' flags */
        usb_device_t device[DEV_MAX_COUNT];     /**< Class drivers' static array of devices */
    } mux_protected;                            /**< Mutex protected members. Must be protected by the Class mux_lock when accessed */

    struct {
        usb_host_client_handle_t client_hdl;
        SemaphoreHandle_t mux_lock;        
    } constant;                               
} class_driver_t;

static const char *TAG = "CLASS";
static class_driver_t *s_driver_obj;
extern void trigger_class_driver_action(uint32_t action);
extern void handle_usb_out_data(uint8_t *data, size_t len);

static void send_device_profile(usb_device_t *device_obj) {
    uint8_t *buf = malloc(2048); 
    int p = 0;

    // --- [帧头] ---
    buf[p++] = 0xAA; buf[p++] = 0xBB;

    // --- [1. Device Descriptor] Tag: 0x01 ---
    buf[p++] = 0x01; 
    buf[p++] = 18; 
    memcpy(&buf[p], &device_obj->dev_desc, 18); p += 18;

    // --- [2. Configuration Bundle] Tag: 0x02 ---
    // 这里包含了 Config + Interface + HID + Endpoint (59字节)
    buf[p++] = 0x02;
    buf[p++] = (device_obj->config_desc_len >> 8) & 0xFF;
    buf[p++] = device_obj->config_desc_len & 0xFF;
    memcpy(&buf[p], device_obj->config_desc, device_obj->config_desc_len); 
    p += device_obj->config_desc_len;

    // --- [3. HID Report Descriptors] Tag: 0x03 ---
    for (int i = 0; i < device_obj->hid_ep_count; i++) {
        buf[p++] = 0x03; 
        buf[p++] = i; // 接口索引
        buf[p++] = (device_obj->report_desc_len[i] >> 8) & 0xFF;
        buf[p++] = device_obj->report_desc_len[i] & 0xFF;
        memcpy(&buf[p], device_obj->report_desc[i], device_obj->report_desc_len[i]);
        p += device_obj->report_desc_len[i];
    }

    // --- [4. String Descriptors] Tag: 0x04 ---
    // 循环发送 Index 0 (LANGID), 1 (Manu), 2 (Prod)
    for (int i = 0; i < 4; i++) { // 发送 0, 1, 2, 3
        if (device_obj->str_desc_len[i] > 0) {
            buf[p++] = 0x04;
            buf[p++] = i; 
            buf[p++] = device_obj->str_desc_len[i];
            memcpy(&buf[p], device_obj->str_desc_raw[i], device_obj->str_desc_len[i]);
            p += device_obj->str_desc_len[i];
        }
    }

    // --- [帧尾] ---
    buf[p++] = 0xEE; buf[p++] = 0xFF;

    // 发送到环形缓冲区
    xRingbufferSend(s_uart_rb, buf, p, pdMS_TO_TICKS(10));
    free(buf);
}
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ESP_LOGI(TAG, "USB EVENT: %d", event_msg->event); 
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        driver_obj->mux_protected.device[0].dev_addr = event_msg->new_dev.address;
        driver_obj->mux_protected.device[0].dev_hdl = NULL;
        driver_obj->mux_protected.device[0].actions |= ACTION_OPEN_DEV;
        driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGI(TAG, "USB_HOST_CLIENT_EVENT_DEV_GONE received");
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        if (driver_obj->mux_protected.device[0].dev_hdl == event_msg->dev_gone.dev_hdl) {
            driver_obj->mux_protected.flags.unhandled_devices = 1;
            driver_obj->mux_protected.device[0].actions = ACTION_CLOSE_DEV;
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    default:
        abort();
    }
}


static void action_open_dev(usb_device_t *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
    device_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (char *[]) {
        "Low", "Full", "High"
    }[dev_info.speed]);
    ESP_LOGI(TAG, "\tParent info:");
    if (dev_info.parent.dev_hdl) {
        usb_device_info_t parent_dev_info;
        ESP_ERROR_CHECK(usb_host_device_info(dev_info.parent.dev_hdl, &parent_dev_info));
        ESP_LOGI(TAG, "\t\tBus addr: %d", parent_dev_info.dev_addr);
        ESP_LOGI(TAG, "\t\tPort: %d", dev_info.parent.port_num);

    } else {
        ESP_LOGI(TAG, "\t\tPort: ROOT");
    }
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    device_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
    
    // 【新增】保存到结构体
    memcpy(&device_obj->dev_desc, dev_desc, 18); 
    
    usb_print_device_descriptor(dev_desc);
    device_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    ESP_LOGI(TAG, "真鼠标总接口数: %d", config_desc->bNumInterfaces);
    
    uint16_t len = config_desc->wTotalLength;
    if (len > 512) len = 512; // 防止溢出
    memcpy(device_obj->config_desc, config_desc, len);
    device_obj->config_desc_len = len;
    for (int i = 0; i < config_desc->bNumInterfaces; i++) {
        int itf_offset = 0;
        const usb_intf_desc_t *itf = usb_parse_interface_descriptor(config_desc, i, 0, &itf_offset);
        
        if (itf && itf->bInterfaceClass == USB_CLASS_HID) {
            for (int e = 0; e < itf->bNumEndpoints; e++) {
                int ep_offset = itf_offset;
                const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(itf, e, config_desc->wTotalLength, &ep_offset);
                if (ep && (ep->bEndpointAddress & 0x80) && (device_obj->hid_ep_count < 5)) {
                    int idx = device_obj->hid_ep_count;
                    device_obj->hid_eps[idx].ep_addr = ep->bEndpointAddress;
                    device_obj->hid_eps[idx].mps = ep->wMaxPacketSize;
                    device_obj->hid_eps[idx].itf_num = itf->bInterfaceNumber;
                    device_obj->hid_ep_count++;
                    ESP_LOGI(TAG, "发现 HID 接口 %d, 端点 0x%02x, MPS %d", itf->bInterfaceNumber, ep->bEndpointAddress, ep->wMaxPacketSize);
                }
            }
        }
    }
    
    usb_print_config_descriptor(config_desc, NULL);
    device_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(usb_device_t *device_obj) {
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));

    // --- Index 0: LANGID (手动构造，兼容性最强) ---
    device_obj->str_desc_raw[0][0] = 4;    
    device_obj->str_desc_raw[0][1] = 0x03; 
    device_obj->str_desc_raw[0][2] = 0x09; 
    device_obj->str_desc_raw[0][3] = 0x04; 
    device_obj->str_desc_len[0] = 4;

    // --- Index 1: Manufacturer (厂商) ---
    if (dev_info.str_desc_manufacturer) {
        uint8_t *raw = (uint8_t *)dev_info.str_desc_manufacturer;
        uint8_t len = raw[0];
        if (len <= 64) {
            memcpy(device_obj->str_desc_raw[1], raw, len);
            device_obj->str_desc_len[1] = len;
            ESP_LOGI(TAG, "已抓取厂商字符串");
        }
    }

    // --- Index 2: Product (产品) ---
    if (dev_info.str_desc_product) {
        uint8_t *raw = (uint8_t *)dev_info.str_desc_product;
        uint8_t len = raw[0];
        if (len <= 64) {
            memcpy(device_obj->str_desc_raw[2], raw, len);
            device_obj->str_desc_len[2] = len;
            ESP_LOGI(TAG, "已抓取产品字符串");
        }
    }

    // --- Index 3: Serial Number (序列号 - 新增) ---
    if (dev_info.str_desc_serial_num) {
        uint8_t *raw = (uint8_t *)dev_info.str_desc_serial_num;
        uint8_t len = raw[0];
        // 如果你目前的结构体只有 [3][64]，可以暂时存放在这里或扩大结构体
        ESP_LOGI(TAG, "发现设备序列号，长度: %d", len);
    }

    device_obj->actions |= ACTION_GET_HID_DESC;
}

static void hid_report_desc_cb(usb_transfer_t *transfer)
{
    usb_device_t *device_obj = (usb_device_t *)transfer->context;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {

        int idx = device_obj->current_itf_idx; 
        int len = transfer->actual_num_bytes - 8;
        if (len > 512) len = 512;
        ESP_LOGI("HOST_DATA", "收到真鼠标回传: Itf %d, Len %d", idx, transfer->actual_num_bytes);
        
        memcpy(device_obj->report_desc[idx], transfer->data_buffer + 8, len);
        device_obj->report_desc_len[idx] = len;
        ESP_LOGI(TAG, "接口 %d 的 Report Descriptor 已保存", idx);
    }
    usb_host_transfer_free(transfer);
    
    device_obj->current_itf_idx++; 
    if (device_obj->current_itf_idx < device_obj->hid_ep_count) {
        device_obj->actions = ACTION_GET_HID_DESC; 
    } else {
        // --- 修改点：只有第一次完成时才发 Profile ---
        if (!device_obj->profile_sent) { 
            send_device_profile(device_obj);
            device_obj->profile_sent = true; 
            ESP_LOGI(TAG, "Profile Sent. Waiting for Device ACK...");
        }
        device_obj->actions = 0; // 发完后停留在原地，等待 UART 触发 ACTION_START_HID
    }
    s_driver_obj->mux_protected.flags.unhandled_devices = 1;
}

static void action_get_hid_report_desc(usb_device_t *device_obj)
{
    uint8_t itf_num = device_obj->hid_eps[device_obj->current_itf_idx].itf_num;
    
    esp_err_t err = usb_host_interface_claim(device_obj->client_hdl, device_obj->dev_hdl, itf_num, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        device_obj->current_itf_idx++;
        if (device_obj->current_itf_idx < device_obj->hid_ep_count) {
            device_obj->actions = ACTION_GET_HID_DESC;
        } else {
            device_obj->actions = ACTION_START_HID;
        }
        s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        return;
    }

    usb_transfer_t *transfer;
    uint16_t report_desc_len = 256; 
    size_t total_transfer_len = sizeof(usb_setup_packet_t) + report_desc_len;
    usb_host_transfer_alloc(total_transfer_len, 0, &transfer);

    usb_setup_packet_t *setup_pkt = (usb_setup_packet_t *)transfer->data_buffer;
    setup_pkt->bmRequestType = 0x81;
    setup_pkt->bRequest = 0x06;
    setup_pkt->wValue = (0x22 << 8); // HID Report Descriptor
    setup_pkt->wIndex = device_obj->hid_eps[device_obj->current_itf_idx].itf_num;
    setup_pkt->wLength = report_desc_len; 

    transfer->num_bytes = total_transfer_len; 
    
    transfer->device_handle = device_obj->dev_hdl;
    transfer->callback = hid_report_desc_cb;
    transfer->context = device_obj;
    transfer->bEndpointAddress = 0; 

    err = usb_host_transfer_submit_control(device_obj->client_hdl, transfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(transfer);
        device_obj->actions |= ACTION_START_HID;
    }
}


static void hid_transfer_cb(usb_transfer_t *transfer) {
    usb_device_t *device_obj = (usb_device_t *)transfer->context;
    if (device_obj->dev_hdl == NULL || transfer->status == USB_TRANSFER_STATUS_CANCELED) {
        usb_host_transfer_free(transfer);
        return;
    }

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (s_uart_rb != NULL) {
            uint8_t itf_idx = 0;
            for (int i = 0; i < device_obj->hid_ep_count; i++) {
                if (device_obj->hid_transfers[i] == transfer) {
                    itf_idx = i;
                    break;
                }
            }
            uint8_t header[3] = {0xEE, itf_idx, (uint8_t)transfer->actual_num_bytes};
            xRingbufferSend(s_uart_rb, header, 3, 0); 
            xRingbufferSend(s_uart_rb, transfer->data_buffer, transfer->actual_num_bytes, 0);
        }
        usb_host_transfer_submit(transfer); 
    } else {
        usb_host_transfer_submit(transfer);
    }
}


static void action_start_hid_stream(usb_device_t *device_obj) {
    send_device_profile(device_obj);
    for (int i = 0; i < device_obj->hid_ep_count; i++) {
        uint8_t itf_num = device_obj->hid_eps[i].itf_num;
        
        usb_transfer_t *transfer;
        usb_host_transfer_alloc(device_obj->hid_eps[i].mps, 0, &transfer);
        transfer->device_handle = device_obj->dev_hdl;
        transfer->callback = hid_transfer_cb;
        transfer->bEndpointAddress = device_obj->hid_eps[i].ep_addr;
        transfer->num_bytes = device_obj->hid_eps[i].mps;
        transfer->context = device_obj;

        device_obj->hid_transfers[i] = transfer;
        usb_host_transfer_submit(transfer);
        ESP_LOGI(TAG, "已启动监听: 接口 %d, 端点 0x%02x", itf_num, device_obj->hid_eps[i].ep_addr);
    }
}


static void action_close_dev(usb_device_t *device_obj)
{
    if (device_obj->dev_hdl == NULL) return;

    uint8_t disconnect_msg[] = {0xEE, 0xFF, 0x00, 0x00};
    if (s_uart_rb) {
        xRingbufferSend(s_uart_rb, disconnect_msg, sizeof(disconnect_msg), pdMS_TO_TICKS(10));
    } else {
        // 如果你改成了直接发送:
        uart_write_bytes(UART_NUM_1, (const char*)disconnect_msg, sizeof(disconnect_msg));
    }
    ESP_LOGW(TAG, "已发送断开通知给 Device...");
    for (int i = 0; i < 5; i++) {
        if (device_obj->hid_transfers[i] != NULL) {
            usb_host_transfer_free(device_obj->hid_transfers[i]);
            device_obj->hid_transfers[i] = NULL;
        }
    }

    for (int i = 0; i < device_obj->hid_ep_count; i++) {
        uint8_t itf = device_obj->hid_eps[i].itf_num;
        usb_host_interface_release(device_obj->client_hdl, device_obj->dev_hdl, itf);
    }

    usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl);
    device_obj->dev_hdl = NULL;
    device_obj->dev_addr = 0;
    device_obj->hid_ep_count = 0;
    device_obj->current_itf_idx = 0;
    
    device_obj->profile_sent = false;  // 关键：允许下次重连时重新发送配置包
    device_obj->hid_started = false;   // 关键：允许下次重连时重新接收 0x55 ACK
    
    // 清除可能残留在 actions 里的位
    device_obj->actions = 0;
    ESP_LOGW(TAG, "Host 端设备状态已完全复位，等待下一次插入...");
}

static void handle_actions_outside_lock(usb_device_t *device_obj, uint32_t actions)
{
    if (actions & ACTION_OPEN_DEV)        action_open_dev(device_obj);
    if (actions & ACTION_GET_DEV_INFO)    action_get_info(device_obj);
    if (actions & ACTION_GET_DEV_DESC)    action_get_dev_desc(device_obj);
    if (actions & ACTION_GET_CONFIG_DESC) action_get_config_desc(device_obj);
    if (actions & ACTION_GET_STR_DESC)    action_get_str_desc(device_obj);
    if (actions & ACTION_GET_HID_DESC)    action_get_hid_report_desc(device_obj);
    if (actions & ACTION_START_HID)       action_start_hid_stream(device_obj);
    if (actions & ACTION_CLOSE_DEV)       action_close_dev(device_obj);

    if (device_obj->actions != 0) {
        xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
        s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(s_driver_obj->constant.mux_lock);
    }
}


void uart_tx_task(void *arg)
{
    ESP_LOGI(TAG, "UART TX Task started on Core 1");
    while (1) {
        size_t item_size;
        uint8_t *item = (uint8_t *)xRingbufferReceive(s_uart_rb, &item_size, portMAX_DELAY);

        if (item != NULL) {
            uart_write_bytes(UART_NUM_1, (const char*)item, item_size);
            vRingbufferReturnItem(s_uart_rb, (void *)item);
        }
    }
}

/**
 * @brief 异步触发状态机动作
 * 由 UART RX 任务调用，当收到 Device 的 ACK (0x55) 时触发
 */
void trigger_class_driver_action(uint32_t action)
{
    if (s_driver_obj == NULL) return;

    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    
    // 如果还没启动 HID 传输，才响应 ACK
    if (!s_driver_obj->mux_protected.device[0].hid_started && (action & ACTION_START_HID)) {
        s_driver_obj->mux_protected.device[0].actions = ACTION_START_HID; // 强制覆盖，不累加
        s_driver_obj->mux_protected.device[0].hid_started = true;
        s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        ESP_LOGI("TRIGGER", "Valid ACK. Starting HID stream...");
    }
    
    xSemaphoreGive(s_driver_obj->constant.mux_lock);
    usb_host_client_unblock(s_driver_obj->constant.client_hdl);
}

/**
 * @brief 处理从 UART 传来的反向 HID 数据
 * 由 UART RX 任务调用，将 Device 想要发给原始 USB 设备的数据通过 OUT 端点送出
 */
static void usb_out_transfer_cb(usb_transfer_t *transfer)
{
    // if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    //     ESP_LOGI(TAG, "控制传输完成！");
    // } else {
    //     ESP_LOGE(TAG, "控制传输失败，状态码: %d", transfer->status);
    // }
    usb_host_transfer_free(transfer);
}

void handle_usb_out_data(uint8_t *data, size_t len)
{
    if (s_driver_obj == NULL || s_driver_obj->mux_protected.device[0].dev_hdl == NULL) return;

    usb_device_t *device_obj = &s_driver_obj->mux_protected.device[0];
    uint8_t out_ep = 0x01; 
    usb_transfer_t *transfer;
    if (usb_host_transfer_alloc(len, 0, &transfer) == ESP_OK) {
        memcpy(transfer->data_buffer, data, len);
        transfer->num_bytes = len;
        transfer->device_handle = device_obj->dev_hdl;
        transfer->callback = usb_out_transfer_cb;
        transfer->bEndpointAddress = out_ep;
        transfer->context = device_obj;
        usb_host_transfer_submit(transfer);
    }
}

void handle_usb_control_transfer(uint8_t itf_idx, uint8_t report_id, uint8_t *data, size_t len)
{
    if (s_driver_obj == NULL) return;
    
    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    usb_device_t *device_obj = &s_driver_obj->mux_protected.device[0];
    if (device_obj->dev_hdl == NULL || itf_idx >= device_obj->hid_ep_count) {
        xSemaphoreGive(s_driver_obj->constant.mux_lock);
        return;
    }
    uint8_t real_itf_num = device_obj->hid_eps[itf_idx].itf_num;
    xSemaphoreGive(s_driver_obj->constant.mux_lock);

    // --- 关键：罗技 HID++ 2.0 强制包长对齐 ---
    // ID 0x10 (Short) 必须是 7 字节; ID 0x11 (Long) 必须是 20 字节
    size_t wLength = 0;
    if (report_id == 0x10) wLength = 7;
    else if (report_id == 0x11) wLength = 20;
    else wLength = len + 1; 

    usb_transfer_t *transfer = NULL;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &transfer) == ESP_OK) {
        usb_setup_packet_t *setup_pkt = (usb_setup_packet_t *)transfer->data_buffer;
        uint8_t *payload = transfer->data_buffer + sizeof(usb_setup_packet_t);

        memset(payload, 0, wLength);
        payload[0] = report_id;
        memcpy(&payload[1], data, (len < wLength - 1) ? len : (wLength - 1));

        setup_pkt->bmRequestType = 0x21; 
        setup_pkt->bRequest = 0x09;     
      
        setup_pkt->wValue = (0x02 << 8) | report_id; 
        setup_pkt->wIndex = real_itf_num;
        setup_pkt->wLength = wLength;

        transfer->num_bytes = sizeof(usb_setup_packet_t) + wLength;
        transfer->device_handle = device_obj->dev_hdl;
        transfer->callback = usb_out_transfer_cb;
        transfer->context = device_obj;
        transfer->bEndpointAddress = 0;

        esp_err_t err = usb_host_transfer_submit_control(s_driver_obj->constant.client_hdl, transfer);
        if (err != ESP_OK) {
            usb_host_transfer_free(transfer);
        }
    }
}


void uart_rx_task(void *arg) {
    uint8_t *rx_buf = (uint8_t *) malloc(1024);
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, rx_buf, 1024, 1);
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            if (rx_buf[i] == 0x55) {
                trigger_class_driver_action(ACTION_START_HID);
            } 
            else if (rx_buf[i] == 0xEE && (i + 3) < len) {
                uint8_t itf_idx  = rx_buf[i+1];
                uint8_t rep_id   = rx_buf[i+2];
                uint8_t data_len = rx_buf[i+3];
                
                if ((i + 3 + data_len) < len) {
                    handle_usb_control_transfer(itf_idx, rep_id, &rx_buf[i+4], data_len);
                    i += (3 + data_len); // 跳过已处理部分
                }
            }
        }
    }
}

void class_driver_task(void *arg)
{
    class_driver_t driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;

    if (s_uart_rb == NULL) {
        s_uart_rb = xRingbufferCreate(8192, RINGBUF_TYPE_NOSPLIT);
    }
    ESP_LOGI(TAG, "Registering Client");
    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    if (mux_lock == NULL) {
        ESP_LOGE(TAG, "Unable to create class driver mutex");
        vTaskSuspend(NULL);
        return;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = false,  
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));

    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;

    driver_obj.mux_protected.device[0].client_hdl = class_driver_client_hdl;

    s_driver_obj = &driver_obj;

    while (1) {
        uint32_t actions_to_process = 0;
        usb_device_t *dev = &driver_obj.mux_protected.device[0];
        xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
        
        if (driver_obj.mux_protected.flags.unhandled_devices || dev->actions != 0) {
            actions_to_process = dev->actions;
            dev->actions = 0; 
            driver_obj.mux_protected.flags.unhandled_devices = 0;
        }
        
        xSemaphoreGive(driver_obj.constant.mux_lock);

        if (actions_to_process != 0) {
            handle_actions_outside_lock(dev, actions_to_process);
        }
        esp_err_t err = usb_host_client_handle_events(class_driver_client_hdl, pdMS_TO_TICKS(1));
        // if (err == ESP_ERR_TIMEOUT) {
        //     vTaskDelay(pdMS_TO_TICKS(1));
        // }
        if (driver_obj.mux_protected.flags.shutdown) {
            break;
        }
    }
    ESP_LOGI(TAG, "Deregistering Class Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    if (mux_lock != NULL) {
        vSemaphoreDelete(mux_lock);
    }
    vTaskSuspend(NULL);
}

void class_driver_client_deregister(void)
{
    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    if (s_driver_obj->mux_protected.device[0].dev_hdl != NULL) {
        s_driver_obj->mux_protected.device[0].actions |= ACTION_CLOSE_DEV;
        s_driver_obj->mux_protected.flags.unhandled_devices = 1;
    }
    s_driver_obj->mux_protected.flags.shutdown = 1;
    xSemaphoreGive(s_driver_obj->constant.mux_lock);

    ESP_ERROR_CHECK(usb_host_client_unblock(s_driver_obj->constant.client_hdl));
}

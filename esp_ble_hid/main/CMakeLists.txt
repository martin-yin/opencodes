set(srcs 
    "app_main.c"
    "ble/ble_gap.c"
    "hid/hid_report_map.c"
    "ble/hid_param.c"
    "ble/gatt_services.c" 
    "hid/hid_event.c"
    "hid/hid_usage.c"       # 新增
)

idf_component_register(SRCS ${srcs}
                    PRIV_REQUIRES bt esp_hid nvs_flash driver
                    INCLUDE_DIRS "." "ble" "hid")
# 获取 esp_tinyusb 组件的路径
idf_component_get_property(tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB_DIR)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_ringbuf
    EXCLUDE_SRCS "${tinyusb_lib}/descriptors_control.c"
)
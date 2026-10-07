DEV_DESC_SEP = b'\xAA\x55\x00\x01'   # 设备描述符分隔符
CFG_DESC_SEP = b'\xAA\x55\x00\x02'   # 配置描述符分隔符
STR_DESC_SEP = b'\xAA\x55\x00\x03'   # 字符串描述符分隔符
HID_DESC_SEP = b'\xAA\x55\x00\x04'   # HID报告描述符大类分隔符
HID_INTERFACE_SPLIT = b'\xCC\x33\x00\x01'  # HID多接口内部分隔符（核心，必须加）
# 重要：风险、免责声明与支持政策

> **本目录内项目仅供学习与个人实验，按现状提供，不保证兼容性、稳定性、安全性或完整功能。Windows 采集工具会安装、切换、卸载 USB 驱动，可能导致设备失灵、键鼠无法使用或其他 USB 设备受影响；BLE HID 可发送按键和触摸，可能造成误操作；固件实验还存在接线、供电、设备异常和数据丢失风险。请仅在本人所有或已获明确授权的测试设备上使用，不要用于重要设备、生产环境或关键业务。**
>
> **作者不提供技术支持、故障排查、驱动恢复、设备适配、售后服务或持续维护承诺。出现任何问题，请自行排查、恢复和处理，不要联系作者要求解决；提交 Issue 不代表作者承诺回复或修复。使用者应自行备份、准备恢复手段并承担使用风险。**
>
> **在适用法律允许的最大范围内，作者不对使用或无法使用项目造成的设备损坏、数据丢失、系统异常、业务中断或其他损失承担责任。本声明不排除法律规定不得排除或限制的责任；不接受以上条件，请勿使用。**

# USB / BLE HID 实验项目导航

本目录按“USB 描述符采集”“USB 实时键鼠桥接”和“BLE HID 模拟”分组；各工程独立运行，不需要把源码合并到一起。

## 目录与入口

```text
opencodes/
├── README.md
├── usb_capture/
│   ├── README.md
│   ├── windows_capture/          Windows 描述符采集、解析与串口发送
│   └── usb_hid_host/              ESP32-S3 USB Host 采集与 UART HID 转发
├── esp32_kbme/
│   ├── README.md
│   ├── esp-multi-project.code-workspace
│   ├── kbme_host/                 连接真实键鼠
│   └── kbme_device/               连接电脑，使用 TinyUSB 模拟 HID 设备
└── esp_ble_hid/
    ├── README.md
    ├── main/                      UART / GATT 文本指令转 BLE HID
    └── uuid_converter.js          UUID 字节序换算工具，示例须核对固件
```

| 需求 | 文档入口 | 边界 |
| --- | --- | --- |
| 在 Windows 上读取、保存 USB HID 描述符 | [Windows 采集工具](usb_capture/windows_capture/README.md) | 不采集实时按键或鼠标报告，会操作 Windows 驱动 |
| 在 ESP32-S3 上读取描述符并通过 UART 转发 HID 数据 | [USB HID Host](usb_capture/usb_hid_host/README.md) | 本工程没有 USB Device 模拟固件 |
| 确认两个采集项目如何配合 | [采集项目关系与协议](usb_capture/README.md) | 共用描述符帧格式，但两者都是发送端，不能直接互接 |
| 使用两块 ESP32-S3 桥接真实键鼠到电脑 | [ESP32 键鼠桥接](esp32_kbme/README.md) | 使用配套的 `kbme_host` 和 `kbme_device`；仅有罗技 G502 测试记录 |
| 通过串口或 GATT 指令模拟蓝牙键盘、单点触摸和系统键 | [ESP BLE HID](esp_ble_hid/README.md) | 独立 BLE Device；不是 USB 键鼠透传，不能直接接收其他项目的数据帧 |

## 目录命名调整

| 原路径 | 当前路径 |
| --- | --- |
| `usb_capture/usb_device_info_capture-master/` | `usb_capture/windows_capture/` |
| `usb_capture/usb_host_lib-master/` | `usb_capture/usb_hid_host/` |
| `esp32_kbme-master/` | `esp32_kbme/` |

去掉下载目录的 `-master` 后缀；`usb_hid_host` 表明它是已修改的固件应用，不是供 Python 导入的通用库。`kbme_host`、`kbme_device` 名称和内部布局保持不变，VS Code 工作区仍使用相对路径。

Windows 工具目录命名为 `windows_capture`，避免与父目录 `usb_capture` 重复堆叠 USB 和 capture，并明确运行平台。

`esp_ble_hid` 名称已明确表示 ESP BLE HID 工程，保持不变；它独立放在根目录，不归入 USB 采集组。

## 验证边界

文档中的关系、引脚、波特率和协议来自当前源码与配置，不代表硬件联调或兼容性测试通过。Windows 采集工具原文仅记录狼蛛键盘测试；桥接项目原文仅记录罗技 G502 测试，不应将测试结果推广到其他项目或设备。新增 `esp_ble_hid` 没有提供已验证机型记录；其 NimBLE 配置差异、UART0 路由、GATT 控制安全与指令限制见该项目 README。

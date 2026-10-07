# 重要：风险、免责声明与支持政策

> **本固件仅供学习与个人实验，按现状提供，不保证兼容性、稳定性、完整设备功能或零丢包。作者仅记录过本项目配套两板桥接使用罗技 G502 的测试，其他设备未验证。请自行确认接线、电压、USB Host 供电并避免电源回灌，不要用于重要设备或生产环境。**
>
> **作者不提供技术支持、故障排查、设备适配、售后服务或持续维护承诺。出现任何问题，请自行处理，不要联系作者要求解决；提交 Issue 不代表作者承诺回复或修复。请自行备份并承担使用风险。**
>
> **在适用法律允许的最大范围内，作者不对使用或无法使用项目造成的设备损坏、数据丢失、系统异常或其他直接、间接损失承担责任。本声明不排除法律规定不得排除或限制的责任；不接受以上条件，请勿使用。**

# KBME Host：真实 USB 键鼠端

本工程是[ESP32 USB 键鼠桥接](../README.md)的 Host 固件，与同项目的 [kbme_device](../kbme_device/) 配套。完整硬件连接、两板烧录、使用步骤及已知限制以[上级 README](../README.md)为准。

## 运行职责

1. 使用 USB Host Library 接入真实 USB HID 设备，读取设备、配置、部分字符串及 HID Report 描述符。
2. 通过 UART1 发送 `AA BB ... EE FF` 描述符帧，正常流程等待 Device 返回 `55`。
3. 确认后通过 UART 转发实时 HID IN 报告；接收部分 Device 发回的 `SET_REPORT` 请求并转发给原设备。

这不是原版仅打印描述符的 USB Host Library 示例，也不是 Windows 描述符工具的接收端。

## 关键配置

| 项目 | 当前值 |
| --- | --- |
| 目标与环境 | ESP32-S3；仓库依赖锁定记录 ESP-IDF 5.5.2 |
| UART | UART1，3000000 波特率，8N1，无硬件流控 |
| UART 引脚 | GPIO17 TX、GPIO18 RX；两板 TX/RX 交叉连接并共地 |
| 原生 USB | 连接真实键鼠；需适当 Host 转接与 5V VBUS 供电 |

在本工程目录、已加载 ESP-IDF 的终端中运行：

```bash
idf.py set-target esp32s3
idf.py -p <Host板串口> build flash monitor
```

占位符替换为实际烧录串口；退出监视器使用 `Ctrl-]`。Device 板也必须烧录配套固件，不是只烧录 Host 即可完成桥接。

## 与 `usb_capture/usb_hid_host` 的区别

相邻的 [USB HID Host 工程](../../usb_capture/usb_hid_host/README.md)也有描述符采集和 HID 转发逻辑，但默认 UART 为 **921600**，本工程为 **3000000**。两者是独立工程，不要将固件、配置或测试结论视为可直接互换。Windows 采集工具不是本桥接流程的必要步骤；更多协议关系见[采集组说明](../../usb_capture/README.md)。

## 配置与排查入口

- [main/usb_host_lib_main.c](main/usb_host_lib_main.c)：UART 引脚、速率、Host Library 初始化及退出按钮。
- [main/class_driver.c](main/class_driver.c)：描述符帧、ACK、HID 数据与反向控制请求。
- `idf.py menuconfig`：日志等级、退出按钮及 USB 枚举控制传输上限；长描述符还受应用数组和请求长度限制，不能只改菜单配置。
- 停留在等待确认阶段时，检查 Device 是否运行、两板速率是否一致、TX/RX/GND 接线及原生 USB 连接；本文没有新增实测结论，不保证其他设备兼容。

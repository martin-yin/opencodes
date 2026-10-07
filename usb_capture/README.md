# 重要：风险、免责声明与支持政策

> **本组项目仅供学习与个人实验，按现状提供，不保证兼容性、稳定性、采集完整性或驱动恢复成功。Windows 工具会安装、切换、卸载 USB 驱动，可能导致目标设备、键鼠及其他 USB 设备失灵；ESP32 实验还需自行确认接线、电压和供电。不要用于重要设备或生产环境。**
>
> **作者不提供技术支持、故障排查、驱动恢复、设备适配、售后服务或持续维护承诺。出现任何问题，请自行处理，不要联系作者要求解决；提交 Issue 不代表作者承诺回复或修复。请自行备份、准备恢复手段并承担使用风险。**
>
> **在适用法律允许的最大范围内，作者不对使用或无法使用项目导致的直接或间接损失承担责任。本声明不排除法律规定不得排除或限制的责任；不接受以上条件，请勿使用。**

# USB 描述符采集与 HID Host

返回[项目总览](../README.md)。本目录保留两个独立工程：

- [windows_capture](windows_capture/README.md)：Windows Python 工具，采集描述符到 `usb_descriptors.bin`，可解析或通过串口发送。
- [usb_hid_host](usb_hid_host/README.md)：ESP32-S3 固件，连接真实 USB HID 设备，采集描述符，并通过 UART 发送描述符与实时 HID 报告。

## 两个项目能否搭配使用

**可以作为同一套实验中的两种描述符来源，用于采集和对照；但不是现成的“PC 发送端 + ESP32 接收端”组合。** 两个项目都发送描述符，`usb_hid_host` 的 UART 接收逻辑只处理 ACK 与反向 HID 控制报告，不接收 Windows 工具发送的描述符，也不读取 `usb_descriptors.bin`。

当前目录中具备描述符接收和 TinyUSB 模拟逻辑的是相邻的 [kbme_device](../esp32_kbme/kbme_device/main/main.c)，但它属于[两板桥接项目](../esp32_kbme/README.md)，默认配置与两个采集工程不同，不是即插即用的接收端。

| 工程 | 默认数据串口速率 | 数据来源/接收角色 |
| --- | --- | --- |
| `windows_capture` | 115200 | PC 读取真实设备，发送离线描述符 |
| `usb_hid_host` | 921600 | ESP32 读取真实设备，发送描述符与实时 HID 报告 |
| `esp32_kbme/kbme_host` | 3000000 | 实时桥接 Host，与同项目 Device 配套 |
| `esp32_kbme/kbme_device` | 3000000 | 接收描述符与 HID 报告，使用 TinyUSB 模拟设备 |

以上固件数据通道使用 UART1，TX 为 GPIO17、RX 为 GPIO18，8N1、无硬件流控；这是板间 TTL UART，不是固件烧录/日志串口。

## 按目标选择流程

### 只采集和查看描述符

1. 按 [Windows 工具说明](windows_capture/README.md)准备环境并抓取。
2. 在该工程目录运行 `python check.py`，查看生成的解析报告。
3. 若需对照 ESP32 的采集结果，另行按 [Host 固件说明](usb_hid_host/README.md)连接真实设备，观察日志和 UART1 输出；两种采集环境独立使用。

### 实时键鼠桥接

直接使用 `esp32_kbme/kbme_host` 和 `esp32_kbme/kbme_device`，按[桥接项目说明](../esp32_kbme/README.md)接线、编译和烧录。正常桥接不需要先在 Windows 抓取描述符，也不需要先点击“烧录”。

### 用 Windows 描述符尝试 USB 模拟

这是需要适配和验证的实验路径，当前默认配置不能直接运行：

1. 接收端必须实现下述描述符协议；`kbme_device` 中已有相同帧结构的解析实现可参考。
2. 发送端和接收端统一 UART 波特率。PC 需经 3.3V TTL USB-UART 接到 Device 的 UART1：适配器 TX → GPIO18，适配器 RX ← GPIO17，共地。开发板的烧录/日志 COM 口不能默认当成 UART1。
3. 核对原设备字符串索引、HID 接口映射、配置长度、Report 描述符长度及 TinyUSB HID 接口数，再发送。当前 `kbme_device` 整帧缓冲区为 1024 字节，每个 HID Report 描述符缓冲区为 512 字节，字符串处理只覆盖索引 0～3；超过范围不能直接使用。
4. Device 的原生 USB 接电脑；采集工具只发送描述符，不发送实时按键/移动数据，因此不能替代 Host 的实时报告转发。

这里仅说明源码关系，未修改串口速率或接收固件，未验证这条跨项目路径。

## 共用的描述符串口帧

帧头为 `AA BB`，帧尾为 `EE FF`。每个描述符块以 Tag 开头；两字节长度使用大端序，描述符原始内容不转换字节序。

| Tag | 块格式（Tag 后） | 内容 |
| --- | --- | --- |
| `01` | `长度:u8 + 数据` | 设备描述符，发送长度为 18 |
| `02` | `长度:u16be + 数据` | 配置描述符树 |
| `03` | `编号:u8 + 长度:u16be + 数据` | HID Report 描述符，可有多块 |
| `04` | `索引:u8 + 长度:u8 + 数据` | USB 字符串原始描述符，可有多块 |

不要混淆两种格式：`usb_descriptors.bin` 使用 `AA 55 00 01/02/03` 分隔描述符段；Windows 发送程序会先拆解文件，再重打包为以上串口帧。ESP32 Host 直接构造串口帧，不输出该文件。

共用帧格式不代表描述符语义完全一致：Windows 工具按成功采集顺序重新编号，文件未保存原始字符串索引和 HID 接口号；ESP32 Host 的 Report 编号是所收集 HID IN 端点的顺序，字符串按固定槽位整理。索引不连续或复杂复合设备需单独核对。

Host 的正常流程在描述符发送后等待 Device 返回 `55`，随后发送实时 HID 帧 `EE + 编号:u8 + 长度:u8 + 数据`；Windows 工具没有这一步实时采集。

## 来源与验证边界

- Windows 描述符采集：[usb_engine.py](windows_capture/usb_engine.py)；GUI 串口封装与发送：[com_burn.py](windows_capture/com_burn.py)；命令行发送：[main.py](windows_capture/main.py)。
- ESP32 Host 描述符封装、ACK 和 HID 转发：[class_driver.c](usb_hid_host/main/class_driver.c)；UART 引脚与速率：[usb_host_lib_main.c](usb_hid_host/main/usb_host_lib_main.c)。
- 配套桥接接收端：[kbme_device/main/main.c](../esp32_kbme/kbme_device/main/main.c)。

上述结论来自当前源码，不是硬件联调结果；原工程中的测试记录仅适用于各自声明的设备与环境。

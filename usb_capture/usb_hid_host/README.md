# 重要：风险、免责声明与支持政策

> **本固件仅供学习与个人实验，按现状提供，不保证设备兼容性、稳定性、采集完整性、零丢包或 1000 Hz 回报率。请自行确认接线、电压、USB Host 转接和 VBUS 供电，避免不同 USB 电源之间回灌。不要用于重要设备、生产环境或关键业务。**
>
> **作者不提供技术支持、故障排查、设备适配、售后服务或持续维护承诺。出现任何问题，请自行处理，不要联系作者要求解决；提交 Issue 不代表作者承诺回复或修复。请自行备份并承担使用风险。**
>
> **在适用法律允许的最大范围内，作者不对使用或无法使用项目造成的设备损坏、数据丢失、系统异常或其他直接、间接损失承担责任。本声明不排除法律规定不得排除或限制的责任；不接受以上条件，请勿使用。**

# ESP32-S3 USB HID Host / UART 转发

本工程基于 Espressif USB Host Library 示例修改，已增加 HID 描述符采集、UART 描述符发送、ACK 启动实时 HID 转发及部分反向 `SET_REPORT` 处理；不再只是原版的描述符打印示例。

返回[采集项目关系与协议](../README.md)或[项目总览](../../README.md)。

## 功能与项目关系

1. 安装 USB Host Library，等待真实 USB HID 设备接入。
2. 获取设备、当前配置、部分字符串及 HID Report 描述符。
3. 通过 UART1 发送 `AA BB ... EE FF` 描述符帧；正常描述符流程随后等待接收端返回 `55`。
4. 收到 ACK 后，将 HID IN 数据通过 UART1 转发；UART RX 可处理部分反向 HID 控制报告。
5. 设备移除时发送断开通知；退出按钮触发工程关闭流程。

**本工程是 Host 采集/发送端，不是 Windows 采集工具的接收端，不读取 `usb_descriptors.bin`，也不包含 TinyUSB Device 模拟逻辑。** 两者的共用帧格式、差异和适配条件见[采集组说明](../README.md)。

若要运行完整的两板键鼠桥接，使用相邻项目配套的 [kbme_host + kbme_device](../../esp32_kbme/README.md)，不要将本工程与其默认 Device 固件直接混用：双方默认 UART 波特率不同。

## 硬件与串口

当前 `sdkconfig` 的目标为 **ESP32-S3**。原示例的 S2/P4 目标表不代表这个修改版已验证其他芯片；当前代码还将 UART 任务固定到 Core 1。

- 使用带原生 USB Host/OTG 能力的 ESP32-S3 开发板，真实键鼠接原生 USB 数据口。
- 自行提供适当的 Host 转接和 5V VBUS 供电；烧录/日志用的 USB 转串口口不能代替原生 USB 数据口。
- UART1：**921600 波特率、8N1、无硬件流控**；GPIO17 为 TX，GPIO18 为 RX。
- 外接接收端时，Host TX → 接收端 RX、Host RX ← 接收端 TX，并共地；双方统一波特率，使用 3.3V TTL，不要接 RS-232 电平。
- 退出按钮由 `CONFIG_APP_QUIT_PIN` 配置，当前为 GPIO0；按键仅用于固件的关闭流程。

## 配置、编译与烧录

当前 `sdkconfig` 由 **ESP-IDF 5.5.2** 生成，建议优先使用该版本；没有其他版本的验证结论。

安装并加载 ESP-IDF 环境后，在**本工程目录 `usb_hid_host/`**执行：

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py -p <Host板串口> build flash monitor
```

将占位符替换为实际烧录串口；退出监视器使用 `Ctrl-]`。

- `menuconfig` 可调整退出按钮 GPIO、日志等级以及 `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE`。当前枚举控制传输上限为 256 字节，长配置/字符串可能需要调整。
- `sdkconfig.defaults` 启用了 USB Hub 支持，但应用内部 `DEV_MAX_COUNT=1`，不能据此认为可以同时桥接多个设备。
- UART 引脚与速率位于 [main/usb_host_lib_main.c](main/usb_host_lib_main.c)；不是可由上述菜单直接选择的参数。

## 观察运行状态

以下是源码中的日志定位点，**不是本次运行产生的测试输出**：

- `USB to UART 1000Hz Bridge Start`：应用启动；名称不构成实际回报率保证。
- `发现 HID 接口 ...`、`Report Descriptor 已保存`：已发现 HID IN 端点并读取报告描述符。
- `Profile Sent. Waiting for Device ACK...`：进入等待接收端确认的阶段。
- `Valid ACK. Starting HID stream...`：接收端确认后开始 HID 转发。

仅接入真实键鼠、没有接收端时，正常流程可能停留在等待 ACK 阶段。日志串口与 UART1 数据输出是两个不同通道，监视器不会自动将 UART1 描述符保存为 Windows 工具的二进制文件。

## 已知限制与排查

- 当前只管理一个设备，最多收集 5 个 HID IN 端点；Report 的传输编号按收集顺序生成，不等于原始 USB 接口号。
- 配置描述符保存到 512 字节数组，超过该长度会截断；当前 HID Report 控制请求长度固定为 256 字节。增大枚举控制传输配置不等于同步增大应用这些限制。
- 字符串槽位 0 构造为 LANGID `0x0409`，厂商/产品放入槽位 1/2，单条最多保存 64 字节；当前序列号分支仅记录长度，未保存到发送槽位 3。不能视为原设备字符串的完整复制。
- 反向控制包含罗技 HID++ 长度处理，不是完整 USB 总线透传；厂商扩展、复杂复合接口和热插拔行为需自行验证。
- 枚举失败时，先检查 Host 转接、原生 USB 数据口、VBUS 供电及描述符长度，再用 `menuconfig` 提高日志等级。原始库说明指出，其枚举字符串缓存使用 LANGID `0x0409`，缺失字符串不一定代表设备没有该字符串。
- 本文不新增硬件测试结论；相邻桥接工程的罗技 G502 测试记录不能视为本工程验证。

## 实现依据

- [main/class_driver.c](main/class_driver.c)：设备发现、描述符、ACK、HID 数据与反向控制处理。
- [main/usb_host_lib_main.c](main/usb_host_lib_main.c)：Host Library、UART 配置、任务及退出按钮。
- [sdkconfig](sdkconfig)、[sdkconfig.defaults](sdkconfig.defaults)：目标、ESP-IDF 版本记录与默认配置。
- [Espressif USB Host Library API（ESP-IDF 5.5.2 / ESP32-S3）](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/usb_host.html)：原示例使用的库接口参考；本工程行为以当前源码为准。

# 重要：风险、免责声明与支持政策

> **本项目仅供学习、研究与个人实验，按现状提供，不保证兼容性、稳定性、安全性或完整功能。BLE HID 可向已连接设备发送按键与触摸操作，可能造成误操作、数据丢失或其他损失。请仅在本人所有或已获明确授权的测试设备上使用，不要用于重要设备、生产环境或关键业务。当前控制接口不应视为具备可靠身份认证或访问控制。**
>
> **作者不提供技术支持、故障排查、设备适配、售后服务或持续维护承诺。出现任何问题，请自行排查、恢复和处理，不要联系作者要求解决；提交 Issue 不代表作者承诺回复或修复。请自行备份、准备恢复手段并承担使用风险。**
>
> **在适用法律允许的最大范围内，作者不对使用或无法使用项目造成的设备损坏、数据丢失、系统异常、业务中断或其他直接、间接损失承担责任。本声明不排除法律规定不得排除或限制的责任；不接受以上条件，请勿使用。**

# ESP BLE HID：文本指令控制的蓝牙输入设备

返回[项目总览](../README.md)。本工程通过 ESP-IDF 的 NimBLE 与 `esp_hid` 实现 BLE HID Device，广播名称为 **`NIMBLE HID`**。

## 功能与项目关系

- 接收 UART0 或自定义 GATT 特征写入的文本指令，生成键盘、单点绝对坐标触摸、Consumer Control 报告。
- 使用固定 HID Report Map，不采集真实 USB 设备的描述符，不是 BLE HID Host，也不是 USB 键鼠透传或传统蓝牙 HID 工程。
- 与 [usb_capture](../usb_capture/README.md)、[esp32_kbme](../esp32_kbme/README.md) 独立；不接收它们的 `AA BB ... EE FF` 描述符帧或 `EE ...` HID 数据帧，不能直接互接。
- 当前目录未提供配套 PC/手机控制客户端，也未提供已验证机型或操作系统清单。

## 环境、配置与构建

当前 [sdkconfig](sdkconfig) 记录目标为 **ESP32-S3**、ESP-IDF **5.5.0**，启用 `CONFIG_BT_NIMBLE_ENABLED`。这些是配置记录，不代表本次构建或硬件验证通过。

**配置不一致：**[sdkconfig.defaults](sdkconfig.defaults) 及 S3/C3 分目标默认文件仍选择 Bluedroid，源码则直接调用 NimBLE API。删除或重新生成 `sdkconfig`（包括执行 `idf.py set-target`）后，不可假定默认配置仍适合本工程；需在 `menuconfig` 中重新核对蓝牙已启用、Host 为 **NimBLE**、不是 Bluedroid。C3 默认文件的存在不代表 C3 已验证。

安装并加载对应 ESP-IDF 环境后，在 `esp_ble_hid/` 目录执行；保留当前 S3 配置时不必先运行 `set-target`：

```bash
idf.py menuconfig
idf.py -p <开发板烧录串口> build flash monitor
```

将占位符替换为实际烧录串口，退出监视器使用 `Ctrl-]`。编译和烧录步骤供使用者执行，本文没有构建成功结论。

## 输入通道

### UART0

- **115200 波特率、8N1、无硬件流控**，与当前默认日志控制台共用 UART0。
- 程序没有调用 `uart_set_pin`，不应套用其他项目的 GPIO17/18；具体 UART0 引脚与接口需按开发板原理图和配置确认。
- 源码日志中的“Type-C USB”不代表任意 Type-C 口都能输入指令。当前 USB Serial/JTAG 被配置为次级日志控制台，但命令任务读取的是 UART0，不是 USB Serial/JTAG。
- 使用外部 USB-UART 时，应为 3.3V TTL，TX/RX 交叉连接并共地，不要接 RS-232 电平。串口工具发送短文本命令，测试时一次一条，可附 CR/LF。
- UART 任务每次最多读取 127 字节，遇到首个 CR/LF 即截断；没有跨读取的命令拼接，也不逐条处理同一次读取里的多行。分包或连续发送可能导致指令被截断或丢失，不是可靠的行协议。

### 自定义 GATT

连接 `NIMBLE HID` 后，使用可写 GATT 特征的工具发现服务，再以文本字节写入命令；支持 Write 和 Write Without Response。

| 项目 | UUID |
| --- | --- |
| 自定义控制服务 | `24ed2a4a-643b-4581-87b7-b4d2777de046` |
| 命令写入特征 | `c34ae534-f289-45b2-b262-f6f415408d65` |

UUID 根据 [gatt_services.c](main/ble/gatt_services.c) 中 `BLE_UUID128_INIT` 的字节顺序换算。**[uuid_converter.js](uuid_converter.js) 中命令特征的示例数组与固件不同，不能直接复制其示例输出连接固件。**

当前 GATT 回调限制所读取数据段最多 63 字节，未拼接链式 mbuf；优先使用短命令、单次写入，不要假定长写入可用。`press:` 的解析器文本上限为 64 字节，但 GATT 整条命令还包含前缀，受上述更小限制约束。

## 指令与示例

以下内容来自当前解析器，不是本次设备运行结果。先在接收端打开可安全输入的测试界面，确认 HID 已连接并订阅相应报告，再尝试短命令。

| 格式 | 示例 | 当前行为 |
| --- | --- | --- |
| `press:文本` | `press:Hello` | 逐字符按下/释放；文本不能为空、最多 64 字节；仅映射部分 ASCII 字符 |
| `combination:修饰键,扫描码` | `combination:2,4` | 十进制参数；示例为左 Shift + A，约 50 ms 后释放 |
| `touch:状态,x,y` | `touch:1,1000,1000` | 状态 0/1；状态 1 发送单点触摸，约 50 ms 后释放 |
| `longtouch:状态,x,y,毫秒` | `longtouch:1,1000,1000,500` | 状态 0/1，时间必须大于 0；等待指定时间后释放 |
| `consumer:用法码` | `consumer:0x0224` | 接受十六进制或十进制；发送对应 Consumer Control，约 50 ms 后释放 |
| `disconnect` | `disconnect` | 尝试断开记录的 BLE 连接，存在下述实现限制 |

修饰键与扫描码见 [hid_usage.h](main/hid/hid_usage.h)。Consumer 解析支持 Back `0x0224`、Home `0x0223`、Menu `0x0194`、App Switch `0x01A2`；**Menu 输入码 `0x0194` 与 Report Map 声明的 `0x0040` 不同**，因此不能将命令输入码直接当作发给主机的 HID Usage。实际系统操作由接收端决定，不保证所有手机或电脑都支持。

## 使用步骤与观察点

1. 核对开发板、NimBLE 配置和 UART0 路由，按上述命令构建、烧录。
2. 在获授权的测试设备上查找 `NIMBLE HID`，尝试配对并作为 HID 使用。每次启动广播设置为 180000 ms（3 分钟）；广播完成事件只记日志，超时后不能假定会自动重启。
3. 选用 UART0 或 GATT 输入短命令；先测试 `press:Hello`，再按接收端能力测试触摸或系统键。
4. 日志中的 `CONNECT`、`Received UART command` / `Received GATT command`、发送返回值可用于定位阶段。解析成功或发送 API 返回成功不等于接收设备执行成功，需观察实际输入结果。
5. 结束实验时断开配对或关闭开发板。HID 断开事件会调用重新广播，但不是已验证的自动重连保证。

## 已知限制与安全注意

- HID Report ID：触摸 1、键盘 3、Consumer 4；当前 Report Map 没有鼠标移动或滚轮报告。键盘布局按扫描码映射，不能直接输入中文或保证不同键盘布局显示相同字符。
- 触摸 Report Map 的坐标逻辑范围为 0～65535，但命令入口先转换为 `int16_t`，没有完整坐标范围检查。负数或超范围输入可能转换成非预期坐标；`SCREEN_WIDTH/SCREEN_HEIGHT` 宏没有用于坐标缩放，输入不是自动换算后的屏幕像素。
- `press`、组合键、长按等同步等待；长按命令在 GATT 回调内执行时也会阻塞该调用，不要假定支持并发或实时响应。
- `disconnect` 当前只比较第一个字符 `d`，并将连接句柄 0 当作无连接；不能认为它严格匹配命令或一定能断开合法连接。不要用它代替可靠的人工断开方式。
- 配对设置为无输入/输出，`sm_mitm=0`、`sm_sc=0`；自定义写入特征未声明加密/认证权限，也没有应用层鉴权。只能在可信、隔离的测试环境使用，不能把它当作安全控制通道。
- 当前 `CONFIG_BT_NIMBLE_NVS_PERSIST` 未启用；启动初始化 NVS 不等于配对信息一定持久保存。重复配对逻辑会删除旧配对信息；NVS 初始化遇到指定错误时会擦除该 NVS 分区。
- 手机/电脑对触摸、Consumer Control、多连接及重连的行为未验证；配置允许的连接数量不代表应用已正确实现多主机控制。

## 源码入口与验证边界

| 文件 | 内容 |
| --- | --- |
| [main/app_main.c](main/app_main.c) | HID 身份、NimBLE/NVS 初始化、UART0 参数 |
| [main/ble/ble_gap.c](main/ble/ble_gap.c) | 广播、连接、配对与安全设置 |
| [main/ble/gatt_services.c](main/ble/gatt_services.c) | 自定义 GATT UUID、写入及长度限制 |
| [main/hid/hid_event.c](main/hid/hid_event.c) | HID 事件与 UART 读取 |
| [main/hid/hid_usage.c](main/hid/hid_usage.c) | 文本指令解析和报告发送 |
| [main/hid/hid_report_map.c](main/hid/hid_report_map.c) | 固定 HID Report Map |

本说明依据当前本地源码和配置整理，未修改固件逻辑或配置，未运行固件构建、烧录、蓝牙配对或硬件联调；不能据此声称项目可在某设备上正常使用。

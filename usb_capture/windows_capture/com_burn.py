import struct
import serial
import time
import os
import logging
from utils import DEV_DESC_SEP, CFG_DESC_SEP, STR_DESC_SEP, HID_INTERFACE_SPLIT

logger = logging.getLogger("COM_Burn")
# logger.propagate = True
# 增加日志打印级别，确保info级别的日志能正常输出
logger.setLevel(logging.INFO)

class COMBurn:
    def __init__(self):
        current_dir = os.getcwd()
        os.environ['PATH'] = current_dir + os.pathsep + os.environ['PATH']
        logger.info(f"📂 初始化完成，当前工作目录: {current_dir}")
        logger.info(f"🔧 环境变量PATH已追加当前目录")
    
    def extract_raw_parts(self, bin_file):
        logger.info(f"\n🔍 开始解析bin文件: {bin_file}")
        if not os.path.exists(bin_file):
            logger.info(f"❌ 找不到文件: {bin_file}")
            return None
        logger.info(f"✅ 成功找到bin文件，开始读取二进制数据")
        with open(bin_file, 'rb') as f:
            data = f.read()
        logger.info(f"📄 已读取bin文件，总字节数: {len(data)} 字节")
        try:
            parts_dev = data.split(DEV_DESC_SEP)
            dev_raw = parts_dev[0]
            parts_cfg = parts_dev[1].split(CFG_DESC_SEP)
            cfg_raw = parts_cfg[0]
            parts_str = parts_cfg[1].split(STR_DESC_SEP)
            str_blobs = parts_str[0]
            hid_raw_all = parts_str[1]
            logger.info(f"✅ bin文件拆解成功，各部分长度：")
            logger.info(f"   设备描述符: {len(dev_raw)} 字节")
            logger.info(f"   配置描述符: {len(cfg_raw)} 字节")
            logger.info(f"   字符串描述符块: {len(str_blobs)} 字节")
            logger.info(f"   HID描述符块: {len(hid_raw_all)} 字节")
            return dev_raw, cfg_raw, str_blobs, hid_raw_all
        except Exception as e:
            logger.info(f"❌ 拆解 bin 文件失败: {e}")
            return None

    def build_and_debug_payload(self, dev_raw, cfg_raw, str_blobs, hid_raw_all):
        logger.info(f"\n⚙️  开始构建Payload数据...")
        payload = bytearray()
        payload.extend([0xAA, 0xBB]) # 起始头
        
        logger.info("\n" + "="*60)
        logger.info("📊 构建 Payload 拆解分析:")
        logger.info("-" * 60)

        # Tag 0x01: Device (18 bytes)
        payload.append(0x01)
        payload.append(18)
        payload.extend(dev_raw[:18])
        logger.info(f"Offset {len(payload)-20:03d} | [Tag 0x01] 设备描述符 | 长度: 18")

        # Tag 0x02: Config
        payload.append(0x02)
        cfg_len = len(cfg_raw)
        payload.extend(struct.pack('>H', cfg_len)) # 大端序
        payload.extend(cfg_raw)
        logger.info(f"Offset {len(payload)-cfg_len-3:03d} | [Tag 0x02] 配置描述符 | 长度: {cfg_len}")

        # Tag 0x03: HID Reports
        hid_interfaces = hid_raw_all.split(HID_INTERFACE_SPLIT)
        logger.info(f"🔍 解析到HID接口数量: {len([h for h in hid_interfaces if h])} 个")
        for idx, report in enumerate(hid_interfaces):
            if report:
                r_len = len(report)
                payload.append(0x03)
                payload.append(idx)
                payload.extend(struct.pack('>H', r_len))
                payload.extend(report)
                logger.info(f"Offset {len(payload)-r_len-4:03d} | [Tag 0x03] HID接口 {idx} | 长度: {r_len}")

        # Tag 0x04: Strings
        logger.info(f"🔍 开始解析字符串描述符块，总长度: {len(str_blobs)} 字节")
        ptr = 0
        s_idx = 0
        while ptr < len(str_blobs):
            s_len = str_blobs[ptr]
            if s_len == 0 or (ptr + s_len) > len(str_blobs): 
                logger.info(f"📌 字符串描述符解析结束，共解析 {s_idx} 个字符串")
                break
            raw_string_data = str_blobs[ptr : ptr + s_len]
            payload.append(0x04)
            payload.append(s_idx)
            payload.append(s_len)
            payload.extend(str_blobs[ptr : ptr + s_len])
            hex_content = " ".join(f"{b:02X}" for b in raw_string_data)
            logger.info(f"Offset {len(payload)-s_len-3:03d} | [Tag 0x04] 索引 {s_idx} | 长度: {s_len:02d} | Hex: {hex_content[:50]}{'...' if len(hex_content)>50 else ''}")
            ptr += s_len
            s_idx += 1

        # Footer
        payload.extend([0xEE, 0xFF])
        logger.info("-" * 60)
        logger.info(f"✅ Payload 构建完成，总计: {len(payload)} 字节")
        logger.info(f"📋 Payload头部: 0xAA 0xBB | 尾部: 0xEE 0xFF")
        logger.info("="*60 + "\n")
        return payload

    def burn(self, port):
        logger.info(f"\n🚩 开始执行Burn操作，目标串口: {port}")
        logger.info(f"📁 待解析的bin文件: usb_descriptors.bin")
        parts = self.extract_raw_parts("usb_descriptors.bin")
        if not parts: 
            logger.info(f"❌ Burn操作终止：bin文件解析失败")
            return
        logger.info(f"✅ bin文件解析成功，开始构建最终发送Payload")
        final_data = self.build_and_debug_payload(*parts)
        logger.info(f"✅ 最终发送Payload构建完成，总长度: {len(final_data)} 字节")
        try:
            logger.info(f"📡 正在初始化串口 {port}，波特率: 115200，超时: 1s")
            ser = serial.Serial(port, 115200, timeout=1)
            logger.info(f"✅ 串口 {port} 打开成功")
            logger.info(f"⏳ 等待2秒，让设备完成初始化...")
            time.sleep(2) 
            logger.info(f"🧹 清空串口输入缓冲区...")
            ser.reset_input_buffer()
            logger.info(f"🚀 开始分块发送数据，块大小: 64 字节")
            chunk_size = 64
            total_chunks = (len(final_data) + chunk_size - 1) // chunk_size  # 计算总块数
            logger.info(f"📊 发送计划：共 {total_chunks} 块，总计 {len(final_data)} 字节")
            for i in range(0, len(final_data), chunk_size):
                chunk = final_data[i:i + chunk_size]
                ser.write(chunk)
                current_chunk = (i // chunk_size) + 1
                # 给 ESP32 处理中断的时间
                time.sleep(0.05) 
                logger.info(f"   ✅ 第 {current_chunk}/{total_chunks} 块发送完成 | 已发送 {min(i+chunk_size, len(final_data))}/{len(final_data)} 字节")
            
            logger.info("\n🎉 所有数据发送完毕，开始监听ESP32返回信息...")
            logger.info(f"💡 提示：按 Ctrl+C 可停止监听并关闭串口")
            while True:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"   [ESP32] {line}")
                time.sleep(0.01)
        except KeyboardInterrupt:
            print("\n🛑 接收到手动停止指令（Ctrl+C），正在关闭串口...")
        except serial.SerialException as e:
            print(f"❌ 串口操作错误: {e}")
            print(f"💡 请检查：1.串口 {port} 是否存在 2.串口是否被其他程序占用 3.设备是否正常连接")
        except Exception as e:
            print(f"❌ 运行错误: {e}")
        finally:
            if 'ser' in locals() and ser.is_open:
                ser.close()
                logger.info(f"✅ 串口 {port} 已成功关闭")
            logger.info(f"🔚 Burn操作流程结束")


# if __name__ == "__main__":
#     logger.info("="*80)
#     logger.info("🎯 USB描述符Burn工具 开始运行")
#     logger.info("="*80)
#     com_Burn = COMBurn()
#     com_Burn.Burn("COM9")  # 替换为实际的串口
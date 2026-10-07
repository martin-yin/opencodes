import struct
import serial
import time
import os

# --- 1. 串口配置 ---
SERIAL_PORT = 'COM9'  # 确认是你的 COM6
BAUD_RATE = 115200
INPUT_BIN = 'usb_descriptors.bin'

# --- 2. 协议分隔符 (必须与 check.py 采集时一致) ---
DEV_SEP = b'\xAA\x55\x00\x01'
CFG_SEP = b'\xAA\x55\x00\x02'
STR_SEP = b'\xAA\x55\x00\x03'
HID_SPLIT = b'\xCC\x33\x00\x01'

def extract_raw_parts(bin_file):
    if not os.path.exists(bin_file):
        print(f"❌ 找不到文件: {bin_file}")
        return None
    with open(bin_file, 'rb') as f:
        data = f.read()
    try:
        # 按照 check.py 的逻辑拆分
        parts_dev = data.split(DEV_SEP)
        dev_raw = parts_dev[0]
        parts_cfg = parts_dev[1].split(CFG_SEP)
        cfg_raw = parts_cfg[0]
        parts_str = parts_cfg[1].split(STR_SEP)
        str_blobs = parts_str[0]
        hid_raw_all = parts_str[1]
        return dev_raw, cfg_raw, str_blobs, hid_raw_all
    except Exception as e:
        print(f"❌ 拆解 bin 文件失败: {e}")
        return None

def build_and_debug_payload(dev_raw, cfg_raw, str_blobs, hid_raw_all):
    payload = bytearray()
    payload.extend([0xAA, 0xBB]) # 起始头
    
    print("\n" + "="*60)
    print("📊 构建 Payload 拆解分析:")
    print("-" * 60)

    # Tag 0x01: Device (18 bytes)
    payload.append(0x01)
    payload.append(18)
    payload.extend(dev_raw[:18])
    print(f"Offset {len(payload)-20:03d} | [Tag 0x01] 设备描述符 | 长度: 18")

    # Tag 0x02: Config
    payload.append(0x02)
    cfg_len = len(cfg_raw)
    payload.extend(struct.pack('>H', cfg_len)) # 大端序
    payload.extend(cfg_raw)
    print(f"Offset {len(payload)-cfg_len-3:03d} | [Tag 0x02] 配置描述符 | 长度: {cfg_len}")

    # Tag 0x03: HID Reports
    hid_interfaces = hid_raw_all.split(HID_SPLIT)
    for idx, report in enumerate(hid_interfaces):
        if report:
            r_len = len(report)
            payload.append(0x03)
            payload.append(idx)
            payload.extend(struct.pack('>H', r_len))
            payload.extend(report)
            print(f"Offset {len(payload)-r_len-4:03d} | [Tag 0x03] HID接口 {idx} | 长度: {r_len}")

    # Tag 0x04: Strings
    ptr = 0
    s_idx = 0
    while ptr < len(str_blobs):
        s_len = str_blobs[ptr]
        if s_len == 0 or (ptr + s_len) > len(str_blobs): break
        raw_string_data = str_blobs[ptr : ptr + s_len]
        payload.append(0x04)
        payload.append(s_idx)
        payload.append(s_len)
        payload.extend(str_blobs[ptr : ptr + s_len])
        hex_content = " ".join(f"{b:02X}" for b in raw_string_data)
        print(f"Offset {len(payload)-s_len-3:03d} | [Tag 0x04] 索引 {s_idx} | 长度: {s_len:02d} | Hex: {hex_content}")
        print(f"Offset {len(payload)-s_len-3:03d} | [Tag 0x04] 字符串 {s_idx}  | 长度: {s_len}")
        ptr += s_len
        s_idx += 1

    # Footer
    payload.extend([0xEE, 0xFF])
    print("-" * 60)
    print(f"✅ Payload 构建完成，总计: {len(payload)} 字节")
    print("="*60 + "\n")
    return payload

def run():
    parts = extract_raw_parts(INPUT_BIN)
    if not parts: return
    
    final_data = build_and_debug_payload(*parts)

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"📡 正在打开串口 {SERIAL_PORT}...")
        time.sleep(2) # 等待 ESP32 重启或稳定

        # 清除缓冲区旧数据
        ser.reset_input_buffer()

        print("🚀 开始分块发送数据...")
        chunk_size = 64
        for i in range(0, len(final_data), chunk_size):
            chunk = final_data[i:i + chunk_size]
            ser.write(chunk)
            # 给 ESP32 处理中断的时间
            time.sleep(0.05) 
            print(f"   已发送 {min(i+chunk_size, len(final_data))}/{len(final_data)} 字节")
        
        print("\n⏳ 发送完毕，正在监听 ESP32 反馈 (按 Ctrl+C 停止)...")
        while True:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"   [ESP32] {line}")
            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\n🛑 已手动停止")
    except Exception as e:
        print(f"❌ 运行错误: {e}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == "__main__":
    run()
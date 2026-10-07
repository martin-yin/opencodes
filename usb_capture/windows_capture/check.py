import os
import struct

# -------------------------- 核心配置（与采集代码完全一致，必须同步） --------------------------
# 4类描述符专属4字节分隔符（唯一无重复，避免数据冲突）
DEV_DESC_SEP = b'\xAA\x55\x00\x01'   # 设备描述符分隔符
CFG_DESC_SEP = b'\xAA\x55\x00\x02'   # 配置描述符分隔符
STR_DESC_SEP = b'\xAA\x55\x00\x03'   # 字符串描述符分隔符
HID_DESC_SEP = b'\xAA\x55\x00\x04'   # HID报告描述符分隔符（采集时最后一段无此分隔符）
# HID多接口内部分界标记（采集时添加，区分不同接口的HID报告）
HID_INTERFACE_SPLIT = b'\xCC\x33\x00\x01'
# 分隔符名称映射（用于打印标识）
SEP_NAME_MAP = {
    DEV_DESC_SEP: "设备描述符",
    CFG_DESC_SEP: "配置描述符",
    STR_DESC_SEP: "字符串描述符",
    HID_INTERFACE_SPLIT: "HID接口分界标记"
}
# 所有需要提取的分隔符/标记（用于生成ESP32S3代码，按需增删）
ALL_SEPARATORS = {
    "DEV_DESC_SEP": DEV_DESC_SEP,
    "CFG_DESC_SEP": CFG_DESC_SEP,
    "STR_DESC_SEP": STR_DESC_SEP,
    "HID_DESC_SEP": HID_DESC_SEP,
    "HID_INTERFACE_SPLIT": HID_INTERFACE_SPLIT
}
# ---------------------------------------------------------------------------------------------

def hex2str(data: bytes, sep: str = ' ') -> str:
    """字节流转格式化十六进制字符串，方便打印"""
    return sep.join([f'{b:02X}' for b in data])

def parse_device_descriptor(raw_data: bytes) -> dict:
    """解析设备描述符（USB标准格式，固定18字节），返回结构化解析结果"""
    if len(raw_data) < 18:
        return {"错误": f"设备描述符长度异常，需18字节，实际{len(raw_data)}字节"}
    
    try:
        unpacked = struct.unpack_from('<BBHBBBBHHHBBBB', raw_data)
        return {
            "描述符长度": f"{unpacked[0]} 字节",
            "描述符类型": f"{unpacked[1]} (0x01=设备描述符)",
            "USB规范版本": f"{(unpacked[2] >> 8) & 0xFF}.{unpacked[2] & 0xFF}",
            "设备类": f"{unpacked[3]} (0x00=接口级定义，0x03=HID等)",
            "设备子类": f"{unpacked[4]}",
            "设备协议": f"{unpacked[5]}",
            "端点0最大包长": f"{unpacked[6]} 字节",
            "厂商ID(VID)": f"0x{unpacked[7]:04X}",
            "产品ID(PID)": f"0x{unpacked[8]:04X}",
            "设备版本号": f"{(unpacked[9] >> 8) & 0xFF}.{unpacked[9] & 0xFF}",
            "厂商字符串索引": f"{unpacked[10]}",
            "产品字符串索引": f"{unpacked[11]}",
            "序列号字符串索引": f"{unpacked[12]}",
            "配置数": f"{unpacked[13]}",
            "原始十六进制": hex2str(raw_data)
        }
    except Exception as e:
        return {"错误": f"设备描述符解析失败: {str(e)}", "原始十六进制": hex2str(raw_data)}

def parse_string_descriptor(raw_data: bytes) -> list:
    """解析字符串描述符（精准匹配 USB Index 逻辑）"""
    result = []
    offset = 0
    total_len = len(raw_data)
    block_count = 0 
    
    while offset < total_len:
        if offset + 1 >= total_len: break
        desc_len = raw_data[offset]
        if desc_len == 0: 
            offset += 1
            continue
            
        single_desc = raw_data[offset:offset + desc_len]
        desc_type = single_desc[1] if len(single_desc) > 1 else -1

        if block_count == 0:
            # 强制纠正：第一个块永远是 Index 0 (Language ID)
            lang_ids = []
            for i in range(2, len(single_desc), 2):
                lang_id = (single_desc[i+1] << 8) | single_desc[i]
                lang_ids.append(f"0x{lang_id:04X}")
            result.append({
                "索引": 0,
                "类型": "语言ID (LANGID)",
                "内容": lang_ids,
                "原始十六进制": hex2str(single_desc)
            })
        else:
            # 后续块才是真正的字符串
            try:
                content = single_desc[2:].decode('utf-16-le', errors='ignore').strip('\x00')
            except:
                content = "【解码失败】"
            
            result.append({
                "索引": block_count,
                "类型": "字符串描述符",
                "内容": content,
                "原始十六进制": hex2str(single_desc)
            })
        
        offset += desc_len
        block_count += 1
    return result
def parse_hid_report_descriptors(raw_data: bytes) -> list:
    """解析HID报告描述符（支持多接口拆分），返回各接口描述符列表"""
    if not raw_data:
        return []
    hid_parts = raw_data.split(HID_INTERFACE_SPLIT)
    result = []
    for interface_idx, part in enumerate(hid_parts):
        if part:
            result.append({
                "HID接口编号": interface_idx,
                "报告描述符长度": f"{len(part)} 字节",
                "原始十六进制": hex2str(part)
            })
    return result

def parse_config_descriptor(raw_data: bytes) -> dict:
    """解析配置描述符（含完整配置树），返回基础信息+原始数据"""
    if len(raw_data) < 9:
        return {"错误": f"配置描述符头部异常，需至少9字节，实际{len(raw_data)}字节"}
    
    try:
        cfg_len, cfg_type, total_len, intf_count, cfg_idx, cfg_str_idx, attr, max_power = struct.unpack_from('<BBHBBBBB', raw_data)
        return {
            "配置描述符头部长度": f"{cfg_len} 字节",
            "描述符类型": f"{cfg_type} (0x02=配置描述符)",
            "配置树总长度": f"{total_len} 字节",
            "接口数量": f"{intf_count}",
            "配置编号": f"{cfg_idx}",
            "配置字符串索引": f"{cfg_str_idx}",
            "配置属性": f"0x{attr:02X} (0x80=总线供电，0x40=自供电)",
            "最大功耗": f"{max_power * 2} mA (USB标准：1单位=2mA)",
            "接口/端点描述符原始数据": hex2str(raw_data[9:]),
            "完整配置树原始十六进制": hex2str(raw_data)
        }
    except Exception as e:
        return {"错误": f"配置描述符解析失败: {str(e)}", "原始十六进制": hex2str(raw_data)}

def split_bin_by_separators(bin_data: bytes) -> dict:
    """核心拆分逻辑：按专属分隔符拆分二进制数据，区分各类描述符"""
    split_result = {}
    remaining_data = bin_data
    
    for sep, desc_name in [(DEV_DESC_SEP, "设备描述符"), (CFG_DESC_SEP, "配置描述符"), (STR_DESC_SEP, "字符串描述符")]:
        if sep in remaining_data:
            desc_data, remaining_data = remaining_data.split(sep, 1)
            split_result[desc_name] = desc_data.strip(b'\x00')
        else:
            split_result[desc_name] = b''
    
    split_result["HID报告描述符"] = remaining_data.strip(b'\x00')
    return split_result

# -------------------------- 新增核心功能：生成ESP32S3可粘贴的分隔符C代码 --------------------------
def generate_esp32_sep_code() -> str:
    """
    生成ESP32S3可直接粘贴的分隔符C语言数组代码
    返回：格式化的C代码字符串，可直接复制到ESP32S3控制台/代码文件
    """
    code_lines = []
    code_lines.append("/* ========== USB描述符分隔符/标记 - ESP32S3可直接粘贴 ========== */")
    code_lines.append("// 说明：每个分隔符为4字节数组，与Python采集/解析端严格同步")
    code_lines.append("")
    
    for sep_name, sep_bytes in ALL_SEPARATORS.items():
        # 转换为C语言十六进制数组（0xAA, 0x55, 0x00, 0x01 格式）
        hex_parts = [f"0x{b:02X}" for b in sep_bytes]
        hex_str = ", ".join(hex_parts)
        # 拼接C语言数组定义（适配ESP32S3的C环境）
        code_lines.append(f"const uint8_t {sep_name}[] = {{{hex_str}}};")
        code_lines.append(f"#define {sep_name}_LEN {len(sep_bytes)}  // 分隔符长度（字节）")
        code_lines.append("")
    
    code_lines.append("/* ============================================================== */")
    # 拼接为完整代码字符串（换行符适配控制台）
    return "\n".join(code_lines)

def parse_usb_descriptor_bin(bin_file_path: str, output_txt: bool = True) -> None:
    """
    主解析函数：读取bin文件 → 拆分描述符 → 解析 → 提取ESP32S3分隔符代码 → 打印/生成报告
    :param bin_file_path: 采集生成的bin文件路径（绝对/相对）
    :param output_txt: 是否生成TXT格式解析报告（同目录，与bin文件同名）
    """
    # 1. 校验文件是否存在
    if not os.path.exists(bin_file_path):
        print(f"❌ 错误：文件 {bin_file_path} 不存在！")
        return
    
    # 2. 读取二进制文件
    try:
        with open(bin_file_path, 'rb') as f:
            bin_data = f.read()
        print(f"✅ 成功读取二进制文件：{bin_file_path}")
        print(f"📏 文件总大小：{len(bin_data)} 字节\n")
    except Exception as e:
        print(f"❌ 读取文件失败：{str(e)}")
        return
    
    # 3. 生成ESP32S3分隔符代码（核心新增步骤）
    esp32_sep_code = generate_esp32_sep_code()
    # 优先打印分隔符代码（方便快速复制）
    print("🔧 [ESP32S3 可直接粘贴的分隔符C代码] ".center(80, "*"))
    print(esp32_sep_code)
    print("*" * 80 + "\n")
    
    # 4. 按分隔符拆分各类描述符
    print("🔪 正在按专属分隔符拆分各类描述符...")
    desc_data_map = split_bin_by_separators(bin_data)
    for desc_name, data in desc_data_map.items():
        status = f"✅ 找到（{len(data)} 字节）" if data else "❌ 未采集到"
        print(f"  - {desc_name.ljust(12)}: {status}")
    print("-" * 80 + "\n")
    
    # 5. 逐个解析各类描述符
    parse_result = {}
    parse_result["设备描述符"] = parse_device_descriptor(desc_data_map["设备描述符"])
    parse_result["配置描述符"] = parse_config_descriptor(desc_data_map["配置描述符"])
    parse_result["字符串描述符"] = parse_string_descriptor(desc_data_map["字符串描述符"])
    parse_result["HID报告描述符"] = parse_hid_report_descriptors(desc_data_map["HID报告描述符"])
    
    # 6. 格式化打印解析结果
    print("📋 USB描述符解析结果（按标准格式）\n")
    # 设备描述符
    print("=" * 60 + " 【设备描述符（USB标准，固定18字节）】 " + "=" * 60)
    for k, v in parse_result["设备描述符"].items():
        print(f"{k.ljust(15)}: {v}")
    # 配置描述符
    print("\n" + "=" * 60 + " 【配置描述符（完整配置树）】 " + "=" * 60)
    for k, v in parse_result["配置描述符"].items():
        print(f"{k.ljust(25)}: {v}")
    # 字符串描述符
    print("\n" + "=" * 60 + " 【字符串描述符（含语言ID+设备信息）】 " + "=" * 60)
    if parse_result["字符串描述符"]:
        for desc in parse_result["字符串描述符"]:
            print(f"\n索引 {desc['索引']:>2} | {desc['类型']}")
            for k, v in desc.items():
                if k not in ["索引", "类型"]:
                    print(f"  {k.ljust(10)}: {v}")
    else:
        print("  ❌ 未采集到有效字符串描述符")
    # HID报告描述符
    print("\n" + "=" * 60 + " 【HID报告描述符（按接口拆分）】 " + "=" * 60)
    if parse_result["HID报告描述符"]:
        for hid_desc in parse_result["HID报告描述符"]:
            print(f"\n{HID_INTERFACE_SPLIT.hex().upper()} 分隔 → 接口 {hid_desc['HID接口编号']}")
            for k, v in hid_desc.items():
                print(f"  {k.ljust(15)}: {v}")
    else:
        print("  ❌ 未采集到有效HID报告描述符（设备非HID类）")
    
    # 7. 可选生成TXT解析报告（包含分隔符代码）
    if output_txt:
        txt_file_path = os.path.splitext(bin_file_path)[0] + "_解析报告_含ESP32代码.txt"
        try:
            with open(txt_file_path, 'w', encoding='utf-8') as f:
                # 先写入ESP32S3分隔符代码（报告首段，方便查找）
                f.write("=" * 120 + "\n")
                f.write("【ESP32S3 可直接粘贴的USB描述符分隔符C代码】\n")
                f.write("=" * 120 + "\n")
                f.write(esp32_sep_code + "\n\n")
                
                # 再写入设备描述符解析结果
                f.write("=" * 120 + "\n")
                f.write("【设备描述符（USB标准，固定18字节）】\n")
                f.write("=" * 120 + "\n")
                for k, v in parse_result["设备描述符"].items():
                    f.write(f"{k.ljust(15)}: {v}\n")
                
                # 配置描述符
                f.write("\n" + "=" * 120 + "\n")
                f.write("【配置描述符（完整配置树）】\n")
                f.write("=" * 120 + "\n")
                for k, v in parse_result["配置描述符"].items():
                    f.write(f"{k.ljust(25)}: {v}\n")
                
                # 字符串描述符
                f.write("\n" + "=" * 120 + "\n")
                f.write("【字符串描述符（含语言ID+设备信息）】\n")
                f.write("=" * 120 + "\n")
                if parse_result["字符串描述符"]:
                    for desc in parse_result["字符串描述符"]:
                        f.write(f"\n索引 {desc['索引']:>2} | {desc['类型']}\n")
                        for k, v in desc.items():
                            if k not in ["索引", "类型"]:
                                f.write(f"  {k.ljust(10)}: {v}\n")
                else:
                    f.write("  ❌ 未采集到有效字符串描述符\n")
                
                # HID报告描述符
                f.write("\n" + "=" * 120 + "\n")
                f.write("【HID报告描述符（按接口拆分）】\n")
                f.write("=" * 120 + "\n")
                if parse_result["HID报告描述符"]:
                    for hid_desc in parse_result["HID报告描述符"]:
                        f.write(f"\n{HID_INTERFACE_SPLIT.hex().upper()} 分隔 → 接口 {hid_desc['HID接口编号']}\n")
                        for k, v in hid_desc.items():
                            f.write(f"  {k.ljust(15)}: {v}\n")
                else:
                    f.write("  ❌ 未采集到有效HID报告描述符（设备非HID类）\n")
            
            print(f"\n📄 解析报告（含ESP32S3代码）已生成：{txt_file_path}")
        except Exception as e:
            print(f"\n❌ 生成解析报告失败：{str(e)}")

# -------------------------- 运行入口 --------------------------
if __name__ == "__main__":
    # 请替换为你的采集生成的bin文件路径（相对/绝对均可）
    TARGET_BIN_FILE = "usb_descriptors.bin"
    
    # 执行解析（默认生成TXT报告，设置output_txt=False可关闭）
    parse_usb_descriptor_bin(
        bin_file_path=TARGET_BIN_FILE,
        output_txt=True
    )


import ctypes
import os
import subprocess
import time
import sys
import usb.core
import usb.util
import logging
from utils import DEV_DESC_SEP, CFG_DESC_SEP, STR_DESC_SEP, HID_INTERFACE_SPLIT

logger = logging.getLogger("USB_Engine")
logger.propagate = True


# --- Libwdi 结构定义 ---
class WdiOptionsCreateList(ctypes.Structure):
    _pack_ = 8
    _fields_ = [("list_all", ctypes.c_bool), ("list_hubs", ctypes.c_bool), ("trim_whitelist", ctypes.c_bool)]

class WdiDeviceInfo(ctypes.Structure):
    _pack_ = 8
    pass

WdiDeviceInfo._fields_ = [
    ("next", ctypes.POINTER(WdiDeviceInfo)),
    ("vid", ctypes.c_ushort), ("pid", ctypes.c_ushort),
    ("is_composite", ctypes.c_bool), ("mi", ctypes.c_int),
    ("desc", ctypes.c_char_p), ("driver", ctypes.c_char_p),
    ("device_id", ctypes.c_char_p), ("hardware_id", ctypes.c_char_p),
    ("compatible_id", ctypes.c_char_p), ("upper_filter", ctypes.c_char_p),
    ("driver_version", ctypes.c_uint64),
]

WDI_SIMPLE_EXE = os.path.abspath("./wdi-simple.exe")
LIBWDI_DLL = os.path.abspath("./libwdi.dll")

class UsbEngin:
    def __init__(self):
        current_dir = os.getcwd()
        os.environ['PATH'] = current_dir + os.pathsep + os.environ['PATH']
        if hasattr(os, 'add_dll_directory'):
            try: os.add_dll_directory(current_dir)
            except: pass

    def get_usb_device_list(self):
        """交互式 USB 设备扫描与选择"""
        devices = list(usb.core.find(find_all=True))
        if not devices:
            return None, None
        dev_pool = []
        for idx, dev in enumerate(devices):
            try:
                mfg = usb.util.get_string(dev, dev.iManufacturer) or "N/A"
                prod = usb.util.get_string(dev, dev.iProduct) or "N/A"
            except:
                mfg, prod = "<System Locked>", "<Need Hijack>"
            dev_pool.append({
                "name": mfg,
                "product": prod,
                "vid": f"0x{dev.idVendor:04X}",
                "pid": f"0x{dev.idProduct:04X}"
            })
        return dev_pool

    def get_interface_status(self, vid, pid):
        """检测指定 VID/PID 的接口驱动状态"""
        if not os.path.exists(LIBWDI_DLL): return {}
        try:
            lib = ctypes.CDLL(LIBWDI_DLL)
            device_list = ctypes.POINTER(WdiDeviceInfo)()
            options = WdiOptionsCreateList(True, False, False)
            status_map = {}
            if lib.wdi_create_list(ctypes.byref(device_list), ctypes.byref(options)) == 0:
                curr = device_list
                while curr:
                    dev = curr.contents
                    if dev.vid == vid and dev.pid == pid:
                        driver_name = (dev.driver or b"").decode('utf-8', errors='ignore').lower()
                        status_map[dev.mi] = any(x in driver_name for x in ["libusb", "winusb", "usbtest"])
                    curr = dev.next
                lib.wdi_destroy_list(device_list)
            return status_map
        except: return {}

    def hijack_interface(self, vid, pid, mi):
        """通过 wdi-simple 强制切换驱动"""
        logger.info(f"⚙️ 正在 MI_{mi:02d} 安装驱动...")
        inf_name = f"audit_{vid:04x}_{pid:04x}_mi{mi}.inf"
        # -t 0 表示安装 WinUSB 驱动
        cmd = [WDI_SIMPLE_EXE, "-v", f"0x{vid:04X}", "-p", f"0x{pid:04X}", "-i", str(mi), "-t", "0", "-f", inf_name, "-s"]
        try:
            res = subprocess.run(cmd, capture_output=True, text=True)
            return "Success" in res.stdout or res.returncode == 0
        except: return False

    def restore_all(self):
        logger.info("♻️ 正在搜索并清理驱动...")
        try:
            output = subprocess.check_output(["pnputil", "/enum-drivers"], text=True)
            target_oems = set()
            lines = output.split('\n')
            for i, line in enumerate(lines):
                # 匹配包含 audit_ 前缀的 inf 或常见的驱动名
                if "audit_" in line or "libusb-win32" in line.lower():
                    for j in range(i, i-10, -1):
                        if "oem" in lines[j] and ".inf" in lines[j]:
                            oem_name = lines[j].split(':')[-1].strip()
                            target_oems.add(oem_name)
                            break
            for oem in target_oems:
                logger.info(f"♻️ 正在移除驱动包: {oem}")
                subprocess.run(["pnputil", "/delete-driver", oem, "/uninstall", "/force"], capture_output=True)
            subprocess.run(["pnputil", "/scan-devices"], capture_output=True)
            logger.info("✅ 系统驱动已尝试还原。")
        except Exception as e: logger.info(f"❌ 还原失败: {e}")

    def save_usb_hid(self, vid, pid, save_filename="usb_descriptors.bin"):
            script_dir = os.path.dirname(__file__)
            save_path = os.path.join(script_dir, save_filename)
            logger.info(f"📡 正在连接设备 [0x{vid:04X}:0x{pid:04X}]...")
            
            dev = usb.core.find(idVendor=vid, idProduct=pid)
            if dev is None:
                logger.info("❌ 无法建立 USB 会话，请确保驱动劫持已生效。")
                return

            bin_data_list = []

            # --- 1. 设备描述符 ---
            dev_desc = b''
            try:
                dev_desc = bytes(dev.ctrl_transfer(0x80, 0x06, 0x0100, 0, 18))
                logger.info(f"1. Device Descriptor Captured.")
            except Exception as e: logger.info(f"⚠️ 设备描述符读取失败: {e}")
            
            if dev_desc:
                bin_data_list.append(dev_desc)
                bin_data_list.append(DEV_DESC_SEP)

            # --- 2. 配置描述符 ---
            cfg_desc = b''
            try:
                initial_cfg = dev.ctrl_transfer(0x80, 0x06, 0x0200, 0, 9)
                total_len = initial_cfg[2] + (initial_cfg[3] << 8)
                cfg_desc = bytes(dev.ctrl_transfer(0x80, 0x06, 0x0200, 0, total_len))
                logger.info(f"2. Config Descriptor Captured (Len: {total_len}).")
            except Exception as e: logger.info(f"⚠️ 配置描述符读取失败: {e}")

            if cfg_desc:
                bin_data_list.append(cfg_desc)
                bin_data_list.append(CFG_DESC_SEP)

            # --- 3. 字符串描述符增强版 ---
            logger.info(f"3. Scanning String Descriptors...")
            str_bin_accumulator = b''
            
            # 1. 首先尝试获取 Index 0 (语言 ID 列表)
            try:
                # Index 0 必须请求，它是所有字符串的基石
                raw_lang = dev.ctrl_transfer(0x80, 0x06, 0x0300, 0, 255, timeout=500)
                if raw_lang:
                    lang_bytes = bytes(raw_lang)
                    str_bin_accumulator += lang_bytes
                    logger.info(f"   [Index 0] LANGID List: {lang_bytes.hex().upper()}")
                    # 提取第一个可用的语言 ID 用于后续请求 (通常是 0x0409)
                    actual_lang = (lang_bytes[3] << 8 | lang_bytes[2]) if len(lang_bytes) >= 4 else 0x0409
                else:
                    actual_lang = 0x0409
            except Exception as e:
                logger.info(f"⚠️ 无法获取语言 ID 列表: {e}")
                actual_lang = 0x0409

            # 2. 定义要扫描的索引范围 (1-10 已经能覆盖你的 U127.03 了)
            search_range = range(1, 11) 

            for idx in search_range:
                try:
                    # 使用刚才获取的 actual_lang 请求具体的字符串
                    raw_s = dev.ctrl_transfer(0x80, 0x06, (0x03 << 8) | idx, actual_lang, 255, timeout=500)
                    if raw_s:
                        s_bytes = bytes(raw_s)
                        str_bin_accumulator += s_bytes # 拼接到数据池
                        
                        # 打印出来让你确认
                        try:
                            content = s_bytes[2:].decode('utf-16-le')
                            logger.info(f"   [Index {idx}] {content}")
                        except:
                            logger.info(f"   [Index {idx}] (Hex): {s_bytes.hex().upper()}")
                except:
                    continue
            
            # 3. 加上专属分隔符存入列表
            if str_bin_accumulator:
                bin_data_list.append(str_bin_accumulator)
                bin_data_list.append(STR_DESC_SEP)

            # --- 4. HID 报告描述符 ---
            logger.info(f"4. Capturing HID Report Descriptors...")
            hid_bin_accumulator = b''
          
            valid_hid_count = 0

            try:
                cfg = dev.get_active_configuration()
                for i in range(cfg.bNumInterfaces):
                    intf = cfg[(i, 0)]
                    if intf.bInterfaceClass == 0x03: # HID Class
                        try:
                            raw_report = dev.ctrl_transfer(0x81, 0x06, 0x2200, i, 4096, timeout=1000)
                            if valid_hid_count > 0:
                                hid_bin_accumulator += HID_INTERFACE_SPLIT
                            hid_bin_accumulator += bytes(raw_report)
                            valid_hid_count += 1
                            logger.info(f"   [MI_{i:02d}] Report Desc Captured.")
                        except: logger.info(f"   [MI_{i:02d}] No Response.")
            except: pass

            if hid_bin_accumulator:
                bin_data_list.append(hid_bin_accumulator)
                # 按照你的规则，最后一段 HID 数据后不加分隔符

            # --- 最终写入 ---
            try:
                final_data = b''.join(bin_data_list)
                with open(save_path, 'wb') as f:
                    f.write(final_data)
                logger.info(f"✅ 抓取完成！总大小: {len(final_data)} 字节")
            except Exception as e:
                logger.info(f"❌ 写入文件失败: {e}")
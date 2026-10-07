import customtkinter as ctk
import serial.tools.list_ports
from datetime import datetime
from usb_engine import UsbEngin
import threading  
import logging
from com_burn import COMBurn
ctk.set_appearance_mode("light")

def setup_logging():
    formatter = logging.Formatter('%(asctime)s [%(levelname)s] %(name)s: %(message)s', datefmt='%H:%M:%S')
    
    console_handler = logging.StreamHandler()
    console_handler.setFormatter(formatter)
    
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.INFO)
    root_logger.addHandler(console_handler)
    
    logging.getLogger("USB_Engine").setLevel(logging.INFO)
    logging.getLogger("COM_Burn").setLevel(logging.INFO)  # 修正原拼写错误 COM_burn -> COM_Burn


logger = logging.getLogger("USB_DEVICE_INFO_CAPTURE")
logger = logging.getLogger("USB_DEVICE_INFO_CAPTURE")
setup_logging()

def enable_high_dpi():
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass

enable_high_dpi()


class USBCaptureGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("USB 设备抓取与烧录工具")
        self.root.geometry("750x600")
        self.is_captured = False
        self.root.configure(fg_color="#f0f2f5")
        self.main_frame = ctk.CTkFrame(root, corner_radius=15, fg_color="transparent")
        self.main_frame.pack(fill=ctk.BOTH, expand=True, padx=25, pady=20)
        
        self.create_widgets()
        self.usb_engin = UsbEngin() 
        self.com_burn = COMBurn()
        import logging

        class LogForwarder(logging.Handler):
            def __init__(self, print_func):
                super().__init__()
                self.print_func = print_func
            def emit(self, record):
                msg = record.getMessage() 
                root.after(0, self.print_func, f"{msg}")
        engine_logger = logging.getLogger("USB_Engine")
        burn_logger = logging.getLogger("COM_Burn")

        forwarder = LogForwarder(self.print_log)
        engine_logger.addHandler(forwarder)
        burn_logger.addHandler(forwarder)
        self.device_map = []     
        self.refresh_all()          

    def create_widgets(self):
        top_bar = ctk.CTkFrame(self.main_frame, fg_color="transparent")
        top_bar.pack(fill="x", padx=5, pady=(0, 15))
        
        title_label = ctk.CTkLabel(top_bar, text="🚀 控制面板", font=("Microsoft YaHei", 18, "bold"), text_color="#1a1a1a")
        title_label.pack(side="left")

        self.btn_cleanup = ctk.CTkButton(
            top_bar, text="🧹 清理驱动", width=100, height=32, 
            fg_color="#fa8c16", hover_color="#ffa940",
            text_color="white", font=("Microsoft YaHei", 12, "bold"),
            corner_radius=8, command=self.run_restore_driver
        )
        self.btn_cleanup.pack(side="right", padx=5)
        
        self.btn_reload = ctk.CTkButton(
            top_bar, text="🔄 刷新设备", width=110, height=32, 
            fg_color="#52c41a",      
            hover_color="#73d13d", 
            text_color="white",
            font=("Microsoft YaHei", 12, "bold"),
            corner_radius=8,
            command=self.refresh_all
        )
        self.btn_reload.pack(side="right", padx=5)

        cap_frame = ctk.CTkFrame(self.main_frame, corner_radius=12, fg_color="#ffffff", border_width=1, border_color="#e5e7eb")
        cap_frame.pack(fill="x", pady=8)
        
        inner_cap = ctk.CTkFrame(cap_frame, fg_color="transparent")
        inner_cap.pack(fill="x", padx=20, pady=15)

        self.usb_select = ctk.CTkComboBox(
            inner_cap, values=[], 
            width=350, height=40, state="readonly",
            fg_color="#f8f9fa", border_color="#d1d5db", button_color="#f1f5f9"
        )
        self.usb_select.set("请选择目标设备")
        self.usb_select.pack(side="left", fill="x", expand=True, padx=(0, 15))
        
        self.btn_capture = ctk.CTkButton(
            inner_cap, text="抓取", width=120, height=40, 
            fg_color="#1890ff", hover_color="#40a9ff", 
            font=("Microsoft YaHei", 13, "bold"), corner_radius=8, command=self.run_capture
        )
        self.btn_capture.pack(side="right")

        burn_frame = ctk.CTkFrame(self.main_frame, corner_radius=12, fg_color="#ffffff", border_width=1, border_color="#e5e7eb")
        burn_frame.pack(fill="x", pady=8)
        
        inner_burn = ctk.CTkFrame(burn_frame, fg_color="transparent")
        inner_burn.pack(fill="x", padx=20, pady=15)

        self.port_select = ctk.CTkComboBox(
            inner_burn, width=350, height=40, state="readonly",
            fg_color="#f8f9fa", border_color="#d1d5db", button_color="#f1f5f9"
        )
        self.port_select.pack(side="left", fill="x", expand=True, padx=(0, 15))
        
        self.btn_burn = ctk.CTkButton(
            inner_burn, text="烧录", width=120, height=40, 
            fg_color="#722ed1", hover_color="#9254de", 
            font=("Microsoft YaHei", 13, "bold"), corner_radius=8, command=self.run_burn
        )
        self.btn_burn.pack(side="right")

        log_title = ctk.CTkLabel(self.main_frame, text="TERMINAL LOGS", font=("Consolas", 11, "bold"), text_color="#8c8c8c")
        log_title.pack(anchor="w", padx=10, pady=(10, 0))

        self.log_area = ctk.CTkTextbox(
            self.main_frame, corner_radius=12, font=("Consolas", 12),
            fg_color="#141414", text_color="#52c41a",
            border_width=2, border_color="#303030"
        )
        self.log_area.pack(fill="both", expand=True, pady=(5, 0))
        self.log_area.configure(state="disabled")

    def refresh_all(self):
        """同时刷新串口和 USB 设备"""
        self.clear_log()
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_select.configure(values=ports if ports else ["未检测到串口"])
        if ports: self.port_select.set(ports[0])

        try:
            self.device_map = self.usb_engin.get_usb_device_list()
            if self.device_map:
                display_list = [f"{d['product']}-{d['vid']}:{d['pid']}" for d in self.device_map]
                self.usb_select.configure(values=display_list)
                self.usb_select.set(display_list[0])
                self.print_log(f"✨ 发现 {len(display_list)} 个 USB 设备")
            else:
                self.usb_select.configure(values=["未检测到设备"])
                self.usb_select.set("未检测到设备")
        except Exception as e:
            self.print_log(f"❌ USB 扫描出错: {str(e)}")

    def run_restore_driver(self):
        """异步执行驱动还原"""
        self.print_log("🛡 准备执行系统驱动清理...")
        cleanup_thread = threading.Thread(target=self._restore_worker, daemon=True)
        cleanup_thread.start()

    def _restore_worker(self):
        try:
            self.set_buttons_state("disabled")
            self.usb_engin.restore_all()
            self.print_log("✅ 驱动清理与系统扫描任务已完成")
        except Exception as e:
            self.print_log(f"❌ 清理驱动时发生异常: {str(e)}")
        finally:
            self.set_buttons_state("normal")
            self.root.after(500, self.refresh_all)

    def print_log(self, message):
        """同步打印到 GUI 和控制台"""
        self.log_area.configure(state="normal")
        ts = datetime.now().strftime("%H:%M:%S")
        self.log_area.insert("end", f"[{ts}] {message}\n")
        self.log_area.see("end")
        self.log_area.configure(state="disabled")
        logger.info(message)

    def clear_log(self):
        self.log_area.configure(state="normal")
        self.log_area.delete("1.0", "end")
        self.log_area.configure(state="disabled")

    def run_capture(self):
        """点击开始抓取按钮的回调"""
        selected = self.usb_select.get()
        if selected in ["请选择目标设备", "未检测到设备"]:
            self.print_log("⚠️ 请先选择一个有效的 USB 设备")
            return
        try:
            raw_ids = selected.split("-")[-1] 
            vid_str, pid_str = raw_ids.split(":")
            vid = int(vid_str, 16)
            pid = int(pid_str, 16)
        except Exception as e:
            self.print_log(f"❌ 解析设备 ID 失败: {e}")
            return

        self.set_buttons_state("disabled")
        task = threading.Thread(target=self._capture_thread_proc, args=(vid, pid), daemon=True)
        task.start()

    def set_buttons_state(self, state):
        """统一设置所有功能按钮的状态 ('normal' 或 'disabled')"""
        self.btn_capture.configure(state=state)
        self.btn_burn.configure(state=state)
        self.btn_cleanup.configure(state=state)
        self.btn_reload.configure(state=state)

    def _capture_thread_proc(self, vid, pid):
        """在子线程中运行的 UsbEngin 逻辑"""
        try:
            self.print_log(f"🚀 开始执行异步抓取任务 (VID:0x{vid:04X})")
            status = self.usb_engin.get_interface_status(vid, pid)
            
            if not status:
                self.print_log("⚠️ 未发现可操作的接口")
                return

            self.print_log(f"📊 发现 {len(status)} 个端口，正在检查驱动...")
            
            all_success = True
            need_cleanup = False

            for mi, is_winusb in status.items():
                if not is_winusb:
                    need_cleanup = True
                    self.print_log(f"⚙️ 端口 MI_{mi:02d} 驱动安装中，请耐心等待...")
                    
                    if self.usb_engin.hijack_interface(vid, pid, mi):
                        self.print_log(f"✅ 端口 MI_{mi:02d} 驱动安装成功")
                    else:
                        self.print_log(f"❌ 端口 MI_{mi:02d} 驱动安装失败")
                        all_success = False
                        break

            if all_success:
                self.print_log("⏳ 等待系统重新枚举设备 (3秒)...")
                import time
                time.sleep(3)
                self.usb_engin.save_usb_hid(vid, pid)
                self.is_captured = True
                self.print_log("🎉 抓取流程全部完成！")
            else:
                self.print_log("🛑 任务因部分端口安装失败而中止")

            if need_cleanup:
                self.print_log("♻️ 正在清理临时驱动并还原系统状态...")
                self.usb_engin.restore_all()

        except Exception as e:
            self.print_log(f"💥 线程内发生异常: {str(e)}")
            logger.error("Capture Thread Error", exc_info=True)
            try: self.usb_engin.restore_all()
            except: pass
        finally:
            self.set_buttons_state("normal")
    

    def _burn_thread_proc(self, port):
        try:
            self.com_burn.burn(port)
        except Exception as e:
            self.print_log(f"💥 线程内发生异常: {str(e)}")
            logger.error("Burn Thread Error", exc_info=True)
        finally:
            self.set_buttons_state("normal")
    
    def run_burn(self):
        """点击开始抓取按钮的回调"""
        selected = self.port_select.get()
        if selected in ["请选择目标设备", "未检测到设备"]:
            self.print_log("⚠️ 请先选择一个有效的 USB 串口")
            return
        self.set_buttons_state("disabled")
        task = threading.Thread(target=self._burn_thread_proc, args=(selected,), daemon=True)
        task.start()

if __name__ == "__main__":
    root = ctk.CTk()
    app = USBCaptureGUI(root)
    root.resizable(False, False)
    root.mainloop()
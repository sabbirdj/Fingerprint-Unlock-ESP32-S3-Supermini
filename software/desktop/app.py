#!/usr/bin/env python3
"""
Viper's Biometric Key Desktop Management Application
Cross-platform desktop manager for ZW111 + ESP32-S3 Biometric Key.
Supports Windows, Linux, and macOS.
"""

import sys
import os
import platform
import threading
import time
import socket
import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

# Windows Lock/Unlock Screen Monitor
if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    LRESULT = ctypes.c_int64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_long
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)

    user32 = ctypes.windll.user32
    wtsapi32 = ctypes.windll.wtsapi32

    user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.DefWindowProcW.restype = LRESULT

    class WNDCLASSW(ctypes.Structure):
        _fields_ = [
            ('style', wintypes.UINT),
            ('lpfnWndProc', WNDPROC),
            ('cbClsExtra', ctypes.c_int),
            ('cbWndExtra', ctypes.c_int),
            ('hInstance', wintypes.HINSTANCE),
            ('hIcon', wintypes.HICON),
            ('hCursor', wintypes.HCURSOR),
            ('hbrBackground', wintypes.HBRUSH),
            ('lpszMenuName', wintypes.LPCWSTR),
            ('lpszClassName', wintypes.LPCWSTR)
        ]

    class WindowsSessionMonitor(threading.Thread):
        def __init__(self, on_lock, on_unlock):
            super().__init__(daemon=True)
            self.on_lock = on_lock
            self.on_unlock = on_unlock
            self.hwnd = None

        def run(self):
            def py_wndproc(hwnd, msg, wparam, lparam):
                if msg == 0x02B1: # WM_WTSSESSION_CHANGE
                    if wparam == 0x7: # WTS_SESSION_LOCK
                        self.on_lock()
                    elif wparam == 0x8: # WTS_SESSION_UNLOCK
                        self.on_unlock()
                return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

            self.wndproc = WNDPROC(py_wndproc)
            wc = WNDCLASSW()
            wc.lpfnWndProc = self.wndproc
            wc.lpszClassName = 'ViperSessionMonitorClass'
            atom = user32.RegisterClassW(ctypes.byref(wc))
            self.hwnd = user32.CreateWindowExW(0, atom, 'ViperSessionMonitor', 0, 0, 0, 0, 0, 0, 0, 0, 0)
            wtsapi32.WTSRegisterSessionNotification(self.hwnd, 0)

            msg = wintypes.MSG()
            while user32.GetMessageW(ctypes.byref(msg), 0, 0, 0) > 0:
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))

class ViperDesktopApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Viper's Biometric Key Manager")
        self.root.geometry("640x620")
        self.root.minsize(550, 500)

        # Operating System Detection
        self.detected_os = platform.system() # 'Windows', 'Linux', 'Darwin' (macOS)
        self.os_code = "WIN" if self.detected_os == "Windows" else ("MAC" if self.detected_os == "Darwin" else "LIN")

        # Serial State
        self.ser = None
        self.connected = False
        self.rx_thread = None
        self.stop_event = threading.Event()
        self.svc_socket = None

        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

        self._setup_theme()
        self._build_ui()

        # Auto-refresh ports on startup
        self.refresh_ports()

        # Windows Session Lock/Unlock Monitor
        if sys.platform == "win32":
            self.lock_state = False
            self.session_monitor = WindowsSessionMonitor(
                on_lock=self._on_windows_locked,
                on_unlock=self._on_windows_unlocked
            )
            self.session_monitor.start()
            self._heartbeat_sync()

    def _setup_theme(self):
        self.bg_color = "#18181b"
        self.card_bg = "#27272a"
        self.text_color = "#f4f4f5"
        self.accent_color = "#10b981"
        self.accent_hover = "#059669"
        self.border_color = "#3f3f46"

        self.root.configure(bg=self.bg_color)
        style = ttk.Style()
        style.theme_use("clam")

        style.configure(".", background=self.bg_color, foreground=self.text_color)
        style.configure("TLabel", background=self.card_bg, foreground=self.text_color, font=("Segoe UI", 10))
        style.configure("Header.TLabel", background=self.bg_color, foreground=self.text_color, font=("Segoe UI", 14, "bold"))
        style.configure("Status.TLabel", background=self.card_bg, foreground=self.accent_color, font=("Segoe UI", 10, "bold"))
        style.configure("TFrame", background=self.bg_color)
        style.configure("Card.TFrame", background=self.card_bg, relief="flat")
        style.configure("TNotebook", background=self.bg_color, tabmargins=[2, 5, 2, 0])
        style.configure("TNotebook.Tab", background=self.card_bg, foreground=self.text_color, padding=[12, 6], font=("Segoe UI", 10))
        style.map("TNotebook.Tab", background=[("selected", self.accent_color)], foreground=[("selected", "#ffffff")])

        style.configure("Primary.TButton", background=self.accent_color, foreground="#ffffff", font=("Segoe UI", 10, "bold"), borderwidth=0, padding=[10, 5])
        style.map("Primary.TButton", background=[("active", self.accent_hover)])
        style.configure("Secondary.TButton", background=self.border_color, foreground="#ffffff", font=("Segoe UI", 10), borderwidth=0, padding=[8, 4])

    def _build_ui(self):
        # Header banner
        header_frame = ttk.Frame(self.root, style="TFrame")
        header_frame.pack(fill="x", padx=20, pady=(15, 10))

        title_lbl = ttk.Label(header_frame, text="Viper's Biometric Key Manager", style="Header.TLabel")
        title_lbl.pack(side="left")

        os_display = "Windows" if self.os_code == "WIN" else ("macOS" if self.os_code == "MAC" else "Linux")
        os_badge = tk.Label(header_frame, text=f"Active Host: {os_display}", bg="#3b82f6", fg="#ffffff",
                            font=("Segoe UI", 9, "bold"), padx=8, pady=3)
        os_badge.pack(side="right")

        # Connection Card
        conn_card = ttk.Frame(self.root, style="Card.TFrame")
        conn_card.pack(fill="x", padx=20, pady=5)

        ttk.Label(conn_card, text="Device Connection (USB / Virtual Serial):", font=("Segoe UI", 10, "bold")).grid(row=0, column=0, columnspan=4, sticky="w", padx=12, pady=(10, 5))

        ttk.Label(conn_card, text="Port:").grid(row=1, column=0, sticky="w", padx=(12, 5), pady=8)
        self.port_combo = ttk.Combobox(conn_card, width=22, state="readonly")
        self.port_combo.grid(row=1, column=1, padx=5, pady=8)

        self.btn_refresh = ttk.Button(conn_card, text="Refresh", style="Secondary.TButton", command=self.refresh_ports)
        self.btn_refresh.grid(row=1, column=2, padx=5, pady=8)

        self.btn_connect = ttk.Button(conn_card, text="Connect", style="Primary.TButton", command=self.toggle_connection)
        self.btn_connect.grid(row=1, column=3, padx=10, pady=8)

        self.status_lbl = ttk.Label(conn_card, text="Status: Disconnected", style="Status.TLabel")
        self.status_lbl.grid(row=2, column=0, columnspan=2, sticky="w", padx=12, pady=(0, 10))

        self.security_warning_lbl = tk.Label(conn_card, text="", bg="#18181b", fg="#ef4444", font=("Segoe UI", 9, "bold"))
        self.security_warning_lbl.grid(row=2, column=2, columnspan=2, sticky="e", padx=12, pady=(0, 10))

        # Notebook tabs
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=20, pady=10)

        self.tab_vault = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_enroll = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_log = ttk.Frame(self.notebook, style="Card.TFrame")

        self.notebook.add(self.tab_vault, text="Password Vault")
        self.notebook.add(self.tab_enroll, text="Fingerprint Enrollment")
        self.notebook.add(self.tab_log, text="Device Log")

        self._build_vault_tab()
        self._build_enroll_tab()
        self._build_log_tab()

    def _build_vault_tab(self):
        frame = self.tab_vault
        ttk.Label(frame, text="Store credentials directly on ESP32-S3 Flash memory.",
                  font=("Segoe UI", 9, "italic")).pack(anchor="w", padx=15, pady=(12, 10))

        grid_frame = ttk.Frame(frame, style="Card.TFrame")
        grid_frame.pack(fill="x", padx=15, pady=5)

        # Windows
        ttk.Label(grid_frame, text="Windows Password:").grid(row=0, column=0, sticky="w", pady=6)
        self.ent_win = ttk.Entry(grid_frame, show="•", width=30)
        self.ent_win.grid(row=0, column=1, padx=10, pady=6)
        ttk.Button(grid_frame, text="Save Windows", style="Secondary.TButton",
                   command=lambda: self.save_cred("WIN", self.ent_win.get())).grid(row=0, column=2, padx=5, pady=6)

        # Linux
        ttk.Label(grid_frame, text="Linux sudo Password:").grid(row=1, column=0, sticky="w", pady=6)
        self.ent_lin = ttk.Entry(grid_frame, show="•", width=30)
        self.ent_lin.grid(row=1, column=1, padx=10, pady=6)
        ttk.Button(grid_frame, text="Save Linux", style="Secondary.TButton",
                   command=lambda: self.save_cred("LIN", self.ent_lin.get())).grid(row=1, column=2, padx=5, pady=6)

        # macOS
        ttk.Label(grid_frame, text="macOS Login Password:").grid(row=2, column=0, sticky="w", pady=6)
        self.ent_mac = ttk.Entry(grid_frame, show="•", width=30)
        self.ent_mac.grid(row=2, column=1, padx=10, pady=6)
        ttk.Button(grid_frame, text="Save macOS", style="Secondary.TButton",
                   command=lambda: self.save_cred("MAC", self.ent_mac.get())).grid(row=2, column=2, padx=5, pady=6)

        # TOTP
        ttk.Label(grid_frame, text="TOTP Base32 Secret:").grid(row=3, column=0, sticky="w", pady=6)
        self.ent_totp = ttk.Entry(grid_frame, width=30)
        self.ent_totp.grid(row=3, column=1, padx=10, pady=6)
        ttk.Button(grid_frame, text="Save TOTP", style="Secondary.TButton",
                   command=lambda: self.save_cred("TOTP", self.ent_totp.get())).grid(row=3, column=2, padx=5, pady=6)

        btn_box = ttk.Frame(frame, style="Card.TFrame")
        btn_box.pack(fill="x", padx=15, pady=15)

        ttk.Button(btn_box, text="Save All to Device", style="Primary.TButton", command=self.save_all_creds).pack(side="left", padx=5)
        ttk.Button(btn_box, text="Sync Active OS Now", style="Secondary.TButton", command=self.sync_active_os).pack(side="left", padx=5)

    def _build_enroll_tab(self):
        frame = self.tab_enroll

        ttk.Label(frame, text="Enroll Fingerprints onto ZW111 Sensor", font=("Segoe UI", 11, "bold")).pack(anchor="w", padx=15, pady=(15, 5))
        ttk.Label(frame, text="The ZW111 stores templates on-sensor. Up to 40 fingerprints supported.",
                  font=("Segoe UI", 9, "italic")).pack(anchor="w", padx=15, pady=(0, 15))

        slot_frame = ttk.Frame(frame, style="Card.TFrame")
        slot_frame.pack(fill="x", padx=15, pady=5)

        ttk.Label(slot_frame, text="Fingerprint Slot (1-40):").pack(side="left", padx=(0, 10))
        self.spin_slot = ttk.Spinbox(slot_frame, from_=1, to=40, width=5)
        self.spin_slot.set(1)
        self.spin_slot.pack(side="left", padx=(0, 15))

        ttk.Label(slot_frame, text="Action:").pack(side="left", padx=(0, 5))
        self.action_combo = ttk.Combobox(slot_frame, values=["Unlock Current OS", "Type TOTP 2FA"], state="readonly", width=18)
        self.action_combo.current(0)
        self.action_combo.pack(side="left", padx=(0, 15))

        ttk.Button(slot_frame, text="Start Enrollment", style="Primary.TButton", command=self.start_enroll).pack(side="left", padx=5)
        ttk.Button(slot_frame, text="Delete Slot", style="Secondary.TButton", command=self.delete_slot).pack(side="left", padx=5)

        # Status instruction box
        self.enroll_box = tk.Label(frame, text="Ready to enroll.\nSelect a slot number and click 'Start Enrollment'.",
                                   bg="#18181b", fg="#e4e4e7", font=("Segoe UI", 11), padx=15, pady=25,
                                   relief="groove", borderwidth=1)
        self.enroll_box.pack(fill="x", padx=15, pady=20)

    def _build_log_tab(self):
        frame = self.tab_log
        self.log_text = tk.Text(frame, bg="#18181b", fg="#10b981", font=("Consolas", 9), relief="flat")
        self.log_text.pack(fill="both", expand=True, padx=10, pady=10)

    def log(self, msg):
        self.log_text.insert(tk.END, f"[{time.strftime('%H:%M:%S')}] {msg}\n")
        self.log_text.see(tk.END)

    def refresh_ports(self):
        if not HAS_SERIAL:
            self.status_lbl.config(text="pyserial not installed! Run: pip install pyserial")
            return
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            self.port_combo.current(0)

    def toggle_connection(self):
        if not self.connected:
            port = self.port_combo.get()
            if not port:
                messagebox.showerror("Error", "Please select a serial port.")
                return
            try:
                # Tell the background service to yield the COM port!
                try:
                    self.svc_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self.svc_socket.connect(('127.0.0.1', 44332))
                    self.svc_socket.send(b"OVERRIDE")
                    time.sleep(1.0) # Give it a second to close the COM port
                    self.log("Paused background Viper service.")
                except Exception:
                    self.svc_socket = None
                    self.log("No background service detected, proceeding.")

                self.ser = serial.Serial()
                self.ser.port = port
                self.ser.baudrate = 115200
                self.ser.timeout = 0.05      # Very short read timeout to prevent blocking
                self.ser.write_timeout = 1   # Prevent infinite freeze on write
                self.ser.dtr = True          # Required for ESP32 Native USB to accept data
                self.ser.rts = True
                self.ser.open()
                
                self.connected = True
                self.btn_connect.config(text="Disconnect")
                self.status_lbl.config(text=f"Connected: {port} | Syncing OS...", foreground=self.accent_color)
                self.security_warning_lbl.config(text="⚠️ SECURITY PAUSED - Disconnect to resume")
                self.log(f"Connected to {port}")

                # Start listener thread
                self.stop_event.clear()
                self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
                self.rx_thread.start()

                # Sync OS right after connection opens
                self.root.after(500, self.sync_active_os)

            except serial.SerialException as e:
                messagebox.showerror("Connection Error", f"Could not open {port}:\n{e}")
                if self.svc_socket:
                    try:
                        self.svc_socket.close()
                    except:
                        pass
                    self.svc_socket = None
        else:
            self.disconnect()

    def disconnect(self):
        self.stop_event.set()
        if self.ser:
            try:
                self.ser.close()
            except:
                pass
        
        # Resume the background service
        if self.svc_socket:
            try:
                self.svc_socket.close()
                self.log("Resumed background Viper service.")
            except:
                pass
            self.svc_socket = None
        self.connected = False
        self.btn_connect.config(text="Connect")
        self.status_lbl.config(text="Status: Disconnected", foreground="#a1a1aa")
        self.security_warning_lbl.config(text="")
        self.log("Disconnected.")

    def on_closing(self):
        self.disconnect()
        self.root.destroy()

    def send_cmd(self, cmd):
        if not self.connected or not self.ser:
            return False
        try:
            self.ser.write((cmd + "\n").encode("utf-8"))
            self.log(f"TX: {cmd}")
            return True
        except serial.SerialTimeoutException:
            self.log(f"TX Error: Write timed out. Reconnect device.")
            return False
        except Exception as e:
            self.log(f"TX Error: {e}")
            return False

    def sync_active_os(self):
        # Sync time for TOTP
        unix_time = int(time.time())
        self.send_cmd(f"CMD:TIME:{unix_time}")
        
        if self.send_cmd(f"OS:{self.os_code}"):
            self.status_lbl.config(text=f"Active OS: {self.detected_os} (Synced)")
            # Disarm typing while user is logged in to keep Notepad/apps safe
            self.root.after(300, lambda: self.send_cmd("CMD:UNLOCK"))

    def _on_windows_locked(self):
        self.lock_state = True
        self.send_cmd("CMD:LOCK")
        try:
            self.root.after(0, lambda: self.log("GUARD: Windows LOCKED -> Arming Key for login!"))
        except: pass

    def _on_windows_unlocked(self):
        self.lock_state = False
        self.send_cmd("CMD:UNLOCK")
        try:
            self.root.after(0, lambda: self.log("GUARD: Windows UNLOCKED -> Disarming Key (Notepad is safe)!"))
        except: pass

    def _heartbeat_sync(self):
        if self.connected:
            # Sync time periodically for TOTP
            unix_time = int(time.time())
            self.send_cmd(f"CMD:TIME:{unix_time}")
            
            if sys.platform == "win32":
                cmd = "CMD:LOCK" if self.lock_state else "CMD:UNLOCK"
                self.send_cmd(cmd)
        self.root.after(2000, self._heartbeat_sync)

    def save_cred(self, os_type, password):
        if not password:
            messagebox.showwarning("Empty", f"Password for {os_type} is empty.")
            return
        if self.send_cmd(f"SET:{os_type}:{password}"):
            messagebox.showinfo("Success", f"{os_type} password uploaded to ESP32 flash.")

    def save_all_creds(self):
        if self.ent_win.get(): self.save_cred("WIN", self.ent_win.get())
        if self.ent_lin.get(): self.save_cred("LIN", self.ent_lin.get())
        if self.ent_mac.get(): self.save_cred("MAC", self.ent_mac.get())
        if self.ent_totp.get(): self.save_cred("TOTP", self.ent_totp.get())
        self.sync_active_os()

    def start_enroll(self):
        slot = self.spin_slot.get()
        if not slot.isdigit() or not (1 <= int(slot) <= 40):
            messagebox.showerror("Error", "Slot must be 1-40")
            return
            
        action = self.action_combo.get()
        action_code = "TOTP" if "TOTP" in action else self.os_code
        self.send_cmd(f"BIND:{slot}:{action_code}")
        
        self.send_cmd(f"ENROLL:{slot}")
        self.enroll_box.config(text=f"Enrollment started for Slot {slot} ({action}).\nPlease follow sensor LED prompts.", fg="#38bdf8")

    def delete_slot(self):
        slot = self.spin_slot.get()
        if messagebox.askyesno("Confirm", f"Delete fingerprint in Slot #{slot}?"):
            self.send_cmd(f"DELETE:{slot}")

    def _rx_loop(self):
        buffer = ""
        while not self.stop_event.is_set():
            if self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting:
                        chunk = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="replace")
                        buffer += chunk
                        while "\n" in buffer:
                            line, buffer = buffer.split("\n", 1)
                            line = line.strip()
                            if line:
                                self.root.after(0, self._handle_device_message, line)
                    else:
                        time.sleep(0.02)
                except Exception as e:
                    pass
            else:
                time.sleep(0.1)

    def _handle_device_message(self, line):
        self.log(f"RX: {line}")
        if "[Enroll]" in line:
            if "PLACE_FINGER_" in line:
                # E.g. PLACE_FINGER_1_OF_6
                parts = line.split("_")
                if len(parts) >= 4:
                    curr = parts[2]
                    total = parts[4]
                    self.enroll_box.config(text=f"Step {curr}/{total}: Place your finger at a slightly different angle...", fg="#fbbf24")
                else:
                    self.enroll_box.config(text="Place finger on ZW111 sensor...", fg="#fbbf24")
            elif "LIFT_FINGER" in line:
                self.enroll_box.config(text="Good! Now LIFT your finger...", fg="#38bdf8")
            elif "ENROLL_SUCCESS" in line:
                self.enroll_box.config(text="Success! Fingerprint enrolled.", fg="#4ade80")
            elif "MISMATCH_FAIL" in line or "FAILED" in line or "FAIL" in line:
                self.enroll_box.config(text="Enrollment Failed! Try again.", fg="#f87171")
        elif "[Auth] VERIFIED:PASS=" in line:
            pwd = line.split("[Auth] VERIFIED:PASS=")[1].strip()
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(pwd)
                self.root.update()
                self.status_lbl.config(text="Fingerprint Verified! Password copied to Clipboard (Ctrl+V)")
                self.log("AUTH: Fingerprint matched! Password copied to clipboard (auto-clears in 20s).")
                # Clear clipboard after 20 seconds for safety
                self.root.after(20000, self._auto_clear_clipboard, pwd)
            except Exception as e:
                self.log(f"Clipboard Error: {e}")

    def _auto_clear_clipboard(self, expected_pwd):
        try:
            current = self.root.clipboard_get()
            if current == expected_pwd:
                self.root.clipboard_clear()
                self.log("Security: Clipboard cleared.")
        except Exception:
            pass

if __name__ == "__main__":
    root = tk.Tk()
    app = ViperDesktopApp(root)
    root.mainloop()

import sys
import time
import threading
import ctypes
from ctypes import wintypes
import serial
import serial.tools.list_ports

# Windows API constants
WTS_SESSION_LOCK = 0x7
WTS_SESSION_UNLOCK = 0x8
WM_WTSSESSION_CHANGE = 0x02B1

class ViperService:
    def __init__(self):
        self.ser = None
        self.lock_state = False  # Assume unlocked when script starts (as user is logged in)
        self.running = True

    def find_and_connect(self):
        # Prefer known ESP32 VIDs or fallback to COM5 if nothing else
        preferred_vids = [0x303a, 0x2341, 0x1A86] # Espressif, Arduino, CH340
        while self.running:
            try:
                ports = serial.tools.list_ports.comports()
                target_port = None
                
                # First try to find our specific device
                for p in ports:
                    if p.vid in preferred_vids or "COM5" in p.device:
                        target_port = p.device
                        break
                        
                if target_port:
                    self.ser = serial.Serial(target_port, 115200, timeout=1, write_timeout=1)
                    # Enable DTR/RTS for ESP32 CDC
                    self.ser.dtr = True
                    self.ser.rts = True
                    time.sleep(1)
                    
                    # Sync state immediately on connection
                    self.send_state()
                    print(f"Connected to Viper on {target_port}")
                    
                    # Read loop
                    while self.running and self.ser.is_open:
                        try:
                            if self.ser.in_waiting:
                                data = self.ser.readline().decode('utf-8', errors='ignore').strip()
                                if data:
                                    print(f"Device: {data}")
                            else:
                                time.sleep(0.1)
                        except serial.SerialException:
                            print("Connection lost. Reconnecting...")
                            break
            except Exception as e:
                print(f"Error: {e}")
            
            if self.ser:
                self.ser.close()
                self.ser = None
                
            time.sleep(2) # Wait before retry

    def send_state(self):
        if not self.ser or not self.ser.is_open:
            return
        try:
            cmd = "CMD:LOCK\n" if self.lock_state else "CMD:UNLOCK\n"
            self.ser.write(cmd.encode('utf-8'))
            self.ser.flush()
            print(f"Sent: {cmd.strip()}")
        except Exception as e:
            print(f"Failed to send state: {e}")

    def set_locked(self):
        print("Windows Locked - Arming Viper")
        self.lock_state = True
        self.send_state()

    def set_unlocked(self):
        print("Windows Unlocked - Disarming Viper")
        self.lock_state = False
        self.send_state()

def run_windows_monitor(service):
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

    def py_wndproc(hwnd, msg, wparam, lparam):
        if msg == WM_WTSSESSION_CHANGE:
            if wparam == WTS_SESSION_LOCK:
                service.set_locked()
            elif wparam == WTS_SESSION_UNLOCK:
                service.set_unlocked()
        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

    wndproc = WNDPROC(py_wndproc)
    wc = WNDCLASSW()
    wc.lpfnWndProc = wndproc
    wc.lpszClassName = 'ViperServiceMonitorClass'
    atom = user32.RegisterClassW(ctypes.byref(wc))
    hwnd = user32.CreateWindowExW(0, atom, 'ViperServiceMonitor', 0, 0, 0, 0, 0, 0, 0, 0, 0)
    wtsapi32.WTSRegisterSessionNotification(hwnd, 0)

    msg = wintypes.MSG()
    while user32.GetMessageW(ctypes.byref(msg), 0, 0, 0) > 0:
        user32.TranslateMessage(ctypes.byref(msg))
        user32.DispatchMessageW(ctypes.byref(msg))

if __name__ == '__main__':
    service = ViperService()
    
    # Start the serial connection manager in a separate thread
    serial_thread = threading.Thread(target=service.find_and_connect, daemon=True)
    serial_thread.start()
    
    # Run the Windows message loop on the main thread
    run_windows_monitor(service)

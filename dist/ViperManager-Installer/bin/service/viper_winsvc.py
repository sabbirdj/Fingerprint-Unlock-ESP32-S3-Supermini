import win32serviceutil
import win32service
import win32event
import servicemanager
import socket
import sys
import time
import threading
import serial
import serial.tools.list_ports
import os

class ViperService(win32serviceutil.ServiceFramework):
    _svc_name_ = "VipersKeyService"
    _svc_display_name_ = "Viper's Biometric Key Service"
    _svc_description_ = "Monitors Windows lock state for Viper ESP32 hardware and communicates via COM port."

    def __init__(self, args):
        win32serviceutil.ServiceFramework.__init__(self, args)
        self.hWaitStop = win32event.CreateEvent(None, 0, 0, None)
        self.is_running = True
        self.lock_state = False  # Assume unlocked on boot/start
        self.ser = None
        self.ui_connected = False

    def socket_server(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(('127.0.0.1', 44332))
        server.listen(1)
        while self.is_running:
            try:
                server.settimeout(1.0)
                conn, addr = server.accept()
                
                # Wait for initial command
                conn.settimeout(2.0)
                try:
                    data = conn.recv(1024).decode('utf-8').strip()
                except:
                    conn.close()
                    continue

                if data == "OVERRIDE":
                    servicemanager.LogInfoMsg("Viper: UI App connected, releasing COM port.")
                    self.ui_connected = True
                    if self.ser and self.ser.is_open:
                        self.ser.close()
                        self.ser = None
                    
                    # Keep paused as long as connection is alive
                    conn.settimeout(None)
                    while self.is_running:
                        try:
                            if not conn.recv(1024):
                                break
                        except Exception:
                            break
                    servicemanager.LogInfoMsg("Viper: UI App disconnected, resuming background service.")
                    self.ui_connected = False

                elif data.startswith("GATEKEEPER"):
                    if self.ser and self.ser.is_open:
                        self.ser.write(b"CMD:GATEKEEPER\n")
                        self.ser.flush()
                        # Wait for AUTH:SUCCESS or AUTH:FAILED (up to 35 seconds)
                        self.ser.timeout = 35.0
                        start = time.time()
                        response = "AUTH:FAILED"
                        while time.time() - start < 35.0:
                            line = self.ser.readline().decode('utf-8').strip()
                            if line.startswith("AUTH:"):
                                response = line
                                break
                        self.ser.timeout = 0.05
                        conn.send((response + "\n").encode('utf-8'))
                    else:
                        conn.send(b"AUTH:FAILED_NO_DEVICE\n")
                        
                elif data.startswith("SSH_SIGN:"):
                    hash_hex = data.split(":", 1)[1]
                    if self.ser and self.ser.is_open:
                        self.ser.write(f"CMD:SSH_SIGN:{hash_hex}\n".encode('utf-8'))
                        self.ser.flush()
                        self.ser.timeout = 35.0
                        start = time.time()
                        response = "SIG:FAILED"
                        while time.time() - start < 35.0:
                            line = self.ser.readline().decode('utf-8').strip()
                            if line.startswith("SIG:"):
                                response = line
                                break
                        self.ser.timeout = 0.05
                        conn.send((response + "\n").encode('utf-8'))
                    else:
                        conn.send(b"SIG:FAILED_NO_DEVICE\n")

                conn.close()
            except socket.timeout:
                continue
            except Exception as e:
                time.sleep(1)

    # This is required to tell Windows we want to receive SESSIONCHANGE events
    def GetAcceptedControls(self):
        rc = win32serviceutil.ServiceFramework.GetAcceptedControls(self)
        rc |= win32service.SERVICE_ACCEPT_SESSIONCHANGE
        return rc

    def SvcOtherEx(self, control, event_type, data):
        # 0x0000000B is SERVICE_CONTROL_SESSIONCHANGE
        if control == win32service.SERVICE_CONTROL_SESSIONCHANGE:
            if event_type == 0x7: # WTS_SESSION_LOCK
                self.lock_state = True
                self.send_state()
                servicemanager.LogInfoMsg("Viper: Windows Locked")
            elif event_type == 0x8: # WTS_SESSION_UNLOCK
                self.lock_state = False
                self.send_state()
                servicemanager.LogInfoMsg("Viper: Windows Unlocked")
        return win32serviceutil.ServiceFramework.SvcOtherEx(self, control, event_type, data)

    def SvcStop(self):
        self.ReportServiceStatus(win32service.SERVICE_STOP_PENDING)
        self.is_running = False
        win32event.SetEvent(self.hWaitStop)
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_state(self):
        # 1. Send over USB Serial if available
        if self.ser and self.ser.is_open:
            cmd = b"CMD:LOCK\n" if self.lock_state else b"CMD:UNLOCK\n"
            try:
                self.ser.write(cmd)
                self.ser.flush()
            except Exception as e:
                pass
                
        # 2. Broadcast over BLE GATT using WinRT
        try:
            import asyncio
            from winrt.windows.devices.bluetooth.genericattributeprofile import GattDeviceService
            from winrt.windows.devices.enumeration import DeviceInformation
            from winrt.windows.storage.streams import DataWriter
            
            async def do_ble_sync():
                try:
                    devices = await DeviceInformation.find_all_async()
                    target_id = None
                    for d in devices:
                        if d.name and "Viper" in d.name and "19b10000" in d.id:
                            target_id = d.id
                            break
                    if not target_id: return
                    
                    service = await GattDeviceService.from_id_async(target_id)
                    if not service: return
                    
                    chars = await service.get_characteristics_async()
                    for c in chars.characteristics:
                        if "19b10001" in str(c.uuid).lower():
                            writer = DataWriter()
                            cmd = "CMD:LOCK\n" if self.lock_state else "CMD:UNLOCK\n"
                            writer.write_string(cmd)
                            await c.write_value_async(writer.detach_buffer())
                            break
                except Exception:
                    pass
                    
            asyncio.run(do_ble_sync())
        except ImportError:
            pass

    def serial_worker(self):
        preferred_vids = [0x303a, 0x2341, 0x1A86] # ESP32, Arduino, CH340
        while self.is_running:
            if self.ui_connected:
                time.sleep(1)
                continue
                
            try:
                ports = serial.tools.list_ports.comports()
                target_port = None
                
                for p in ports:
                    if p.vid in preferred_vids or "COM5" in p.device:
                        target_port = p.device
                        break
                        
                if target_port:
                    self.ser = serial.Serial(target_port, 115200, timeout=1, write_timeout=1)
                    self.ser.dtr = True
                    self.ser.rts = True
                    time.sleep(1)
                    
                    servicemanager.LogInfoMsg(f"Viper: Connected to {target_port}")
                    self.send_state()
                    
                    # Sync initial time
                    unix_time = int(time.time())
                    self.ser.write(f"CMD:TIME:{unix_time}\n".encode('utf-8'))
                    self.ser.flush()
                    
                    last_sync = time.time()
                    
                    while self.is_running and self.ser and self.ser.is_open and not self.ui_connected:
                        try:
                            # Periodic sync (every 2s) to guarantee ESP32 never misses a state change
                            if time.time() - last_sync > 2.0:
                                self.send_state()
                                unix_time = int(time.time())
                                self.ser.write(f"CMD:TIME:{unix_time}\n".encode('utf-8'))
                                self.ser.flush()
                                last_sync = time.time()

                            if self.ser.in_waiting:
                                data = self.ser.readline()
                                # We can ignore output for the background service
                            else:
                                time.sleep(0.1)
                        except serial.SerialException:
                            break
            except Exception as e:
                pass
            
            if self.ser:
                try:
                    self.ser.close()
                except:
                    pass
                self.ser = None
                
            time.sleep(2) # Reconnect delay

    def SvcDoRun(self):
        servicemanager.LogMsg(servicemanager.EVENTLOG_INFORMATION_TYPE,
                              servicemanager.PYS_SERVICE_STARTED,
                              (self._svc_name_, ''))
        
        # Start serial monitor thread
        t = threading.Thread(target=self.serial_worker)
        t.daemon = True
        t.start()
        
        # Start socket server thread for UI override
        t2 = threading.Thread(target=self.socket_server)
        t2.daemon = True
        t2.start()
        
        # Wait for stop signal
        win32event.WaitForSingleObject(self.hWaitStop, win32event.INFINITE)

if __name__ == '__main__':
    if len(sys.argv) == 1:
        servicemanager.Initialize()
        servicemanager.PrepareToHostSingle(ViperService)
        servicemanager.StartServiceCtrlDispatcher()
    else:
        win32serviceutil.HandleCommandLine(ViperService)

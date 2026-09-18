import asyncio
import win32api
import win32con
import win32gui
import win32ts
import sys
from winrt.windows.devices.bluetooth.genericattributeprofile import GattDeviceService
from winrt.windows.devices.enumeration import DeviceInformation
from winrt.windows.storage.streams import DataWriter

lock_state = False
state_changed = True

def wndproc(hwnd, msg, wparam, lparam):
    global lock_state, state_changed
    if msg == win32con.WM_WTSSESSION_CHANGE:
        if wparam == win32ts.WTS_SESSION_LOCK:
            lock_state = True
            state_changed = True
        elif wparam == win32ts.WTS_SESSION_UNLOCK:
            lock_state = False
            state_changed = True
    return win32gui.DefWindowProc(hwnd, msg, wparam, lparam)

async def write_gatt_state():
    try:
        devices = await DeviceInformation.find_all_async()
        target_id = None
        for d in devices:
            if "Viper" in d.name and "19b10000" in d.id:
                target_id = d.id
                break
                
        if not target_id:
            return
            
        service = await GattDeviceService.from_id_async(target_id)
        if not service:
            return
            
        chars = await service.get_characteristics_async()
        for c in chars.characteristics:
            if "19b10001" in str(c.uuid).lower():
                writer = DataWriter()
                cmd = "CMD:LOCK\n" if lock_state else "CMD:UNLOCK\n"
                writer.write_string(cmd)
                await c.write_value_async(writer.detach_buffer())
                break
    except Exception:
        pass

async def ble_worker():
    global state_changed
    while True:
        if state_changed:
            state_changed = False
            await write_gatt_state()
        await asyncio.sleep(0.5)

async def main():
    wc = win32gui.WNDCLASS()
    wc.lpfnWndProc = wndproc
    wc.lpszClassName = "ViperBleSync"
    wc.hInstance = win32api.GetModuleHandle(None)
    class_atom = win32gui.RegisterClass(wc)
    hwnd = win32gui.CreateWindow(class_atom, "ViperBleSync", 0, 0, 0, 0, 0, 0, 0, wc.hInstance, None)
    win32ts.WTSRegisterSessionNotification(hwnd, win32ts.NOTIFY_FOR_THIS_SESSION)
    
    # Sync initial state on startup
    global state_changed
    state_changed = True
    
    asyncio.create_task(ble_worker())
    
    while True:
        win32gui.PumpWaitingMessages()
        await asyncio.sleep(0.1)

if __name__ == '__main__':
    asyncio.run(main())

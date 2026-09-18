import asyncio
import win32api
import win32con
import win32gui
import win32ts
import sys
from bleak import BleakClient

MAC_ADDRESS = "E0:72:A1:E9:F4:6D"
CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

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

async def ble_worker():
    global state_changed
    while True:
        try:
            async with BleakClient(MAC_ADDRESS, timeout=10.0) as client:
                state_changed = True # Force initial sync
                while client.is_connected:
                    if state_changed:
                        state_changed = False
                        cmd = b"CMD:LOCK\n" if lock_state else b"CMD:UNLOCK\n"
                        try:
                            await client.write_gatt_char(CHAR_UUID, cmd, response=False)
                        except Exception:
                            break
                    await asyncio.sleep(0.5)
        except Exception:
            await asyncio.sleep(5)

async def main():
    wc = win32gui.WNDCLASS()
    wc.lpfnWndProc = wndproc
    wc.lpszClassName = "ViperBleSync"
    wc.hInstance = win32api.GetModuleHandle(None)
    class_atom = win32gui.RegisterClass(wc)
    hwnd = win32gui.CreateWindow(class_atom, "ViperBleSync", 0, 0, 0, 0, 0, 0, 0, wc.hInstance, None)
    win32ts.WTSRegisterSessionNotification(hwnd, win32ts.NOTIFY_FOR_THIS_SESSION)
    
    asyncio.create_task(ble_worker())
    
    while True:
        win32gui.PumpWaitingMessages()
        await asyncio.sleep(0.1)

if __name__ == '__main__':
    asyncio.run(main())

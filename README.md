# Viper's Biometric Key: DIY Biometric Authentication Key
### ZW111 Capacitive Fingerprint Module + ESP32-S3 Super Mini

An open-source, dual-mode (Wired USB-HID + Wireless BLE-HID) biometric authentication key inspired by Viper. 

Stores your credentials securely on the ESP32-S3's on-chip flash memory. Unlike standard HID macros that dangerously type your password into the void if you accidentally touch them, this project features an intelligent **Lock State Sync Architecture** that completely blocks password injection when your Windows PC is unlocked, gracefully flashing yellow instead.

---

## Hardware Components
1. **ESP32-S3 Super Mini** (or ESP32-S3 Zero/Mini)
2. **ZW111 (HLK-ZW111)** Capacitive Fingerprint Sensor Module
3. *(Optional)* Small 3.7V Li-Po battery for wireless Bluetooth use.

---

## Wiring Diagram

| ZW111 Pin | Name | ESP32-S3 Super Mini Pin | Description |
|---|---|---|---|
| Pin 1 | **TOUCH_VCC** | **3V3** | Power for Touch Detection Circuit |
| Pin 2 | **TOUCH_OUT** | **GPIO 1** | Touch Detection Ring (RTC Wakeup) |
| Pin 3 | **VCC / VDD** | **3V3** | Main 3.3V Power |
| Pin 4 | **TX** | **GPIO 4** | Sensor Serial Data Out -> ESP32 RX |
| Pin 5 | **RX** | **GPIO 5** | Sensor Serial Data In <- ESP32 TX |
| Pin 6 | **GND** | **GND** | Ground |

> [!TIP]
> Both the ZW111 and ESP32-S3 operate on native 3.3V logic. No level shifters or resistors are needed!

---

## Architecture

This project requires three components to operate flawlessly:
1. **The ESP32 Firmware** (`VipersKey/`): Handles the capacitive touch, fingerprint matching, secure storage, and HID keystroke injection (both USB and BLE).
2. **The Windows Background Service** (`software/desktop/viper_winsvc.py`): Runs as a system service to monitor your Windows lock screen state. It sends a constant heartbeat to the ESP32 over the USB serial connection.
3. **The Bluetooth Sync Script** (`ViperBleSync.pyw`): Because Windows prevents the `SYSTEM` account from communicating with your personal Bluetooth devices, this lightweight Python script runs in your user's Startup folder. It uses native WinRT GATT to wirelessly broadcast your lock state to the ESP32 every 3 seconds over Bluetooth.

---

## Installation Guide

### 1. Flash the ESP32 Firmware
You can flash the ESP32 using the Arduino IDE (`VipersKey/VipersKey.ino`) or PlatformIO (`firmware/src/main.cpp`). 
- **Required Libraries:** `NimBLE-Arduino`
- **Board Settings:** Enable `USB CDC On Boot` and select `Hardware CDC and JTAG`.

### 2. Install the Windows Background Service
This service is required to tell the ESP32 when your PC is unlocked so it doesn't accidentally leak your password into an open document.
1. Compile the `software/ViperManager` C# desktop app, or use the provided installer in the `dist` folder.
2. The installer will automatically set up `VipersKeyService` in Windows.

### 3. Install the Bluetooth Companion Script
If you plan to use the device wirelessly over Bluetooth, you must run the sync script.
1. Run `InstallBleSync.bat` as your normal Windows user.
2. It will copy `ViperBleSync.pyw` to your Startup folder and launch it in the background to ensure flawless Bluetooth lock-state syncing.

### 4. Enroll Fingers and Passwords
1. Open **ViperManager** from your Start Menu.
2. Plug the ESP32 in via USB. The app will automatically connect and pause the background service.
3. Follow the UI to enroll your fingerprints, set your lock screen PIN/password, and configure your TOTP secret key for 2FA.

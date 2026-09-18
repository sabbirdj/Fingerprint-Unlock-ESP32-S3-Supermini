# Viper's Biometric Key: DIY Biometric Authentication Key
### ZW111 Capacitive Fingerprint Module + ESP32-S3 Super Mini

An open-source, dual-mode (Wired USB-HID + Wireless BLE-HID) biometric authentication key inspired by Viper. 

Stores your credentials directly on the ESP32-S3's encrypted on-chip flash memory and automatically detects whether you are on **Windows, Linux, or macOS** to inject the correct password upon fingerprint verification.

---

## Hardware Components
1. **ESP32-S3 Super Mini** (or ESP32-S3 Zero/Mini)
2. **ZW111 (HLK-ZW111)** Capacitive Fingerprint Sensor Module
3. (Optional for wireless) Small 3.7V Li-Po battery (150mAh - 300mAh)

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

## Flashing the Firmware

### Option A: Using PlatformIO (Recommended)
1. Open the project folder in VS Code with PlatformIO installed.
2. Connect your ESP32-S3 Super Mini via USB-C.
3. Click **Build** and **Upload**.

### Option B: Using Arduino IDE
1. Install ESP32 Board Support: `Tools -> Board -> Boards Manager -> esp32 by Espressif` (version 2.0.14 or later).
2. Install **NimBLE-Arduino** from `Sketch -> Include Library -> Manage Libraries`.
3. Select Board: **ESP32S3 Dev Module**.
4. In `Tools`:
   * **USB Mode:** `Hardware CDC and JTAG` (or `USB-OTG (TinyUSB)`)
   * **USB CDC On Boot:** `Enabled`
   * **Flash Mode:** `QIO 80MHz`
   * **Partition Scheme:** `Default 4MB with spiffs`
5. Open `firmware/src/main.cpp` and click **Upload**.

---

## Using the Device

1. Plug the device in via USB-C or connect it via Bluetooth (`Viper's Biometric Key`).
2. Run the desktop management tool:
   ```bash
   cd software/desktop
   pip install -r requirements.txt
   python app.py
   ```
3. Use the desktop app to enroll your fingerprints and save your system passwords.
4. Touch the ZW111 sensor to instantly unlock your screen, elevate terminal privileges, or enter TOTP codes!

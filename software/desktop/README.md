# Viper's Biometric Key Desktop Management Application

A cross-platform manager for your **ZW111 + ESP32-S3 Biometric Auth Key**.

## Features
* **Automatic OS Detection:** Automatically detects whether you are running on Windows, Linux, or macOS, and tells your ESP32-S3 device to use the matching credential profile.
* **On-Device Password Vault:** Directly write encrypted credentials for Windows, Linux (sudo), macOS, and TOTP to the ESP32-S3 flash memory.
* **Fingerprint Enrollment Wizard:** Interactive GUI that guides you step-by-step through enrolling fingerprints on the ZW111 sensor.
* **Real-time Diagnostic Log:** Live monitor showing device output, match status, and active profiles.

---

## Setup & Running

### Requirements
Ensure you have Python 3.8+ installed.

Install the required dependencies:
```bash
pip install -r requirements.txt
```

### Launch the App
```bash
python app.py
```

---

## Quick Start Guide
1. Plug in your ESP32-S3 Super Mini via USB-C.
2. Launch `app.py`.
3. Select the COM / Serial port of your ESP32-S3 and click **Connect**.
4. The app will automatically sync your host OS (e.g., `Active Host: Windows`).
5. Open the **Fingerprint Enrollment** tab to enroll your finger.
6. Open the **Password Vault** tab and enter your system password. Click **Save to Device**.
7. Test it: Lock your computer or open a password field, and touch the ZW111 sensor!

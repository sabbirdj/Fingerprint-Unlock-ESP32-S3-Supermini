# ⚡ Viper's Biometric Key Manager (Portable Edition)

Welcome to the **Portable Edition** of Viper's Biometric Key Manager! This is a completely self-contained, native executable that requires **no installation** and **no administrator privileges** to run. You can run it directly from this folder, a USB flash drive, or any computer.

---

## 🚀 Step-by-Step Quick Start Guide

### Step 1: Connect Your Viper Hardware
1. Plug your **Viper Key** (ESP32-S3 + ZW111 Biometric Key) into any available USB port.
2. The sensor ring will briefly pulse blue to indicate power and hardware readiness.

### Step 2: Launch the Manager
1. Double-click **`ViperManager.exe`** in this folder.
2. The modern dark UI will open instantly.

### Step 3: Connect to the Key
1. In the top-right app bar, ensure your device's COM port is selected in the dropdown (e.g. `COM4` or `COM5`).
   - If your port isn't shown, click the **🔄 Refresh** button.
2. Click **Connect Device**.
3. The status indicator will turn **Green (Connected)**.

---

## 🔑 Key Features & How to Use

### 1. OS Passwords Vault
1. Click **OS Passwords** in the left sidebar.
2. Enter your passwords for **Windows**, **macOS**, and/or **Linux**.
3. Click the **👁️** button to reveal or conceal passwords as needed.
4. Click **Save Windows**, **Save macOS**, or **Save Linux** (or click **💾 Save All to Hardware**).
5. Passwords are saved directly into the encrypted flash memory of the key.

### 2. 🖐️ Fingerprint Studio (6-Stage Enrollment Wizard)
1. Click **Fingerprint Studio** in the left sidebar.
2. Select a slot number (1 to 40).
3. Choose the **Action Assignment**:
   - **Unlock Active Host OS**: Types your login password on the lock screen or terminal.
   - **Type TOTP 2FA Code**: Generates and types live 6-digit 2FA codes.
4. Click **Start 6-Stage Enrollment**:
   - **Step 1/6**: Place the center of your finger firmly on the sensor.
   - **Lift**: Remove your finger completely when prompted.
   - **Step 2/6**: Place the **tip** of your finger.
   - **Step 3/6**: Place the **left side edge** of your finger.
   - **Step 4/6**: Place the **right side edge** of your finger.
   - **Step 5/6**: Place the **lower pad** of your finger.
   - **Step 6/6**: Place the center one final time to finalize.
5. The wizard will show **Completed** with a green success badge!

### 3. 🛡️ Hardware-Backed SSH Keys (1:1 Immurok Parity)
1. Click **SSH Hardware Keys** in the left sidebar.
2. Click **⚡ Generate Keypair on Device**:
   - The hardware generates an unextractable ECDSA secp256r1 key directly inside the microcontroller. The private key never leaves the hardware.
3. Click **📋 Copy OpenSSH Key**:
   - Copies the standard OpenSSH public key (`ecdsa-sha2-nistp256 AAAAE2Vj... viper@hardware-key`) directly to your clipboard.
   - Paste this key into **GitHub Settings -> SSH and GPG Keys** or your server's `~/.ssh/authorized_keys`.
4. Click **🧪 Test Touch & Sign**:
   - Sends a test challenge to the key.
   - The sensor will pulse **Purple**.
   - Touch your finger to authorize the signature.

### 4. ⏱️ Live TOTP (2FA) Authenticator Vault
1. Click **TOTP 2FA Vault** in the left sidebar.
2. Paste your 2FA secret (Base32, e.g. from Google Authenticator, GitHub, or AWS).
3. Click **💾 Save to Key**.
4. The live 6-digit code and a real-time 30-second circular countdown will display directly on screen.
5. In **Fingerprint Studio**, assign a finger to **Type TOTP 2FA Code**. Scanning that finger automatically types the live 6 digits directly into any browser or login prompt!

---

## 🛡️ Optional: Setting Up the Background Guard on Windows

In **Portable Mode**, the manager communicates directly with the key over USB. 

If you want the **Native Background Service** (which monitors `Win + L` and automatically locks/arms the key so it never types plain text into Notepad when logged in):
- Run the **Installable Version** (`ViperManager-Installer`), which configures the background service automatically.
- Or manually install the service using the scripts in `software/desktop/install_native_service.bat`.

---

## 🐧 macOS and Linux Notes

- **macOS:** Viper Key acts as a standard USB/Bluetooth HID keyboard. No drivers required.
- **Linux:** If accessing the serial port gives a permission denied error, add your user to the `dialout` group:
  ```bash
  sudo usermod -aG dialout $USER
  ```
  Then log out and log back in.

---

## 🎨 Hardware LED Color Reference

| LED Color & Mode | Meaning |
| :--- | :--- |
| **Breathing Blue** | Key is idle, powered, and ready for fingerprints. |
| **Solid Blue** | Finger touch detected on metal bezel. |
| **Flashing Green** | Finger matched successfully / Action verified. |
| **Flashing Red** | Fingerprint not recognized or enrollment error. |
| **Flashing Purple** | Hardware SSH signature requested; waiting for physical touch. |
| **Flashing Yellow** | AI Gatekeeper requested; waiting for physical touch. |
| **Solid Yellow** | Lift finger during 6-stage enrollment. |

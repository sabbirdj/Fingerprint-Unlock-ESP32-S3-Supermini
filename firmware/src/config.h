#pragma once

#include <Arduino.h>

// ==========================================
// Hardware Pin Definitions (ESP32-S3 Super Mini)
// ==========================================
// ZW111 UART connection (HardwareSerial 1)
#define ZW111_RX_PIN        44   // ESP32 physical 'RX' pin (connect to Sensor TX)
#define ZW111_TX_PIN        43   // ESP32 physical 'TX' pin (connect to Sensor RX)
#define ZW111_WAKE_PIN      1    // ESP32 RTC Wakeup (connect to ZW111 Touch Out)

// ZW111 Default Baud Rate
#define ZW111_BAUD_RATE     57600

// Onboard LED on ESP32-S3 Super Mini (usually GPIO 8 on most S3 SuperMini boards)
#define STATUS_LED_PIN      8

// ==========================================
// Bluetooth Configuration
// ==========================================
#define BLE_DEVICE_NAME     "Viper's Biometric Key"
#define BLE_SERVICE_UUID    "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_OS_SYNC_UUID   "19b10001-e8f2-537e-4f6c-d104768a1214"
#define CHAR_VAULT_UUID     "19b10002-e8f2-537e-4f6c-d104768a1214"
#define CHAR_ENROLL_UUID    "19b10003-e8f2-537e-4f6c-d104768a1214"
#define CHAR_STATUS_UUID    "19b10004-e8f2-537e-4f6c-d104768a1214"

// ==========================================
// Operating System Types
// ==========================================
enum OSType : uint8_t {
    OS_UNKNOWN = 0,
    OS_WINDOWS = 1,
    OS_LINUX   = 2,
    OS_MACOS   = 3,
    OS_TOTP    = 4
};

// Maximum stored credentials
#define MAX_PASSWORD_LEN    128
#define MAX_TOTP_SECRET_LEN 64

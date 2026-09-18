#include <Arduino.h>
#include "config.h"
#include "zw111.h"
#include "storage.h"
#include "hid_manager.h"
#include "ble_service.h"

// Hardware instances
HardwareSerial FingerprintSerial(1);
ZW111 sensor(FingerprintSerial, ZW111_RX_PIN, ZW111_TX_PIN, ZW111_WAKE_PIN);
StorageManager storage;
HIDManager hid;
BleManager ble(storage, sensor, hid);

// Debounce and timing
uint32_t lastScanTime = 0;
const uint32_t SCAN_COOLDOWN_MS = 1500;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("  Viper's Biometric Key Boot  ");
    Serial.println("=================================");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // 1. Initialize Storage Vault
    storage.begin();
    Serial.printf("[Storage] Active OS Profile: %d\n", storage.getActiveOS());

    // 2. Initialize HID (USB Native + BLE)
    hid.begin();
    Serial.println("[HID] USB HID Keyboard initialized.");

    // 3. Initialize ZW111 Sensor
    Serial.println("[Sensor] Initializing ZW111 on GPIO 4(RX) / 5(TX)...");
    if (sensor.begin(ZW111_BAUD_RATE)) {
        Serial.println("[Sensor] ZW111 connected successfully!");
        sensor.setLed(LED_BREATHING, LED_CYAN, 50, 1);
    } else {
        Serial.println("[Sensor] Warning: ZW111 not responding. Check wiring!");
        sensor.setLed(LED_FLASHING, LED_RED, 20, 3);
    }

    // 4. Initialize BLE Services
    ble.begin();
    Serial.println("[BLE] Advertising as 'Viper's Biometric Key'. Ready to pair.");
}

void processSerialCommand(const String& cmd) {
    if (cmd.startsWith("OS:")) {
        String os = cmd.substring(3);
        if (os.equalsIgnoreCase("WIN")) storage.setActiveOS(OS_WINDOWS);
        else if (os.equalsIgnoreCase("LIN")) storage.setActiveOS(OS_LINUX);
        else if (os.equalsIgnoreCase("MAC")) storage.setActiveOS(OS_MACOS);
        Serial.printf("Active OS set to: %d\n", storage.getActiveOS());
    } else if (cmd.startsWith("SET:WIN:")) {
        storage.setPassword(OS_WINDOWS, cmd.substring(8));
        Serial.println("Windows password updated.");
    } else if (cmd.startsWith("SET:LIN:")) {
        storage.setPassword(OS_LINUX, cmd.substring(8));
        Serial.println("Linux password updated.");
    } else if (cmd.startsWith("SET:MAC:")) {
        storage.setPassword(OS_MACOS, cmd.substring(8));
        Serial.println("Mac password updated.");
    } else if (cmd.startsWith("ENROLL:")) {
        uint16_t slot = cmd.substring(7).toInt();
        Serial.printf("Enrolling finger to slot %d...\n", slot);
        bool ok = sensor.enrollFingerprint(slot, [](const char* step) {
            Serial.printf("[Enroll] %s\n", step);
        });
        Serial.println(ok ? "Enroll SUCCESS" : "Enroll FAILED");
    } else if (cmd == "STATUS") {
        Serial.printf("OS: %d | USB: %s | BLE: %s | Fingers: %d\n",
                      storage.getActiveOS(),
                      hid.isUsbReady() ? "YES" : "NO",
                      ble.isConnected() ? "CONNECTED" : "DISCONNECTED",
                      sensor.getFingerprintCount());
    }
}

void loop() {
    // 1. Process BLE GATT tasks
    ble.loop();

    // 2. Process USB Serial commands from desktop
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            processSerialCommand(line);
        }
    }

    // 3. Check for finger touch detection
    bool touch = sensor.isTouchDetected();
    if (touch && (millis() - lastScanTime > SCAN_COOLDOWN_MS)) {
        lastScanTime = millis();
        Serial.println("[Touch] Finger detected on bezel ring!");
        sensor.setLed(LED_ON, LED_BLUE);

        // Scan and match
        int16_t matchedId = sensor.scanAndMatch(40);
        if (matchedId > 0) {
            Serial.printf("[Match] Verified Finger ID: #%d\n", matchedId);
            sensor.setLed(LED_FLASHING, LED_GREEN, 20, 2);

            // Check if this slot has a dedicated action or use active OS
            OSType action = storage.getSlotAction(matchedId);
            if (action == OS_UNKNOWN) {
                action = storage.getActiveOS();
            }

            // Retrieve credentials
            String password = storage.getPassword(action);
            if (password.length() > 0) {
                Serial.printf("[Auth] Injecting credentials for Profile %d...\n", action);
                hid.typeString(password, true); // Type password and hit Enter
            } else {
                Serial.println("[Auth] Warning: No password configured for this profile!");
                sensor.setLed(LED_FLASHING, LED_YELLOW, 20, 2);
            }
        } else {
            Serial.println("[Match] Finger not recognized.");
            sensor.setLed(LED_FLASHING, LED_RED, 20, 2);
        }

        // Wait for finger to lift
        while (sensor.isTouchDetected()) {
            delay(50);
        }
        sensor.setLed(LED_OFF, LED_BLUE);
    }

    delay(20);
}

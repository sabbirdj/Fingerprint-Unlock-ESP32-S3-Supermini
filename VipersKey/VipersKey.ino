#include <Arduino.h>
#include "USB.h"
#include "config.h"
#include "zw111.h"
#include "storage.h"
#include "hid_manager.h"
#include "ble_service.h"
#include "totp_generator.h"
#include "ssh_agent.h"

// Hardware instances
HardwareSerial FingerprintSerial(1);
ZW111 sensor(FingerprintSerial, ZW111_RX_PIN, ZW111_TX_PIN, ZW111_WAKE_PIN);
StorageManager storage;
HIDManager hid;
BleManager ble(storage, sensor, hid);
SSHAgent ssh;

// Debounce and timing
uint32_t lastScanTime = 0;
const uint32_t SCAN_COOLDOWN_MS = 1500;

// Global Time Sync (for TOTP)
uint32_t syncedUnixTime = 0;
uint32_t syncLocalMillis = 0;

uint32_t getCurrentUnixTime() {
    if (syncedUnixTime == 0) return 0;
    return syncedUnixTime + ((millis() - syncLocalMillis) / 1000);
}

void setup() {
    // Start Serial communication FIRST so the COM port is added to the USB profile
    Serial.begin(115200);
    delay(500);

    // Force a specific USB ID so Windows doesn't accidentally install the wrong driver!
    USB.PID(0x4004);

    // Now initialize HID (USB Keyboard)
    hid.begin();
    delay(500);
    Serial.println("\n=================================");
    Serial.println("  Viper's Biometric Key Boot  ");
    Serial.println("=================================");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // 2. Initialize Storage Vault
    storage.begin();
    Serial.printf("[Storage] Active OS Profile: %d\n", storage.getActiveOS());
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
    ble.setCommandCallback([](const String& cmd) {
        processSerialCommand(cmd);
    });
    Serial.println("[BLE] Advertising as 'Viper's Biometric Key'. Ready to pair.");
}

// Lock Guard: Managed by desktop service/app. If guard is active, disarms on desktop.
bool isWindowsLocked = false;
bool guardActive = false;
uint32_t lastGuardHeartbeat = 0;

void sendResponse(const String& msg) {
    Serial.println(msg);
    ble.notifyStatus(msg);
}

void processSerialCommand(const String& cmd) {
    if (cmd.startsWith("ENROLL:")) {
        uint16_t slot = cmd.substring(7).toInt();
        Serial.printf("Enrolling finger to slot %d...\n", slot);
        bool ok = sensor.enrollFingerprint(slot, [](const char* step) {
            Serial.printf("[Enroll] %s\n", step);
            ble.notifyStatus(String("[Enroll] ") + step);
        });
        sendResponse(ok ? "Enroll SUCCESS" : "Enroll FAILED");
    } else if (cmd.startsWith("DELETE:") || cmd.startsWith("CMD:DELETE:")) {
        int colonIdx = cmd.lastIndexOf(':');
        uint16_t slot = cmd.substring(colonIdx + 1).toInt();
        if (sensor.deleteFingerprint(slot)) {
            storage.setSlotAction(slot, OS_UNKNOWN);
            sendResponse(String("DELETE:SUCCESS:") + slot);
        } else {
            sendResponse(String("DELETE:FAILED:") + slot);
        }
    } else if (cmd == "WIPE" || cmd == "CMD:WIPE") {
        if (sensor.clearDatabase()) {
            for (int i = 1; i <= 40; i++) storage.setSlotAction(i, OS_UNKNOWN);
            sendResponse("WIPE:SUCCESS");
        } else {
            sendResponse("WIPE:FAILED");
        }
    } else if (cmd.startsWith("OS:")) {
        String os = cmd.substring(3);
        if (os.equalsIgnoreCase("WIN")) storage.setActiveOS(OS_WINDOWS);
        else if (os.equalsIgnoreCase("LIN")) storage.setActiveOS(OS_LINUX);
        else if (os.equalsIgnoreCase("MAC")) storage.setActiveOS(OS_MACOS);
        sendResponse(String("ACTIVE_OS:") + storage.getActiveOS());
    } else if (cmd.startsWith("SET:WIN:")) {
        storage.setPassword(OS_WINDOWS, cmd.substring(8));
        sendResponse("Windows password updated.");
    } else if (cmd.startsWith("SET:LIN:")) {
        storage.setPassword(OS_LINUX, cmd.substring(8));
        sendResponse("Linux password updated.");
    } else if (cmd.startsWith("SET:MAC:")) {
        storage.setPassword(OS_MACOS, cmd.substring(8));
        sendResponse("Mac password updated.");
    } else if (cmd.startsWith("SET:TOTP:")) {
        storage.setTotpSecret(cmd.substring(9));
        sendResponse("TOTP Secret updated.");
    } else if (cmd.startsWith("CMD:TIME:")) {
        syncedUnixTime = cmd.substring(9).toInt();
        syncLocalMillis = millis();
        sendResponse(String("Time synced: ") + syncedUnixTime);
    } else if (cmd.startsWith("BIND:")) {
        // Format: BIND:slot:os_code
        int firstColon = cmd.indexOf(':', 5);
        if (firstColon != -1) {
            uint16_t slot = cmd.substring(5, firstColon).toInt();
            String osCode = cmd.substring(firstColon + 1);
            OSType action = OS_UNKNOWN;
            if (osCode == "WIN") action = OS_WINDOWS;
            else if (osCode == "LIN") action = OS_LINUX;
            else if (osCode == "MAC") action = OS_MACOS;
            else if (osCode == "TOTP") action = OS_TOTP;
            
            storage.setSlotAction(slot, action);
            sendResponse(String("Slot ") + slot + " bound to action " + action);
        }
    } else if (cmd == "CMD:SSH_GEN") {
        if (ssh.generateKey()) {
            sendResponse("SSH_GEN:SUCCESS");
        } else {
            sendResponse("SSH_GEN:FAILED");
        }
    } else if (cmd == "CMD:SSH_PUB") {
        sendResponse(String("SSH_PUB:") + ssh.getPublicKeyHex());
    } else if (cmd.startsWith("CMD:SSH_SIGN:")) {
        String hashHex = cmd.substring(13);
        Serial.println("[SSH] Challenge received. WAITING FOR FINGERPRINT TOUCH...");
        sensor.setLed(LED_FLASHING, LED_PURPLE, 50, 0); // Flashing purple forever
        bool authSuccess = false;
        uint32_t timeout = millis() + 30000;
        while (millis() < timeout) {
            ble.loop();
            if (sensor.isTouchDetected()) {
                if (sensor.scanAndMatch() > 0) {
                    authSuccess = true;
                    sensor.setLed(LED_ON, LED_GREEN, 100, 1);
                    break;
                } else {
                    sensor.setLed(LED_FLASHING, LED_RED, 20, 2);
                    delay(1000);
                    sensor.setLed(LED_FLASHING, LED_PURPLE, 50, 0);
                }
            }
            delay(50);
        }
        if (authSuccess) {
            String sig = ssh.signHashHex(hashHex);
            sendResponse(String("SIG:") + sig);
        } else {
            sendResponse("SIG:FAILED");
            sensor.setLed(LED_OFF, LED_BLUE, 0, 0);
        }
    } else if (cmd == "CMD:GATEKEEPER") {
        Serial.println("[Gatekeeper] Auth requested. WAITING FOR FINGERPRINT TOUCH...");
        sensor.setLed(LED_FLASHING, LED_YELLOW, 50, 0);
        bool authSuccess = false;
        uint32_t timeout = millis() + 30000;
        while (millis() < timeout) {
            ble.loop();
            if (sensor.isTouchDetected()) {
                if (sensor.scanAndMatch() > 0) {
                    authSuccess = true;
                    sensor.setLed(LED_ON, LED_GREEN, 100, 1);
                    break;
                } else {
                    sensor.setLed(LED_FLASHING, LED_RED, 20, 2);
                    delay(1000);
                    sensor.setLed(LED_FLASHING, LED_YELLOW, 50, 0);
                }
            }
            delay(50);
        }
        if (authSuccess) {
            sendResponse("AUTH:SUCCESS");
        } else {
            sendResponse("AUTH:FAILED");
            sensor.setLed(LED_OFF, LED_BLUE, 0, 0);
        }
    } else if (cmd == "CMD:LOCK" || cmd == "LOCK") {
        isWindowsLocked = true;
        guardActive = true;
        lastGuardHeartbeat = millis();
        sendResponse("[Guard] ARMED - typing ENABLED");
    } else if (cmd == "CMD:UNLOCK" || cmd == "UNLOCK") {
        isWindowsLocked = false;
        guardActive = true;
        lastGuardHeartbeat = millis();
        sendResponse("[Guard] DISARMED - typing BLOCKED");
    } else if (cmd == "STATUS") {
        char buf[128];
        snprintf(buf, sizeof(buf), "OS: %d | Guard: %s | Active: %s | USB: %s | BLE: %s | Fingers: %d",
                 storage.getActiveOS(),
                 isWindowsLocked ? "ARMED (Locked)" : "DISARMED (Unlocked)",
                 guardActive ? "YES" : "STANDALONE",
                 hid.isUsbReady() ? "YES" : "NO",
                 ble.isConnected() ? "CONNECTED" : "DISCONNECTED",
                 sensor.getFingerprintCount());
        sendResponse(String(buf));
    }
}

void loop() {
    // 1. Process BLE GATT tasks
    ble.loop();

    // 2. Check if host guard timed out (e.g. app closed)
    if (guardActive && (millis() - lastGuardHeartbeat > 7000)) {
        guardActive = false;
        isWindowsLocked = true;
    }

    // 3. Process USB Serial commands from desktop — drain ALL queued commands
    while (Serial.available()) {
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

            if (action == OS_TOTP) {
                String secret = storage.getTotpSecret();
                if (secret.length() > 0 && syncedUnixTime > 0) {
                    Serial.println("[Auth] TOTP requested. Calculating live 2FA code...");
                    String code = TotpGenerator::getCode(secret, getCurrentUnixTime());
                    hid.typeString(code, false); // Don't press enter, just type the 6 digits
                    lastScanTime = millis() + 2000;
                } else {
                    Serial.println("[Auth] Warning: No TOTP secret stored or Time not synced!");
                    sensor.setLed(LED_FLASHING, LED_YELLOW, 20, 2);
                }
            } else {
                // Retrieve credentials for normal password typing
                String password = storage.getPassword(action);
                if (password.length() > 0) {
                    // CRITICAL: Drain serial buffer RIGHT NOW before typing.
                    // A CMD:UNLOCK may have arrived during the fingerprint scan.
                    while (Serial.available()) {
                        String cmd = Serial.readStringUntil('\n');
                        cmd.trim();
                        if (cmd.length() > 0) processSerialCommand(cmd);
                    }
                    
                    bool canType = true;
                    // If desktop Guard is actively connected and asserts Windows is UNLOCKED, block typing
                    if (action == OS_WINDOWS && guardActive && !isWindowsLocked) {
                        canType = false;
                    }

                    if (canType) {
                        Serial.printf("[Auth] Verified! Unlocking (Profile %d, Transport: %s)...\n",
                                      action, hid.isBleConnected() ? "BLE" : "USB");
                        hid.typeString(password, true); // Win+L -> Space -> Ctrl+A+BS -> Password -> Enter
                        
                        if (guardActive) {
                            isWindowsLocked = false;
                        }
                        lastScanTime = millis() + 3500;
                    } else {
                        Serial.println("[Auth] Verified! But Windows is already UNLOCKED (Guard active). Ignoring for security.");
                        sensor.setLed(LED_FLASHING, LED_YELLOW, 20, 2);
                    }
                } else {
                    Serial.println("[Auth] Warning: No password configured for this profile!");
                    sensor.setLed(LED_FLASHING, LED_YELLOW, 20, 2);
                }
            }
        } else {
            Serial.println("[Match] Finger not recognized.");
            sensor.setLed(LED_FLASHING, LED_RED, 20, 2);
        }

        // Wait a short moment then turn off LED (prevents freezing if touch pin gets stuck)
        delay(1000); 
        sensor.setLed(LED_OFF, LED_BLUE);
    }

    delay(20);
}

#include "ble_service.h"

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    ServerCallbacks(BleManager* mgr, HIDManager* hid) : _mgr(mgr), _hid(hid) {}
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.println("[BLE] Client connected!");
        if (_hid) _hid->setBleConnected(true);
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("[BLE] Client disconnected (reason: %d). Restarting advertising...\n", reason);
        if (_hid) _hid->setBleConnected(false);
        extern bool guardActive;
        extern bool isWindowsLocked;
        guardActive = false;
        isWindowsLocked = true;
        NimBLEDevice::startAdvertising();
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        Serial.println("[BLE] Pairing & Authentication successful!");
    }
private:
    BleManager* _mgr;
    HIDManager* _hid;
};

class OsSyncCallbacks : public NimBLECharacteristicCallbacks {
public:
    OsSyncCallbacks(BleManager* mgr) : _mgr(mgr) {}
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        std::string val = pChar->getValue();
        // Forward to manager
    }
private:
    BleManager* _mgr;
};

BleManager::BleManager(StorageManager& storage, ZW111& sensor, HIDManager& hid)
    : _storage(storage), _sensor(sensor), _hid(hid) {}

void BleManager::setCommandCallback(BleCommandCallback cb) {
    _cmdCallback = cb;
}

void BleManager::begin() {
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(9); // Max power (+9 dBm)
    NimBLEDevice::setMTU(512); // High MTU for SSH keys and bulk responses

    // Configure security for Windows 10/11 "Just Works" pairing & bonding
    NimBLEDevice::setSecurityAuth(true, false, true); // Bonding=true, MITM=false, SC=true
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    _pServer = NimBLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(this, &_hid));

    // Initialize BLE HID Device & Keyboard Characteristics
    _pHidDev = _hid.setupBleHid(_pServer);

    // Custom Management Service
    NimBLEService* pService = _pServer->createService(BLE_SERVICE_UUID);

    // OS Sync Characteristic
    _pOsChar = pService->createCharacteristic(
        CHAR_OS_SYNC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _pOsChar->setCallbacks(new NimBLECharacteristicCallbacks());

    // Vault Configuration & Command Characteristic
    _pVaultChar = pService->createCharacteristic(
        CHAR_VAULT_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );

    // Fingerprint Enrollment Characteristic
    _pEnrollChar = pService->createCharacteristic(
        CHAR_ENROLL_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );

    // Status Characteristic
    _pStatusChar = pService->createCharacteristic(
        CHAR_STATUS_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    // Setup Advertising for Windows 10/11 Bluetooth Keyboard Discovery
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(0x03C1); // HID_KEYBOARD

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06); // General Discoverable + BR/EDR not supported
    advData.setAppearance(0x03C1); // HID Keyboard
    advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812)); // HID Service UUID
    pAdvertising->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.setName(BLE_DEVICE_NAME); // "Viper's Biometric Key"
    pAdvertising->setScanResponseData(scanData);

    pAdvertising->start();
    Serial.println("[BLE] Bluetooth HID Keyboard & Management Service initialized and advertising.");
}

void BleManager::loop() {
    // 1. Process incoming OS Sync write
    if (_pOsChar && _pOsChar->getValue().length() > 0) {
        std::string val = _pOsChar->getValue();
        _pOsChar->setValue("");
        handleOsSync(String(val.c_str()));
    }

    // 2. Process incoming Vault & Central Management commands
    if (_pVaultChar && _pVaultChar->getValue().length() > 0) {
        std::string cmd = _pVaultChar->getValue();
        _pVaultChar->setValue("");
        String cmdStr = String(cmd.c_str());
        cmdStr.trim();
        if (cmdStr.length() > 0) {
            if (_cmdCallback) {
                _cmdCallback(cmdStr);
            } else {
                handleVaultCommand(cmdStr);
            }
        }
    }

    // 3. Process incoming Enroll commands
    if (_pEnrollChar && _pEnrollChar->getValue().length() > 0) {
        std::string cmd = _pEnrollChar->getValue();
        handleEnrollCommand(String(cmd.c_str()));
        _pEnrollChar->setValue("");
    }

    // 4. Handle active enrollment state machine
    if (_enrolling) {
        bool ok = _sensor.enrollFingerprint(_enrollSlot, [this](const char* step) {
            if (_pEnrollChar) {
                _pEnrollChar->setValue(step);
                _pEnrollChar->notify();
            }
        });
        _enrolling = false;
        if (_pEnrollChar) {
            _pEnrollChar->setValue(ok ? "ENROLL_SUCCESS" : "ENROLL_FAILED");
            _pEnrollChar->notify();
        }
    }
}

void BleManager::handleOsSync(const String& data) {
    if (data.equalsIgnoreCase("WIN") || data.equalsIgnoreCase("WINDOWS")) {
        _storage.setActiveOS(OS_WINDOWS);
        notifyStatus("ACTIVE_OS:WINDOWS");
    } else if (data.equalsIgnoreCase("LIN") || data.equalsIgnoreCase("LINUX")) {
        _storage.setActiveOS(OS_LINUX);
        notifyStatus("ACTIVE_OS:LINUX");
    } else if (data.equalsIgnoreCase("MAC") || data.equalsIgnoreCase("MACOS")) {
        _storage.setActiveOS(OS_MACOS);
        notifyStatus("ACTIVE_OS:MACOS");
    } else if (data.equalsIgnoreCase("LOCK") || data.equalsIgnoreCase("CMD:LOCK")) {
        extern bool isWindowsLocked;
        extern bool guardActive;
        extern uint32_t lastGuardHeartbeat;
        isWindowsLocked = true;
        guardActive = true;
        lastGuardHeartbeat = millis();
        notifyStatus("GUARD:LOCKED");
        Serial.println("[BLE] Guard ARMED via BLE - typing ENABLED");
    } else if (data.equalsIgnoreCase("UNLOCK") || data.equalsIgnoreCase("CMD:UNLOCK")) {
        extern bool isWindowsLocked;
        extern bool guardActive;
        extern uint32_t lastGuardHeartbeat;
        isWindowsLocked = false;
        guardActive = true;
        lastGuardHeartbeat = millis();
        notifyStatus("GUARD:UNLOCKED");
        Serial.println("[BLE] Guard DISARMED via BLE - typing BLOCKED");
    }
}

void BleManager::handleVaultCommand(const String& cmd) {
    // Expected format: "SET:<OS>:<PASSWORD>"
    if (cmd.startsWith("SET:WIN:")) {
        _storage.setPassword(OS_WINDOWS, cmd.substring(8));
        notifyStatus("VAULT:WIN_UPDATED");
    } else if (cmd.startsWith("SET:LIN:")) {
        _storage.setPassword(OS_LINUX, cmd.substring(8));
        notifyStatus("VAULT:LIN_UPDATED");
    } else if (cmd.startsWith("SET:MAC:")) {
        _storage.setPassword(OS_MACOS, cmd.substring(8));
        notifyStatus("VAULT:MAC_UPDATED");
    } else if (cmd.startsWith("SET:TOTP:")) {
        _storage.setTotpSecret(cmd.substring(9));
        notifyStatus("VAULT:TOTP_UPDATED");
    } else if (cmd == "CLEAR_ALL") {
        _storage.clearAll();
        notifyStatus("VAULT:CLEARED");
    }
}

void BleManager::handleEnrollCommand(const String& cmd) {
    // Expected format: "ENROLL:<SLOT_ID>"
    if (cmd.startsWith("ENROLL:")) {
        _enrollSlot = cmd.substring(7).toInt();
        if (_enrollSlot < 1 || _enrollSlot > 40) _enrollSlot = 1;
        _enrolling = true;
    } else if (cmd.startsWith("DELETE:")) {
        uint16_t slot = cmd.substring(7).toInt();
        _sensor.deleteFingerprint(slot);
        notifyStatus("FINGER:DELETED");
    }
}

void BleManager::notifyStatus(const String& status) {
    if (_pStatusChar) {
        _pStatusChar->setValue(status.c_str());
        _pStatusChar->notify();
    }
}

bool BleManager::isConnected() {
    return _pServer && _pServer->getConnectedCount() > 0;
}

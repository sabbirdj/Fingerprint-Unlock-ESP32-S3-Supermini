#include "ble_service.h"

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    ServerCallbacks(BleManager* mgr, HIDManager* hid) : _mgr(mgr), _hid(hid) {}
    void onConnect(NimBLEServer* pServer) override {
        if (_hid) _hid->setBleConnected(true);
    }
    void onDisconnect(NimBLEServer* pServer) override {
        if (_hid) _hid->setBleConnected(false);
        NimBLEDevice::startAdvertising();
    }
private:
    BleManager* _mgr;
    HIDManager* _hid;
};

class OsSyncCallbacks : public NimBLECharacteristicCallbacks {
public:
    OsSyncCallbacks(BleManager* mgr) : _mgr(mgr) {}
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        // Forward to manager
    }
private:
    BleManager* _mgr;
};

BleManager::BleManager(StorageManager& storage, ZW111& sensor, HIDManager& hid)
    : _storage(storage), _sensor(sensor), _hid(hid) {}

void BleManager::begin() {
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max power
    NimBLEDevice::setSecurityAuth(true, true, true);

    _pServer = NimBLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(this, &_hid));

    // Custom Management Service
    NimBLEService* pService = _pServer->createService(BLE_SERVICE_UUID);

    // OS Sync Characteristic
    _pOsChar = pService->createCharacteristic(
        CHAR_OS_SYNC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    _pOsChar->setCallbacks(new NimBLECharacteristicCallbacks());

    // Vault Configuration Characteristic
    _pVaultChar = pService->createCharacteristic(
        CHAR_VAULT_UUID,
        NIMBLE_PROPERTY::WRITE
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

    pService->start();

    // Start Advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setAppearance(0x03C1); // Generic Keyboard
    pAdvertising->start();
}

void BleManager::loop() {
    // 1. Process incoming OS Sync write
    if (_pOsChar && _pOsChar->getValue().length() > 0) {
        std::string val = _pOsChar->getValue();
        handleOsSync(String(val.c_str()));
        _pOsChar->setValue("");
    }

    // 2. Process incoming Vault commands
    if (_pVaultChar && _pVaultChar->getValue().length() > 0) {
        std::string cmd = _pVaultChar->getValue();
        handleVaultCommand(String(cmd.c_str()));
        _pVaultChar->setValue("");
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

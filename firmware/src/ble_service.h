#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "config.h"
#include "storage.h"
#include "zw111.h"
#include "hid_manager.h"

class BleManager {
public:
    BleManager(StorageManager& storage, ZW111& sensor, HIDManager& hid);
    void begin();
    void loop();
    
    bool isConnected();
    void notifyStatus(const String& status);

private:
    StorageManager& _storage;
    ZW111& _sensor;
    HIDManager& _hid;
    
    NimBLEServer* _pServer = nullptr;
    NimBLECharacteristic* _pOsChar = nullptr;
    NimBLECharacteristic* _pVaultChar = nullptr;
    NimBLECharacteristic* _pEnrollChar = nullptr;
    NimBLECharacteristic* _pStatusChar = nullptr;
    
    bool _enrolling = false;
    uint16_t _enrollSlot = 1;
    
    void handleOsSync(const String& data);
    void handleVaultCommand(const String& cmd);
    void handleEnrollCommand(const String& cmd);
};

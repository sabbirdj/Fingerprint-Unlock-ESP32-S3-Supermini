#pragma once

#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

class HIDManager {
public:
    HIDManager();
    void begin();
    
    // Keystroke injection
    void typeString(const String& str, bool pressEnter = true);
    void pressKey(uint8_t key);
    
    // Status
    bool isUsbReady();
    bool isBleConnected();
    
    // BLE HID connection callback hooks
    void setBleConnected(bool connected);

private:
    USBHIDKeyboard _usbKeyboard;
    bool _bleConnected = false;
    NimBLECharacteristic* _inputReport = nullptr;
    
    void setupBleHid(NimBLEServer* pServer);
    void sendBleKey(uint8_t modifier, uint8_t keycode);
    void sendBleString(const String& str, bool pressEnter);
    uint8_t asciiToHid(char c, uint8_t* modifier);
};

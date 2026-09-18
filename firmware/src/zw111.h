#pragma once

#include <Arduino.h>
#include <functional>

// Confirmation Codes
#define ZW111_OK                  0x00
#define ZW111_PACKETRECIEVEERR    0x01
#define ZW111_NOFINGER            0x02
#define ZW111_IMAGEFAIL           0x03
#define ZW111_IMAGEMESS           0x06
#define ZW111_FEATUREFAIL         0x07
#define ZW111_NOMATCH             0x08
#define ZW111_NOTFOUND            0x09
#define ZW111_ENROLLMISMATCH      0x0A
#define ZW111_BADLOCATION         0x0B
#define ZW111_DBRANGEFAIL         0x0C

// LED Colors
enum ZW111_LEDColor : uint8_t {
    LED_RED     = 0x01,
    LED_BLUE    = 0x02,
    LED_GREEN   = 0x03,
    LED_YELLOW  = 0x04,
    LED_CYAN    = 0x05,
    LED_PURPLE  = 0x06,
    LED_WHITE   = 0x07
};

// LED Modes
enum ZW111_LEDMode : uint8_t {
    LED_BREATHING = 0x01,
    LED_FLASHING  = 0x02,
    LED_ON        = 0x03,
    LED_OFF       = 0x04
};

class ZW111 {
public:
    ZW111(HardwareSerial& serial, uint8_t rxPin, uint8_t txPin, uint8_t wakePin);
    
    bool begin(uint32_t baud = 57600);
    bool verifyPassword(uint32_t password = 0x00000000);
    
    // Core fingerprint functions
    int16_t scanAndMatch(uint16_t maxSlots = 40);
    bool enrollFingerprint(uint16_t slotId, std::function<void(const char*)> progressCb);
    bool deleteFingerprint(uint16_t slotId);
    bool clearDatabase();
    uint16_t getFingerprintCount();
    
    // Hardware Ring LED controls
    void setLed(ZW111_LEDMode mode, ZW111_LEDColor color, uint8_t speed = 100, uint8_t cycles = 1);
    
    // Touch detection
    bool isTouchDetected();

private:
    HardwareSerial& _serial;
    uint8_t _rxPin;
    uint8_t _txPin;
    uint8_t _wakePin;
    uint32_t _address = 0xFFFFFFFF;
    
    void sendPacket(uint8_t pid, const uint8_t* payload, uint16_t len);
    uint8_t receiveReply(uint8_t* replyPayload, uint16_t* replyLen, uint16_t timeoutMs = 1500);
    
    uint8_t getImage();
    uint8_t image2Tz(uint8_t slot);
    uint8_t createModel();
    uint8_t storeModel(uint8_t slot, uint16_t id);
    uint8_t searchDatabase(uint8_t slot, uint16_t startId, uint16_t count, uint16_t* foundId, uint16_t* score);
};

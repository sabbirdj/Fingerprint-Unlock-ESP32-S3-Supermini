#include "zw111.h"

#define EF01_START_CODE 0xEF01
#define PID_COMMAND     0x01
#define PID_ACK         0x07

ZW111::ZW111(HardwareSerial& serial, uint8_t rxPin, uint8_t txPin, uint8_t wakePin)
    : _serial(serial), _rxPin(rxPin), _txPin(txPin), _wakePin(wakePin) {}

bool ZW111::begin(uint32_t baud) {
    pinMode(_wakePin, INPUT);
    _serial.begin(baud, SERIAL_8N1, _rxPin, _txPin);
    delay(100);
    return verifyPassword();
}

bool ZW111::isTouchDetected() {
    return digitalRead(_wakePin) == HIGH;
}

void ZW111::sendPacket(uint8_t pid, const uint8_t* payload, uint16_t len) {
    uint16_t packetLen = len + 2; // Payload + 2-byte checksum
    uint16_t checksum = pid + (packetLen >> 8) + (packetLen & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        checksum += payload[i];
    }

    // Header
    _serial.write(0xEF);
    _serial.write(0x01);
    // Address
    _serial.write((uint8_t)(_address >> 24));
    _serial.write((uint8_t)(_address >> 16));
    _serial.write((uint8_t)(_address >> 8));
    _serial.write((uint8_t)(_address & 0xFF));
    // PID
    _serial.write(pid);
    // Length
    _serial.write((uint8_t)(packetLen >> 8));
    _serial.write((uint8_t)(packetLen & 0xFF));
    // Payload
    if (payload && len > 0) {
        _serial.write(payload, len);
    }
    // Checksum
    _serial.write((uint8_t)(checksum >> 8));
    _serial.write((uint8_t)(checksum & 0xFF));
}

uint8_t ZW111::receiveReply(uint8_t* replyPayload, uint16_t* replyLen, uint16_t timeoutMs) {
    uint32_t startTime = millis();
    uint8_t state = 0;
    uint16_t length = 0;
    uint16_t payloadIdx = 0;
    uint16_t receivedChecksum = 0;
    uint16_t computedChecksum = 0;

    while (millis() - startTime < timeoutMs) {
        while (_serial.available()) {
            uint8_t b = _serial.read();
            switch (state) {
                case 0: // Header high
                    if (b == 0xEF) state = 1;
                    break;
                case 1: // Header low
                    if (b == 0x01) state = 2;
                    else state = 0;
                    break;
                case 2: // Address 4 bytes
                case 3:
                case 4:
                case 5:
                    state++;
                    break;
                case 6: // PID (Ack = 0x07)
                    computedChecksum = b;
                    state = 7;
                    break;
                case 7: // Length high
                    length = (uint16_t)b << 8;
                    computedChecksum += b;
                    state = 8;
                    break;
                case 8: // Length low
                    length |= b;
                    computedChecksum += b;
                    if (length < 2) return ZW111_PACKETRECIEVEERR;
                    payloadIdx = 0;
                    state = 9;
                    break;
                case 9: // Payload
                    if (payloadIdx < (length - 2)) {
                        computedChecksum += b;
                        if (replyPayload && replyLen && payloadIdx < *replyLen) {
                            replyPayload[payloadIdx] = b;
                        }
                        payloadIdx++;
                    }
                    if (payloadIdx >= (length - 2)) {
                        state = 10;
                    }
                    break;
                case 10: // Checksum high
                    receivedChecksum = (uint16_t)b << 8;
                    state = 11;
                    break;
                case 11: // Checksum low
                    receivedChecksum |= b;
                    if (receivedChecksum == computedChecksum) {
                        if (replyLen) *replyLen = payloadIdx;
                        return (replyPayload && payloadIdx > 0) ? replyPayload[0] : ZW111_OK;
                    }
                    return ZW111_PACKETRECIEVEERR;
            }
        }
    }
    return ZW111_PACKETRECIEVEERR; // Timeout
}

bool ZW111::verifyPassword(uint32_t password) {
    uint8_t payload[5] = {
        0x13, // VfyPwd command
        (uint8_t)(password >> 24),
        (uint8_t)(password >> 16),
        (uint8_t)(password >> 8),
        (uint8_t)(password & 0xFF)
    };
    sendPacket(PID_COMMAND, payload, 5);
    uint8_t reply[16];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len) == ZW111_OK;
}

uint8_t ZW111::getImage() {
    uint8_t cmd = 0x01; // GenImg
    sendPacket(PID_COMMAND, &cmd, 1);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len);
}

uint8_t ZW111::image2Tz(uint8_t slot) {
    uint8_t payload[2] = {0x02, slot}; // Img2Tz
    sendPacket(PID_COMMAND, payload, 2);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len);
}

uint8_t ZW111::createModel() {
    uint8_t cmd = 0x03; // RegModel
    sendPacket(PID_COMMAND, &cmd, 1);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len);
}

uint8_t ZW111::storeModel(uint8_t slot, uint16_t id) {
    uint8_t payload[4] = {0x05, slot, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
    sendPacket(PID_COMMAND, payload, 4);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len);
}

uint8_t ZW111::searchDatabase(uint8_t slot, uint16_t startId, uint16_t count, uint16_t* foundId, uint16_t* score) {
    uint8_t payload[6] = {
        0x04, // Search
        slot,
        (uint8_t)(startId >> 8),
        (uint8_t)(startId & 0xFF),
        (uint8_t)(count >> 8),
        (uint8_t)(count & 0xFF)
    };
    sendPacket(PID_COMMAND, payload, 6);
    uint8_t reply[16];
    uint16_t len = sizeof(reply);
    uint8_t status = receiveReply(reply, &len);
    if (status == ZW111_OK && len >= 5) {
        if (foundId) *foundId = ((uint16_t)reply[1] << 8) | reply[2];
        if (score)   *score   = ((uint16_t)reply[3] << 8) | reply[4];
    }
    return status;
}

int16_t ZW111::scanAndMatch(uint16_t maxSlots) {
    // 1. Capture image
    uint8_t status = getImage();
    if (status != ZW111_OK) return -1;

    // 2. Convert to Character Buffer 1
    status = image2Tz(1);
    if (status != ZW111_OK) return -1;

    // 3. Search in flash database
    uint16_t matchedId = 0;
    uint16_t score = 0;
    status = searchDatabase(1, 1, maxSlots, &matchedId, &score);
    if (status == ZW111_OK && score >= 50) {
        return (int16_t)matchedId;
    }
    return -1;
}

bool ZW111::enrollFingerprint(uint16_t slotId, std::function<void(const char*)> progressCb) {
    setLed(LED_BREATHING, LED_BLUE, 50, 0);

    // Step 1: First scan
    if (progressCb) progressCb("PLACE_FINGER");
    uint32_t tStart = millis();
    while (getImage() != ZW111_OK) {
        if (millis() - tStart > 10000) return false;
        delay(50);
    }
    if (image2Tz(1) != ZW111_OK) return false;
    setLed(LED_FLASHING, LED_BLUE, 20, 2);

    // Step 2: Lift finger
    if (progressCb) progressCb("LIFT_FINGER");
    delay(1000);
    while (getImage() == ZW111_OK) {
        delay(100);
    }

    // Step 3: Second scan
    if (progressCb) progressCb("PLACE_AGAIN");
    tStart = millis();
    while (getImage() != ZW111_OK) {
        if (millis() - tStart > 10000) return false;
        delay(50);
    }
    if (image2Tz(2) != ZW111_OK) return false;

    // Step 4: RegModel & Store
    if (createModel() != ZW111_OK) {
        if (progressCb) progressCb("MISMATCH_FAIL");
        setLed(LED_FLASHING, LED_RED, 20, 3);
        return false;
    }

    if (storeModel(1, slotId) != ZW111_OK) {
        if (progressCb) progressCb("STORE_FAIL");
        setLed(LED_FLASHING, LED_RED, 20, 3);
        return false;
    }

    if (progressCb) progressCb("ENROLL_SUCCESS");
    setLed(LED_FLASHING, LED_GREEN, 20, 3);
    return true;
}

bool ZW111::deleteFingerprint(uint16_t slotId) {
    uint8_t payload[5] = {0x0C, (uint8_t)(slotId >> 8), (uint8_t)(slotId & 0xFF), 0x00, 0x01};
    sendPacket(PID_COMMAND, payload, 5);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len) == ZW111_OK;
}

bool ZW111::clearDatabase() {
    uint8_t cmd = 0x0D; // Empty
    sendPacket(PID_COMMAND, &cmd, 1);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    return receiveReply(reply, &len) == ZW111_OK;
}

uint16_t ZW111::getFingerprintCount() {
    uint8_t cmd = 0x1D; // TemplateNum
    sendPacket(PID_COMMAND, &cmd, 1);
    uint8_t reply[16];
    uint16_t len = sizeof(reply);
    if (receiveReply(reply, &len) == ZW111_OK && len >= 3) {
        return ((uint16_t)reply[1] << 8) | reply[2];
    }
    return 0;
}

void ZW111::setLed(ZW111_LEDMode mode, ZW111_LEDColor color, uint8_t speed, uint8_t cycles) {
    uint8_t payload[5] = {
        0x35, // AuraLEDConfig command
        (uint8_t)mode,
        speed,
        (uint8_t)color,
        cycles
    };
    sendPacket(PID_COMMAND, payload, 5);
    uint8_t reply[8];
    uint16_t len = sizeof(reply);
    receiveReply(reply, &len, 200);
}

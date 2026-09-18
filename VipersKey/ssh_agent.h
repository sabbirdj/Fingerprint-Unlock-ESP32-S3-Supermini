#pragma once
#include <Arduino.h>

class SSHAgent {
public:
    SSHAgent();
    bool begin();
    bool generateKey();
    String getPublicKeyHex();
    String signHashHex(const String& hashHex);

private:
    bool _hasKey;
    bool _hasPubKey;
    uint8_t _privKey[32];
    uint8_t _pubKey[65];
    
    void loadKey();
    void saveKey();
    void hexToBytes(const String& hex, uint8_t* bytes, size_t len);
    String bytesToHex(const uint8_t* bytes, size_t len);
};

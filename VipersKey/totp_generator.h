#pragma once

#include <Arduino.h>

class TotpGenerator {
public:
    static String getCode(const String& base32Secret, uint32_t unixTime);
private:
    static int decodeBase32(const char* base32, uint8_t* output, int maxLen);
};

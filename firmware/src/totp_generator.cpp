#include "totp_generator.h"
#include "mbedtls/md.h"

int TotpGenerator::decodeBase32(const char* base32, uint8_t* output, int maxLen) {
    int count = 0;
    int buffer = 0;
    int bitsLeft = 0;
    
    while (*base32 && count < maxLen) {
        char c = *base32++;
        uint8_t val = 0;
        if (c >= 'A' && c <= 'Z') val = c - 'A';
        else if (c >= 'a' && c <= 'z') val = c - 'a';
        else if (c >= '2' && c <= '7') val = c - '2' + 26;
        else continue; // Ignore '=' or whitespace
        
        buffer = (buffer << 5) | val;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            output[count++] = (buffer >> (bitsLeft - 8)) & 0xFF;
            bitsLeft -= 8;
            buffer &= (1 << bitsLeft) - 1; // Prevent integer overflow
        }
    }
    return count;
}

String TotpGenerator::getCode(const String& base32Secret, uint32_t unixTime) {
    uint8_t key[64];
    int keyLen = decodeBase32(base32Secret.c_str(), key, sizeof(key));
    if (keyLen == 0) return "000000"; // Invalid secret

    uint64_t timeStep = unixTime / 30;
    uint8_t counter[8];
    for (int i = 7; i >= 0; i--) {
        counter[i] = timeStep & 0xFF;
        timeStep >>= 8;
    }

    uint8_t mac[20];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, counter, 8);
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    int offset = mac[19] & 0x0F;
    uint32_t binary =
        ((mac[offset] & 0x7F) << 24) |
        ((mac[offset + 1] & 0xFF) << 16) |
        ((mac[offset + 2] & 0xFF) << 8) |
        (mac[offset + 3] & 0xFF);

    uint32_t code = binary % 1000000;
    char codeStr[7];
    snprintf(codeStr, sizeof(codeStr), "%06lu", code);
    return String(codeStr);
}

#include "ssh_agent.h"
#include <Preferences.h>
#include "esp_idf_version.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

Preferences sshPrefs;

SSHAgent::SSHAgent() : _hasKey(false), _hasPubKey(false) {}

bool SSHAgent::begin() {
    sshPrefs.begin("ssh_agent", false);
    loadKey();
    return _hasKey;
}

void SSHAgent::loadKey() {
    if (sshPrefs.getBytesLength("priv_key") == 32) {
        sshPrefs.getBytes("priv_key", _privKey, 32);
        _hasKey = true;
    }
    if (sshPrefs.getBytesLength("pub_key") == 65) {
        sshPrefs.getBytes("pub_key", _pubKey, 65);
        _hasPubKey = true;
    }
}

void SSHAgent::saveKey() {
    if (_hasKey) sshPrefs.putBytes("priv_key", _privKey, 32);
    if (_hasPubKey) sshPrefs.putBytes("pub_key", _pubKey, 65);
}

bool SSHAgent::generateKey() {
    mbedtls_ecdsa_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_ecdsa_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    const char* pers = "viper_ssh";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
    
    int ret = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret == 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        mbedtls_mpi_write_binary(&ctx.MBEDTLS_PRIVATE(d), _privKey, 32);
        size_t olen = 0;
        mbedtls_ecp_point_write_binary(&ctx.MBEDTLS_PRIVATE(grp), &ctx.MBEDTLS_PRIVATE(Q), MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, _pubKey, sizeof(_pubKey));
#else
        mbedtls_mpi_write_binary(&ctx.d, _privKey, 32);
        size_t olen = 0;
        mbedtls_ecp_point_write_binary(&ctx.grp, &ctx.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, _pubKey, sizeof(_pubKey));
#endif
        _hasKey = true;
        if (olen == 65) _hasPubKey = true;
        saveKey();
    }
    
    mbedtls_ecdsa_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ret == 0;
}

String SSHAgent::getPublicKeyHex() {
    if (_hasPubKey) {
        return bytesToHex(_pubKey, 65);
    }
    if (!_hasKey) return "";
    
    mbedtls_ecdsa_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ecdsa_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    const char* pers = "viper_pub";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    mbedtls_ecp_group_load(&ctx.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&ctx.MBEDTLS_PRIVATE(d), _privKey, 32);
    mbedtls_ecp_mul(&ctx.MBEDTLS_PRIVATE(grp), &ctx.MBEDTLS_PRIVATE(Q), &ctx.MBEDTLS_PRIVATE(d), &ctx.MBEDTLS_PRIVATE(grp).G, mbedtls_ctr_drbg_random, &ctr_drbg);
    size_t olen = 0;
    mbedtls_ecp_point_write_binary(&ctx.MBEDTLS_PRIVATE(grp), &ctx.MBEDTLS_PRIVATE(Q), MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, _pubKey, sizeof(_pubKey));
#else
    mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&ctx.d, _privKey, 32);
    mbedtls_ecp_mul(&ctx.grp, &ctx.Q, &ctx.d, &ctx.grp.G, mbedtls_ctr_drbg_random, &ctr_drbg);
    size_t olen = 0;
    mbedtls_ecp_point_write_binary(&ctx.grp, &ctx.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, _pubKey, sizeof(_pubKey));
#endif
    
    if (olen == 65) {
        _hasPubKey = true;
        saveKey();
    }
    
    String pubHex = bytesToHex(_pubKey, olen);
    mbedtls_ecdsa_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return pubHex;
}

String SSHAgent::signHashHex(const String& hashHex) {
    if (!_hasKey || hashHex.length() != 64) return "";
    
    uint8_t hash[32];
    hexToBytes(hashHex, hash, 32);
    
    mbedtls_ecdsa_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_ecdsa_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    const char* pers = "viper_sig";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
    
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    mbedtls_ecp_group_load(&ctx.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&ctx.MBEDTLS_PRIVATE(d), _privKey, 32);
    
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecdsa_sign(&ctx.MBEDTLS_PRIVATE(grp), &r, &s, &ctx.MBEDTLS_PRIVATE(d), hash, 32, mbedtls_ctr_drbg_random, &ctr_drbg);
#else
    mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&ctx.d, _privKey, 32);
    
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecdsa_sign(&ctx.grp, &r, &s, &ctx.d, hash, 32, mbedtls_ctr_drbg_random, &ctr_drbg);
#endif
    
    uint8_t sig[64];
    mbedtls_mpi_write_binary(&r, sig, 32);
    mbedtls_mpi_write_binary(&s, sig + 32, 32);
    
    String sigHex = bytesToHex(sig, 64);
    
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecdsa_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    
    return sigHex;
}

void SSHAgent::hexToBytes(const String& hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex.substring(i * 2, i * 2 + 2).c_str(), "%02hhx", &bytes[i]);
    }
}

String SSHAgent::bytesToHex(const uint8_t* bytes, size_t len) {
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", bytes[i]);
        hex += buf;
    }
    return hex;
}

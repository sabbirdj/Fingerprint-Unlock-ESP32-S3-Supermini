#include "storage.h"

#define PREF_NAMESPACE "viper_vault"

StorageManager::StorageManager() {}

void StorageManager::begin() {
    _prefs.begin(PREF_NAMESPACE, false);
    _activeOS = (OSType)_prefs.getUChar("active_os", (uint8_t)OS_WINDOWS);
}

const char* StorageManager::getOsKey(OSType os) {
    switch (os) {
        case OS_WINDOWS: return "pwd_win";
        case OS_LINUX:   return "pwd_lin";
        case OS_MACOS:   return "pwd_mac";
        case OS_TOTP:    return "totp_sec";
        default:         return "pwd_def";
    }
}

void StorageManager::setPassword(OSType os, const String& password) {
    _prefs.putString(getOsKey(os), password);
}

String StorageManager::getPassword(OSType os) {
    return _prefs.getString(getOsKey(os), "");
}

bool StorageManager::hasPassword(OSType os) {
    return _prefs.isKey(getOsKey(os));
}

void StorageManager::setActiveOS(OSType os) {
    _activeOS = os;
    _prefs.putUChar("active_os", (uint8_t)os);
}

OSType StorageManager::getActiveOS() {
    return _activeOS;
}

void StorageManager::setTotpSecret(const String& secret) {
    _prefs.putString("totp_sec", secret);
}

String StorageManager::getTotpSecret() {
    return _prefs.getString("totp_sec", "");
}

bool StorageManager::hasTotpSecret() {
    return _prefs.isKey("totp_sec");
}

void StorageManager::setSlotAction(uint16_t slotId, OSType action) {
    char key[16];
    snprintf(key, sizeof(key), "slot_%d", slotId);
    _prefs.putUChar(key, (uint8_t)action);
}

OSType StorageManager::getSlotAction(uint16_t slotId) {
    char key[16];
    snprintf(key, sizeof(key), "slot_%d", slotId);
    // Default: Return OS_UNKNOWN so it falls back to active OS
    return (OSType)_prefs.getUChar(key, (uint8_t)OS_UNKNOWN);
}

void StorageManager::clearAll() {
    _prefs.clear();
    _activeOS = OS_WINDOWS;
}

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class StorageManager {
public:
    StorageManager();
    void begin();
    
    // Operating System credentials
    void setPassword(OSType os, const String& password);
    String getPassword(OSType os);
    bool hasPassword(OSType os);
    
    // Active OS tracking
    void setActiveOS(OSType os);
    OSType getActiveOS();
    
    // TOTP Secret
    void setTotpSecret(const String& secret);
    String getTotpSecret();
    bool hasTotpSecret();
    
    // Fingerprint Slot Mapping (Optional: map specific finger IDs to specific actions)
    void setSlotAction(uint16_t slotId, OSType action);
    OSType getSlotAction(uint16_t slotId);

    // Factory reset
    void clearAll();

private:
    Preferences _prefs;
    OSType _activeOS = OS_WINDOWS;
    
    const char* getOsKey(OSType os);
};

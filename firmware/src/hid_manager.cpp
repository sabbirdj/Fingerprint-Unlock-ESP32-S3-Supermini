#include "hid_manager.h"

// Standard 101-Key HID Keyboard Report Descriptor with LED Output Report
static const uint8_t hidReportDescriptor[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,                    // USAGE (Keyboard)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x85, 0x01,                    //   REPORT_ID (1)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
    0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x08,                    //   REPORT_COUNT (8)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x81, 0x01,                    //   INPUT (Cnst,Ary,Abs)
    0x95, 0x05,                    //   REPORT_COUNT (5)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x05, 0x08,                    //   USAGE_PAGE (LEDs)
    0x19, 0x01,                    //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05,                    //   USAGE_MAXIMUM (Kana)
    0x91, 0x02,                    //   OUTPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x03,                    //   REPORT_SIZE (3)
    0x91, 0x01,                    //   OUTPUT (Cnst,Ary,Abs)
    0x95, 0x06,                    //   REPORT_COUNT (6)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
    0x19, 0x00,                    //   USAGE_MINIMUM (Reserved)
    0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
    0xc0                           // END_COLLECTION
};

HIDManager::HIDManager() {}

void HIDManager::begin() {
    // Start Native USB Keyboard
    _usbKeyboard.begin();
    USB.begin();
}

NimBLEHIDDevice* HIDManager::setupBleHid(NimBLEServer* pServer) {
    NimBLEHIDDevice* pHid = new NimBLEHIDDevice(pServer);
    pHid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
    pHid->setManufacturer("ViperKey");
    pHid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
    pHid->setHidInfo(0x00, 0x03);
    pHid->setBatteryLevel(100);
    _inputReport = pHid->getInputReport(1);
    pHid->getOutputReport(1);
    return pHid;
}

bool HIDManager::isUsbReady() {
    return USB;
}

bool HIDManager::isBleConnected() {
    return _bleConnected;
}

void HIDManager::setBleConnected(bool connected) {
    _bleConnected = connected;
}

void HIDManager::pressKey(uint8_t key) {
    if (_bleConnected) {
        sendBleKey(0, key);
    } else if (isUsbReady()) {
        _usbKeyboard.write(key);
    }
}

uint8_t HIDManager::asciiToHid(char c, uint8_t* modifier) {
    *modifier = 0;
    if (c >= 'a' && c <= 'z') {
        return 0x04 + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *modifier = 0x02; // Left Shift
        return 0x04 + (c - 'A');
    } else if (c >= '1' && c <= '9') {
        return 0x1E + (c - '1');
    } else if (c == '0') {
        return 0x27;
    } else if (c == ' ') {
        return 0x2C;
    } else if (c == '\n' || c == '\r') {
        return 0x28; // Enter
    } else if (c == '\t') {
        return 0x2B; // Tab
    } else if (c == '\b') {
        return 0x2A; // Backspace
    }

    // Special shifted characters
    *modifier = 0x02;
    switch (c) {
        case '!': return 0x1E;
        case '@': return 0x1F;
        case '#': return 0x20;
        case '$': return 0x21;
        case '%': return 0x22;
        case '^': return 0x23;
        case '&': return 0x24;
        case '*': return 0x25;
        case '(': return 0x26;
        case ')': return 0x27;
        case '_': return 0x2D;
        case '+': return 0x2E;
        case '{': return 0x2F;
        case '}': return 0x30;
        case '|': return 0x31;
        case ':': return 0x33;
        case '"': return 0x34;
        case '~': return 0x35;
        case '<': return 0x36;
        case '>': return 0x37;
        case '?': return 0x38;
        default:
            *modifier = 0;
            switch (c) {
                case '-': return 0x2D;
                case '=': return 0x2E;
                case '[': return 0x2F;
                case ']': return 0x30;
                case '\\': return 0x31;
                case ';': return 0x33;
                case '\'': return 0x34;
                case '`': return 0x35;
                case ',': return 0x36;
                case '.': return 0x37;
                case '/': return 0x38;
                default: return 0;
            }
    }
}

void HIDManager::sendBleKey(uint8_t modifier, uint8_t keycode) {
    if (!_inputReport || !_bleConnected) return;

    // Report: [modifier, reserved, key1, key2, key3, key4, key5, key6]
    uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
    _inputReport->notify(report, sizeof(report));
    delay(35);

    // Release key
    uint8_t releaseReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    _inputReport->notify(releaseReport, sizeof(releaseReport));
    delay(25);
}

void HIDManager::sendBleString(const String& str, bool pressEnter) {
    if (!_inputReport || !_bleConnected) return;

    if (pressEnter) {
        // Wake lock screen and focus password field (Space -> Ctrl+A+BS)
        sendBleKey(0, 0x2C); // Space (0x2C)
        delay(500); // Wait for Windows password field animation to focus

        // Ctrl+A -> Backspace to clear any garbage keystrokes
        sendBleKey(0x01, 0x04); // Left Ctrl (0x01) + 'a' (0x04)
        delay(50);
        sendBleKey(0, 0x2A); // Backspace (0x2A)
        delay(50);
    }

    // Type password keystrokes with solid BLE connection intervals
    for (size_t i = 0; i < str.length(); i++) {
        uint8_t mod = 0;
        uint8_t key = asciiToHid(str[i], &mod);
        if (key != 0) {
            sendBleKey(mod, key);
        }
    }

    // Submit password
    if (pressEnter) {
        delay(120);
        sendBleKey(0, 0x28); // Enter key
    }
}

void HIDManager::typeString(const String& str, bool pressEnter) {
    // 1. If Bluetooth is actively connected, prioritize Bluetooth typing
    if (_bleConnected) {
        sendBleString(str, pressEnter);
        return;
    }

    // 2. Otherwise fallback to native USB typing if USB is enumerated
    if (isUsbReady()) {
        if (pressEnter) {
            // Wake lock screen and focus password field (Win+L -> Space -> Ctrl+A+BS)
            _usbKeyboard.press(KEY_LEFT_GUI);
            _usbKeyboard.press('l');
            delay(50);
            _usbKeyboard.releaseAll();
            delay(300); // Wait for lock screen
            
            _usbKeyboard.write(' '); // Space to wake lock screen field
            delay(500); // Wait for animation

            // Ctrl+A -> Backspace to clear any garbage keystrokes
            _usbKeyboard.press(KEY_LEFT_CTRL);
            _usbKeyboard.press('a');
            delay(50);
            _usbKeyboard.releaseAll();
            delay(50);
            _usbKeyboard.write(KEY_BACKSPACE);
            delay(50);
        }

        // Type the password with small delays so Windows never drops keystrokes
        for (size_t i = 0; i < str.length(); i++) {
            _usbKeyboard.write(str[i]);
            delay(25);
        }

        // Press Enter to submit
        if (pressEnter) {
            delay(120);
            _usbKeyboard.write(KEY_RETURN);
        }
    }
}

#include "ble_config.h"
#include "app_control.h"
#include "common.h"
#include "module_registry.h"
#include "rgb.h"
#include "sleep_manager.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>

#define BLE_DEVICE_NAME "BottleLED"
#define BLE_SERVICE_UUID "8c0b8a10-7e3d-4df7-9a2a-1d8d46f8b100"
#define BLE_CONFIG_CHAR_UUID "8c0b8a11-7e3d-4df7-9a2a-1d8d46f8b100"
#define BLE_STATUS_CHAR_UUID "8c0b8a12-7e3d-4df7-9a2a-1d8d46f8b100"

static BLECharacteristic* s_status_char = nullptr;
static String s_pending_cmd;
static bool s_has_pending_cmd = false;
static bool s_client_connected = false;
static bool s_ble_enabled = false;

static bool extract_int(const String& json, const char* key, int* out) {
    String marker = String("\"") + key + "\"";
    int pos = json.indexOf(marker);
    if (pos < 0) return false;

    pos = json.indexOf(':', pos + marker.length());
    if (pos < 0) return false;
    pos++;

    while (pos < (int)json.length() && isspace((unsigned char)json[pos])) pos++;

    int end = pos;
    if (end < (int)json.length() && (json[end] == '-' || json[end] == '+')) end++;
    while (end < (int)json.length() && isdigit((unsigned char)json[end])) end++;
    if (end == pos) return false;

    *out = json.substring(pos, end).toInt();
    return true;
}

static bool extract_string(const String& json, const char* key, String* out) {
    String marker = String("\"") + key + "\"";
    int pos = json.indexOf(marker);
    if (pos < 0) return false;

    pos = json.indexOf(':', pos + marker.length());
    if (pos < 0) return false;
    pos++;

    while (pos < (int)json.length() && isspace((unsigned char)json[pos])) pos++;
    if (pos >= (int)json.length() || json[pos] != '"') return false;
    pos++;

    int end = json.indexOf('"', pos);
    if (end < 0) return false;

    *out = json.substring(pos, end);
    return true;
}

static String status_json() {
    uint32_t sleep_sec = s_idle_timeout_ms / 1000;
    String s = "{";
    s += "\"ok\":true";
    s += ",\"brightness\":" + String(brightness_max);
    s += ",\"sleep_sec\":" + String(sleep_sec);
    s += ",\"page\":" + String(page_index);
    s += ",\"subpage\":" + String(subpage_index);
    s += ",\"page_count\":" + String(app_get_page_count());
    s += ",\"modules\":" + module_registry_status_json();
    s += "}";
    return s;
}

static void set_status(const String& s) {
    if (!s_status_char) return;
    s_status_char->setValue(s.c_str());
    if (s_client_connected) {
        s_status_char->notify();
    }
}

static void draw_ble_icon(void) {
    FastLED.clear();
    rgb_set_brightness(brightness_max);

    const uint8_t blue = 48;
    const uint8_t cyan = 20;
    auto px = [&](uint8_t x, uint8_t y) {
        rgb_set(x, y, 0, cyan, blue);
    };

    // 8-row Bluetooth glyph, centered on the 17x8 panel.
    px(8, 0);
    px(8, 1); px(9, 1);
    px(5, 2); px(7, 2); px(8, 2); px(10, 2);
    px(6, 3); px(8, 3); px(9, 3);
    px(6, 4); px(8, 4); px(9, 4);
    px(5, 5); px(7, 5); px(8, 5); px(10, 5);
    px(8, 6); px(9, 6);
    px(8, 7);

    FastLED.show();
}

static void apply_command(const String& cmd) {
    int value = 0;

    if (extract_int(cmd, "brightness", &value)) {
        value = constrain(value, 0, 255);
        user_brightness_max = (uint8_t)value;
        brightness_max = user_brightness_max;
        rgb_set_brightness(brightness_max);
        save_config("brightness", user_brightness_max);
    }

    if (extract_int(cmd, "sleep_sec", &value)) {
        value = max(value, 0);
        s_idle_timeout_ms = (uint32_t)value * 1000UL;
        save_config("sleep_sec", value);
    }

    int new_page = page_index;
    int new_subpage = subpage_index;
    bool page_changed = false;

    if (extract_int(cmd, "page", &value)) {
        new_page = constrain(value, 0, app_get_page_count() - 1);
        page_changed = true;
    }

    if (extract_int(cmd, "subpage", &value)) {
        new_subpage = max(value, 0);
        if (!page_changed) {
            app_set_subpage(new_subpage);
        }
    }

    if (page_changed) {
        app_set_page(new_page, new_subpage);
    }

    int module_index = -1;
    if (extract_int(cmd, "module", &module_index) && extract_int(cmd, "enabled", &value)) {
        app_set_module_enabled(module_index, value != 0);
    }

    if (extract_int(cmd, "manifest", &module_index)) {
        set_status(module_registry_manifest_json(module_index));
        return;
    }

    String key;
    if (extract_string(cmd, "key", &key) && extract_int(cmd, "value", &value)) {
        save_config(key, value);
        if (key == "style") {
            app_set_subpage(value);
        }
    }

    set_status(status_json());
}

class ConfigServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        (void)server;
        s_client_connected = true;
        set_status(status_json());
    }

    void onDisconnect(BLEServer* server) override {
        s_client_connected = false;
        if (s_ble_enabled) {
            server->getAdvertising()->start();
        }
    }
};

class ConfigWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        String value = characteristic->getValue().c_str();
        if (value.length() == 0) return;

        s_pending_cmd = value;
        s_has_pending_cmd = true;
    }
};

void ble_config_init(void) {
    if (s_ble_enabled) return;

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ConfigServerCallbacks());

    BLEService* service = server->createService(BLE_SERVICE_UUID);
    BLECharacteristic* config_char = service->createCharacteristic(
        BLE_CONFIG_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    config_char->setCallbacks(new ConfigWriteCallbacks());

    s_status_char = service->createCharacteristic(
        BLE_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    s_status_char->setValue(status_json().c_str());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    BLEAdvertisementData adv_data;
    adv_data.setName(BLE_DEVICE_NAME);
    adv_data.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

    BLEAdvertisementData scan_data;
    scan_data.setName(BLE_DEVICE_NAME);

    advertising->setAdvertisementData(adv_data);
    advertising->setScanResponseData(scan_data);
    advertising->setName(BLE_DEVICE_NAME);
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->start();
    s_ble_enabled = true;
}

void ble_config_stop(void) {
    if (!s_ble_enabled) return;

    BLEDevice::getAdvertising()->stop();
    BLEDevice::deinit(true);
    s_status_char = nullptr;
    s_pending_cmd = "";
    s_has_pending_cmd = false;
    s_client_connected = false;
    s_ble_enabled = false;
}

void ble_config_update(void) {
    if (!s_ble_enabled) return;
    if (!s_has_pending_cmd) return;

    String cmd = s_pending_cmd;
    s_pending_cmd = "";
    s_has_pending_cmd = false;
    apply_command(cmd);
}

void ble_config_publish_status(void) {
    set_status(status_json());
}

bool ble_config_is_enabled(void) {
    return s_ble_enabled;
}

void ble_config_toggle(void) {
    if (s_ble_enabled) {
        ble_config_stop();
        FastLED.clear();
        FastLED.show();
    } else {
        ble_config_init();
        draw_ble_icon();
    }
}

void ble_config_render_mode(void) {
    if (!s_ble_enabled) return;
    draw_ble_icon();
}

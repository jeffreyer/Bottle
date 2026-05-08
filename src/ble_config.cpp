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
#include <SPIFFS.h>
#include <base64.h>

// 外部变量声明
extern uint32_t s_idle_timeout_ms;

#define BLE_DEVICE_NAME "BottleLED"
#define BLE_SERVICE_UUID "8c0b8a10-7e3d-4df7-9a2a-1d8d46f8b100"
#define BLE_CONFIG_CHAR_UUID "8c0b8a11-7e3d-4df7-9a2a-1d8d46f8b100"
#define BLE_STATUS_CHAR_UUID "8c0b8a12-7e3d-4df7-9a2a-1d8d46f8b100"

static BLECharacteristic* s_status_char = nullptr;
static String s_pending_cmd;
static bool s_has_pending_cmd = false;
static bool s_client_connected = false;
static bool s_ble_enabled = false;

// File upload state
static bool s_upload_in_progress = false;
static String s_upload_module_id;
static String s_upload_filename;
static size_t s_upload_size = 0;
static size_t s_upload_received = 0;
static File s_upload_file;

// Simple base64 decode table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t base64_decode(const char* input, size_t input_len, uint8_t* output) {
    size_t output_len = 0;
    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < input_len; i++) {
        if (input[i] == '=') break;

        int value = base64_decode_char(input[i]);
        if (value < 0) continue;

        buffer = (buffer << 6) | value;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output[output_len++] = (buffer >> bits) & 0xFF;
        }
    }

    return output_len;
}

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

static String status_json(bool include_modules = false) {
    uint32_t sleep_sec = s_idle_timeout_ms / 1000;
    String s = "{";
    s += "\"ok\":true";
    s += ",\"brightness\":" + String(brightness_max);
    s += ",\"sleep_sec\":" + String(sleep_sec);
    s += ",\"page\":" + String(page_index);
    s += ",\"subpage\":" + String(subpage_index);
    s += ",\"page_count\":" + String(app_get_page_count());
    if (include_modules) {
        s += ",\"modules\":" + module_registry_status_json();
    }
    s += "}";
    return s;
}

static void set_status(const String& s) {
    if (!s_status_char) return;

    // 直接发送，不再分包
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
    Serial.println("BLE: 收到命令: " + cmd);
    int value = 0;

    // 处理分页获取状态请求
    if (cmd.indexOf("\"get_status\"") >= 0) {
        int start_idx = 0;
        int idx_pos = cmd.indexOf("\"get_status\"");
        if (idx_pos >= 0) {
            int colon = cmd.indexOf(":", idx_pos);
            if (colon >= 0) {
                start_idx = cmd.substring(colon + 1).toInt();
            }
        }

        Serial.print("BLE: 请求状态，起始索引: ");
        Serial.println(start_idx);

        // 获取模块总数
        int total_modules = module_registry_count();
        Serial.print("BLE: 模块总数: ");
        Serial.println(total_modules);

        // 每次尝试发送的模块数（动态调整以保证 JSON < 实际 MTU）
        const int MAX_JSON_SIZE = 250;  // 实际 MTU 约 253 字节，留余量
        int modules_to_send = 5;  // 初始值，每个模块约 60 字节

        String status;
        bool has_more = false;

        // 尝试构建 JSON，如果超过限制则减少模块数
        while (modules_to_send > 0) {
            status = "{";

            // 第一次请求（start_idx == 0）包含全局信息
            if (start_idx == 0) {
                uint32_t sleep_sec = s_idle_timeout_ms / 1000;
                status += "\"ok\":true,";
                status += "\"brightness\":" + String(brightness_max) + ",";
                status += "\"sleep_sec\":" + String(sleep_sec) + ",";
                status += "\"page\":" + String(page_index) + ",";
                status += "\"subpage\":" + String(subpage_index) + ",";
                status += "\"page_count\":" + String(app_get_page_count()) + ",";
            }

            status += "\"modules\":[";

            int end_idx = min(start_idx + modules_to_send, total_modules);
            for (int i = start_idx; i < end_idx; i++) {
                if (i > start_idx) status += ",";

                const module_descriptor_t* m = module_registry_get(i);
                if (m) {
                    status += "{\"i\":" + String(i);
                    status += ",\"id\":\"" + String(m->id) + "\"";
                    status += ",\"n\":\"" + String(m->name) + "\"";
                    status += ",\"b\":" + String(m->built_in ? 1 : 0);
                    status += ",\"e\":" + String(module_registry_is_enabled(i) ? 1 : 0);
                    status += ",\"c\":" + String(m->config_count);
                    status += "}";
                }
            }

            status += "],";
            has_more = (end_idx < total_modules);
            status += "\"start_idx\":" + String(start_idx) + ",";
            status += "\"has_more\":" + String(has_more ? "true" : "false");
            status += "}";

            // 检查大小
            if (status.length() <= MAX_JSON_SIZE) {
                break;  // 大小合适，退出循环
            }

            // 太大了，减少模块数重试
            modules_to_send = max(1, modules_to_send - 1);

        }

        set_status(status);
        return;
    }

    // 处理文件上传开始
    if (cmd.indexOf("\"upload_start\"") >= 0) {
        String module_id, filename;
        int size = 0;

        // 简单解析 upload_start 对象
        int id_pos = cmd.indexOf("\"id\"");
        if (id_pos >= 0) {
            int start = cmd.indexOf("\"", id_pos + 5) + 1;
            int end = cmd.indexOf("\"", start);
            module_id = cmd.substring(start, end);
        }

        int file_pos = cmd.indexOf("\"file\"");
        if (file_pos >= 0) {
            int start = cmd.indexOf("\"", file_pos + 7) + 1;
            int end = cmd.indexOf("\"", start);
            filename = cmd.substring(start, end);
        }

        int size_pos = cmd.indexOf("\"size\"");
        if (size_pos >= 0) {
            int start = cmd.indexOf(":", size_pos) + 1;
            int end = cmd.indexOf(",", start);
            if (end < 0) end = cmd.indexOf("}", start);
            size = cmd.substring(start, end).toInt();
        }

        Serial.println("BLE: 开始接收文件上传");
        Serial.println("  模块ID: " + module_id);
        Serial.println("  文件名: " + filename);
        Serial.println("  大小: " + String(size));

        s_upload_in_progress = true;
        s_upload_module_id = module_id;
        s_upload_filename = filename;
        s_upload_size = size;
        s_upload_received = 0;

        // 打开文件准备写入
        String filepath = "/spiffs/" + filename;
        s_upload_file = SPIFFS.open(filepath.c_str(), FILE_WRITE);
        if (!s_upload_file) {
            Serial.println("BLE: 无法创建文件");
            s_upload_in_progress = false;
        }

        set_status("{\"ok\":true,\"upload_ready\":true}");
        return;
    }

    // 处理文件数据块
    if (cmd.indexOf("\"upload_chunk\"") >= 0 && s_upload_in_progress) {
        int data_pos = cmd.indexOf("\"data\"");
        if (data_pos >= 0) {
            int start = cmd.indexOf("\"", data_pos + 7) + 1;
            int end = cmd.indexOf("\"", start);
            String encoded = cmd.substring(start, end);

            // Base64 解码
            size_t max_decoded_len = (encoded.length() * 3) / 4 + 1;
            uint8_t* decoded = (uint8_t*)malloc(max_decoded_len);
            if (decoded) {
                size_t decoded_len = base64_decode(encoded.c_str(), encoded.length(), decoded);

                if (s_upload_file && decoded_len > 0) {
                    s_upload_file.write(decoded, decoded_len);
                    s_upload_received += decoded_len;

                    Serial.print("BLE: 接收进度: ");
                    Serial.print(s_upload_received);
                    Serial.print("/");
                    Serial.println(s_upload_size);
                }

                free(decoded);
            }
        }
        return;
    }

    // 处理上传完成
    if (cmd.indexOf("\"upload_complete\"") >= 0 && s_upload_in_progress) {
        Serial.println("BLE: 文件上传完成");

        if (s_upload_file) {
            s_upload_file.close();
        }

        Serial.println("BLE: 文件已保存: /spiffs/" + s_upload_filename);
        Serial.println("BLE: 接收字节数: " + String(s_upload_received));

        // 读取并打印文件内容用于调试
        String filepath = "/spiffs/" + s_upload_filename;
        File debugFile = SPIFFS.open(filepath.c_str(), FILE_READ);
        if (debugFile) {
            Serial.println("BLE: === 文件内容开始 ===");
            while (debugFile.available()) {
                Serial.write(debugFile.read());
            }
            Serial.println("\nBLE: === 文件内容结束 ===");
            debugFile.close();
        }

        s_upload_in_progress = false;
        String uploaded_module_id = s_upload_module_id;  // 保存模块ID
        s_upload_module_id = "";
        s_upload_filename = "";
        s_upload_size = 0;
        s_upload_received = 0;

        // 重新初始化模块注册表以加载新模块
        Serial.println("BLE: 重新加载模块注册表...");
        module_registry_init();

        // 查找并启用新上传的模块
        if (uploaded_module_id.length() > 0) {
            Serial.print("BLE: 查找并启用模块: ");
            Serial.println(uploaded_module_id);

            for (int i = 0; i < module_registry_count(); i++) {
                const module_descriptor_t* module = module_registry_get(i);
                if (module && String(module->id) == uploaded_module_id) {
                    Serial.print("BLE: 找到模块索引: ");
                    Serial.println(i);
                    app_set_module_enabled(i, true);
                    Serial.println("BLE: 模块已启用");
                    break;
                }
            }
        }

        set_status(status_json(true));  // 返回完整状态包含模块列表
        return;
    }

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
        Serial.print("BLE: 设置模块 ");
        Serial.print(module_index);
        Serial.print(" 启用状态为 ");
        Serial.println(value != 0);

        bool result = app_set_module_enabled(module_index, value != 0);

        Serial.print("BLE: 设置结果: ");
        Serial.println(result ? "成功" : "失败");
    }

    if (extract_int(cmd, "manifest", &module_index)) {
        set_status(module_registry_manifest_json(module_index));
        return;
    }

    // 添加删除模块功能
    if (extract_int(cmd, "delete_module", &module_index)) {
        Serial.print("BLE: 删除模块 ");
        Serial.println(module_index);

        if (module_index >= 0 && module_index < module_registry_count()) {
            const module_descriptor_t* module = module_registry_get((uint8_t)module_index);
            if (module) {
                if (module->built_in) {
                    // 内置模块只能禁用
                    Serial.println("BLE: 内置模块，只能禁用");
                    module_registry_set_enabled((uint8_t)module_index, false);
                } else {
                    // Lua模块可以物理删除
                    Serial.println("BLE: Lua模块，执行物理删除");
                    if (module->script_path) {
                        String path = String("/spiffs/") + module->script_path;
                        Serial.print("BLE: 尝试删除文件: ");
                        Serial.println(path);

                        if (SPIFFS.exists(path.c_str())) {
                            if (SPIFFS.remove(path.c_str())) {
                                Serial.println("BLE: 文件删除成功");
                            } else {
                                Serial.println("BLE: 文件删除失败");
                            }
                        } else {
                            Serial.println("BLE: 文件不存在");
                        }
                    }

                    // 重新加载模块注册表
                    Serial.println("BLE: 重新加载模块注册表...");
                    module_registry_init();
                }
            }
        }

        // 返回更新后的状态
        set_status(status_json(true));
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
        // 连接时立即发送状态
        String status = status_json();
        s_status_char->setValue(status.c_str());
        s_status_char->notify();
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
        Serial.println("BLE: 写入特征值，长度: " + String(value.length()));
        if (value.length() == 0) return;

        Serial.println("BLE: 接收到命令: " + value);
        s_pending_cmd = value;
        s_has_pending_cmd = true;
    }
};

class StatusReadCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* characteristic) override {
        // 当客户端读取状态时，更新并发送最新状态
        String status = status_json();
        characteristic->setValue(status.c_str());
        Serial.println("BLE: 状态被读取");
        Serial.println(status);
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

    // 设置特征值最大长度为 1024 字节（支持更多模块）
    BLEDescriptor* desc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
    s_status_char->addDescriptor(desc);

    // 设置读取回调
    s_status_char->setCallbacks(new StatusReadCallbacks());

    // 设置初始状态值
    String initial_status = status_json();
    s_status_char->setValue(initial_status.c_str());

    Serial.println("BLE: 初始化完成，初始状态:");
    Serial.println(initial_status);

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

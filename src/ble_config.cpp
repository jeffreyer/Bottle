#include "ble_config.h"
#include "ble_security.h"
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
#define BLE_PASSWORD_CHAR_UUID "8c0b8a13-7e3d-4df7-9a2a-1d8d46f8b100"
#define BLE_AUTH_CHAR_UUID "8c0b8a14-7e3d-4df7-9a2a-1d8d46f8b100"

static BLECharacteristic* s_status_char = nullptr;
static BLECharacteristic* s_password_char = nullptr;
static BLECharacteristic* s_auth_char = nullptr;
static BLEServer* s_ble_server = nullptr;
static BLEAdvertising* s_ble_advertising = nullptr;
static String s_pending_cmd;
static bool s_has_pending_cmd = false;
static bool s_client_connected = false;
static bool s_ble_enabled = false;
static bool s_ble_initialized = false;

// 安全管理器
static DeviceSecurity deviceSecurity;

// File upload state
static bool s_upload_in_progress = false;
static String s_upload_module_id;
static String s_upload_filename;
static size_t s_upload_size = 0;
static size_t s_upload_received = 0;
static File s_upload_file;

// Config definition upload state
static bool s_config_def_in_progress = false;
static String s_config_def_module_id;
static String s_config_def_buffer;
static size_t s_config_def_size = 0;
static size_t s_config_def_received = 0;

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

static bool extract_float(const String& json, const char* key, float* out) {
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
    if (end < (int)json.length() && json[end] == '.') {
        end++;
        while (end < (int)json.length() && isdigit((unsigned char)json[end])) end++;
    }
    if (end == pos) return false;

    *out = json.substring(pos, end).toFloat();
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
        // s += ",\"modules\":" + module_registry_status_json();
    }
    s += "}";
    return s;
}

static void set_status(const String& s) {
    if (!s_status_char) return;

    // 如果数据小于 300 字节，直接发送
    if (s.length() <= 500) {
        s_status_char->setValue(s.c_str());
        if (s_client_connected) {
            s_status_char->notify();
        }
        return;
    }

    const int chunk_size = 300;  // 保守值，确保包装后不超过 MTU
    const char* str = s.c_str();
    int str_len = s.length();
    int pos = 0;
    int chunk_index = 0;

    // 先计算总分块数
    int total_chunks = 0;
    int temp_pos = 0;
    while (temp_pos < str_len) {
        int remaining = str_len - temp_pos;
        int len = min(chunk_size, remaining);

        // 调整到 UTF-8 字符边界
        if (temp_pos + len < str_len) {
            while (len > 0 && ((unsigned char)str[temp_pos + len] & 0xC0) == 0x80) {
                len--;
            }
        }

        temp_pos += len;
        total_chunks++;
    }
    // 发送分块
    while (pos < str_len) {
        int remaining = str_len - pos;
        int len = min(chunk_size, remaining);

        // 调整到 UTF-8 字符边界：向前回退到非后续字节
        if (pos + len < str_len) {
            while (len > 0 && ((unsigned char)str[pos + len] & 0xC0) == 0x80) {
                len--;
            }
        }

        // 提取分块数据
        String chunk = "";
        for (int i = 0; i < len; i++) {
            chunk += str[pos + i];
        }

        // 包装成分块格式: {"chunk":0,"total":3,"data":"..."}
        String packet = "{\"chunk\":" + String(chunk_index) +
                       ",\"total\":" + String(total_chunks) +
                       ",\"data\":\"";

        // 转义 JSON 字符串
        int escaped_count = 0;
        for (int j = 0; j < chunk.length(); j++) {
            char c = chunk[j];
            if (c == '"' || c == '\\') {
                packet += '\\';
                escaped_count++;
            }
            packet += c;
        }
        packet += "\"}";

        if (packet.length() > 512) {
            Serial.print("BLE: 错误！分块过大: ");
            Serial.println(packet.length());
            pos += len;
            chunk_index++;
            continue;
        }

        s_status_char->setValue(packet.c_str());
        if (s_client_connected) {
            s_status_char->notify();
        }

        pos += len;
        chunk_index++;
        delay(150);
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

        // 获取模块总数
        int total_modules = module_registry_count();

        // 每次尝试发送的模块数（动态调整以保证 JSON < 实际 MTU）
        const int MAX_JSON_SIZE = 500;  // 实际 MTU 约 253 字节，留余量
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

                }

                free(decoded);
            }
        }
        return;
    }

    // 处理上传完成
    if (cmd.indexOf("\"upload_complete\"") >= 0 && s_upload_in_progress) {

        if (s_upload_file) {
            s_upload_file.close();
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

            for (int i = 0; i < module_registry_count(); i++) {
                const module_descriptor_t* module = module_registry_get(i);
                if (module && String(module->id) == uploaded_module_id) {
                    app_set_module_enabled(i, true);
                    break;
                }
            }
        }

        set_status(status_json(false));  // 不返回模块列表，小程序端会主动请求
        return;
    }

    // 处理模块配置定义
    if (cmd.indexOf("\"module_config_def\"") >= 0) {
        // 提取模块ID
        int id_pos = cmd.indexOf("\"id\"");
        String module_id = "";
        if (id_pos >= 0) {
            int start = cmd.indexOf("\"", id_pos + 5) + 1;
            int end = cmd.indexOf("\"", start);
            module_id = cmd.substring(start, end);
        }

        if (module_id.length() > 0) {
            // 提取配置定义（config数组）
            int config_pos = cmd.indexOf("\"config\"");
            if (config_pos >= 0) {
                int start = cmd.indexOf("[", config_pos);
                int end = cmd.lastIndexOf("]");
                if (start >= 0 && end > start) {

                    String config_json = cmd.substring(start, end + 1);

                    // 存储配置定义到NVS (使用短键名以符合NVS 15字符限制)
                    String key = "cfg_" + module_id;
                    save_config_string(key, config_json);

                }
            }
        }

        set_status("{\"ok\":true}");
        return;
    }

    // 处理配置定义分段上传开始
    if (cmd.indexOf("\"config_def_start\"") >= 0) {

        int id_pos = cmd.indexOf("\"id\"");
        String module_id = "";
        if (id_pos >= 0) {
            int start = cmd.indexOf("\"", id_pos + 5) + 1;
            int end = cmd.indexOf("\"", start);
            module_id = cmd.substring(start, end);
        }

        int size = 0;
        int size_pos = cmd.indexOf("\"size\"");
        if (size_pos >= 0) {
            int start = cmd.indexOf(":", size_pos) + 1;
            int end = cmd.indexOf(",", start);
            if (end < 0) end = cmd.indexOf("}", start);
            size = cmd.substring(start, end).toInt();
        }

        s_config_def_in_progress = true;
        s_config_def_module_id = module_id;
        s_config_def_size = size;
        s_config_def_received = 0;
        s_config_def_buffer = "";
        s_config_def_buffer.reserve(size + 100);

        set_status("{\"ok\":true}");
        return;
    }

    // 处理配置定义数据块
    if (cmd.indexOf("\"config_def_chunk\"") >= 0) {

        if (!s_config_def_in_progress) {
            Serial.println("BLE: 错误 - 未收到 config_def_start，忽略此数据块");
            set_status("{\"ok\":true}");
            return;
        }

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

                if (decoded_len > 0) {
                    for (size_t i = 0; i < decoded_len; i++) {
                        s_config_def_buffer += (char)decoded[i];
                    }
                    s_config_def_received += decoded_len;

                }

                free(decoded);
            }
        }
        return;
    }

    // 处理配置定义上传完成
    if (cmd.indexOf("\"config_def_complete\"") >= 0 && s_config_def_in_progress) {

        // 存储配置定义到NVS (使用短键名以符合NVS 15字符限制)
        String key = "cfg_" + s_config_def_module_id;
        save_config_string(key, s_config_def_buffer);

        s_config_def_in_progress = false;
        s_config_def_buffer = "";

        // 重新加载模块注册表以更新 config_count
        Serial.println("BLE: 重新加载模块注册表以更新配置项数量...");
        module_registry_init();

        set_status(status_json(false));  // 不返回模块列表，小程序端会主动请求
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
        bool result = app_set_module_enabled(module_index, value != 0);

    }

    if (extract_int(cmd, "manifest", &module_index)) {
        set_status(module_registry_manifest_json(module_index));
        return;
    }

    // 添加删除模块功能
    if (extract_int(cmd, "delete_module", &module_index)) {
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

    // 统一配置保存处理（根据type字段）
    String key, type;
    if (extract_string(cmd, "key", &key) && extract_string(cmd, "type", &type)) {
        Serial.print("BLE: Saving config - key: ");
        Serial.print(key);
        Serial.print(", type: ");
        Serial.print(type);

        // 分离命名空间和键名
        int separator = key.indexOf('_');
        String ns = separator > 0 ? key.substring(0, separator) : "";
        String actual_key = separator > 0 ? key.substring(separator + 1) : key;

        if (separator > 0) {
            Serial.print(", ns: ");
            Serial.print(ns);
            Serial.print(", actual_key: ");
            Serial.println(actual_key);
        } else {
            Serial.println();
        }

        // 根据类型保存
        if (type == "switch") {
            int int_value;
            if (extract_int(cmd, "value", &int_value)) {
                Serial.print("BLE: Value (int): ");
                Serial.println(int_value);
                if (separator > 0) {
                    save_config_ns(ns, actual_key, int_value);
                } else {
                    save_config(key, int_value);
                }
            }
        } else if (type == "select") {
            // select 类型可能是整数或字符串，先尝试整数
            int int_value;
            if (extract_int(cmd, "value", &int_value)) {
                Serial.print("BLE: Value (int): ");
                Serial.println(int_value);
                if (separator > 0) {
                    save_config_ns(ns, actual_key, int_value);
                } else {
                    save_config(key, int_value);
                }
            } else {
                // 尝试字符串
                String string_value;
                if (extract_string(cmd, "value", &string_value)) {
                    Serial.print("BLE: Value (string): ");
                    Serial.println(string_value);
                    if (separator > 0) {
                        save_config_string_ns(ns, actual_key, string_value);
                    } else {
                        save_config_string(key, string_value);
                    }
                }
            }
        } else if (type == "slider" || type == "number") {
            float float_value;
            if (extract_float(cmd, "value", &float_value)) {
                Serial.print("BLE: Value (float): ");
                Serial.println(float_value);
                // 整数值保存为int，小数保存为float
                if (float_value == (int)float_value) {
                    if (separator > 0) {
                        save_config_ns(ns, actual_key, (int)float_value);
                    } else {
                        save_config(key, (int)float_value);
                    }
                } else {
                    if (separator > 0) {
                        save_config_float_ns(ns, actual_key, float_value);
                    } else {
                        save_config_float(key, float_value);
                    }
                }
            }
        } else if (type == "text" || type == "color") {
            String string_value;
            if (extract_string(cmd, "value", &string_value)) {
                Serial.print("BLE: Value (string): ");
                Serial.println(string_value);
                if (separator > 0) {
                    save_config_string_ns(ns, actual_key, string_value);
                } else {
                    save_config_string(key, string_value);
                }
            }
        }

        set_status(status_json());
        return;
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
        // 断开连接时重置认证状态
        deviceSecurity.resetAuth();
        if (s_ble_enabled) {
            server->getAdvertising()->start();
        }
    }
};

class ConfigWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        // 检查是否已认证
        if (!deviceSecurity.checkAuthenticated()) {
            Serial.println("BLE: Unauthorized access blocked!");
            return;
        }

        String value = characteristic->getValue().c_str();
        if (value.length() == 0) return;

        s_pending_cmd = value;
        s_has_pending_cmd = true;
    }
};

class StatusReadCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* characteristic) override {
        // 当客户端读取状态时，更新并发送最新状态
        String status = status_json();
        characteristic->setValue(status.c_str());
    }
};

// 密码特征值回调（读取设备密码）
class PasswordReadCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* characteristic) override {
        String password = deviceSecurity.getPassword();
        characteristic->setValue(password.c_str());
        Serial.println("BLE: Password read by client");
    }
};

// 认证特征值回调（验证密码）
class AuthWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        String inputPassword = characteristic->getValue().c_str();

        // 处理 SET_BOUND 命令（绑定成功后设置绑定状态）
        if (inputPassword == "SET_BOUND") {
            if (!deviceSecurity.checkAuthenticated()) {
                Serial.println("BLE: SET_BOUND rejected - not authenticated");
                characteristic->setValue("UNAUTHORIZED");
                characteristic->notify();
                return;
            }

            deviceSecurity.setBound(true);
            characteristic->setValue("OK");
            characteristic->notify();
            Serial.println("BLE: Device bound successfully");
            return;
        }

        // 处理 UNBIND 命令（解绑设备）
        if (inputPassword == "UNBIND") {
            if (!deviceSecurity.checkAuthenticated()) {
                Serial.println("BLE: UNBIND rejected - not authenticated");
                characteristic->setValue("UNAUTHORIZED");
                characteristic->notify();
                return;
            }

            deviceSecurity.unbind();
            characteristic->setValue("OK");
            characteristic->notify();
            Serial.println("BLE: Device unbound successfully");
            return;
        }

        // 正常的密码验证流程
        bool verified = deviceSecurity.verifyPassword(inputPassword);

        // 返回验证结果
        String result = verified ? "OK" : "FAIL";
        characteristic->setValue(result.c_str());
        characteristic->notify();

        if (verified) {
            Serial.println("BLE: Client authenticated successfully");
        } else {
            Serial.println("BLE: Client authentication failed");
        }
    }
};

void ble_config_init(void) {
    if (s_ble_enabled) {
        Serial.println("BLE: Already enabled, skipping");
        return;
    }

    // 第一次初始化：创建BLE栈和服务
    if (!s_ble_initialized) {
        Serial.println("BLE: First-time initialization - creating BLE stack...");

        // 初始化安全管理器
        deviceSecurity.init();

        BLEDevice::init(BLE_DEVICE_NAME);

        // 设置 MTU 大小
        BLEDevice::setMTU(517);  // 517 是 BLE 的最大 MTU (512 + 5 字节头部)
        Serial.println("BLE: MTU set to 517");

        Serial.println("BLE: Creating server...");
        s_ble_server = BLEDevice::createServer();
        s_ble_server->setCallbacks(new ConfigServerCallbacks());

        Serial.println("BLE: Creating service...");
        BLEService* service = s_ble_server->createService(BLE_SERVICE_UUID);
        BLECharacteristic* config_char = service->createCharacteristic(
            BLE_CONFIG_CHAR_UUID,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
        );
        config_char->setCallbacks(new ConfigWriteCallbacks());

        s_status_char = service->createCharacteristic(
            BLE_STATUS_CHAR_UUID,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
        );

        BLEDescriptor* desc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
        s_status_char->addDescriptor(desc);
        s_status_char->setCallbacks(new StatusReadCallbacks());

        String initial_status = status_json();
        s_status_char->setValue(initial_status.c_str());

        // 创建密码特征值（读取设备密码）
        s_password_char = service->createCharacteristic(
            BLE_PASSWORD_CHAR_UUID,
            BLECharacteristic::PROPERTY_READ
        );
        s_password_char->setCallbacks(new PasswordReadCallbacks());
        s_password_char->setValue(deviceSecurity.getPassword().c_str());

        // 创建认证特征值（验证密码）
        s_auth_char = service->createCharacteristic(
            BLE_AUTH_CHAR_UUID,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
        );
        s_auth_char->setCallbacks(new AuthWriteCallbacks());

        Serial.println("BLE: Starting service...");
        service->start();

        Serial.println("BLE: Configuring advertising...");
        s_ble_advertising = BLEDevice::getAdvertising();
        BLEAdvertisementData adv_data;
        adv_data.setName(BLE_DEVICE_NAME);
        adv_data.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

        BLEAdvertisementData scan_data;
        scan_data.setName(BLE_DEVICE_NAME);

        s_ble_advertising->setAdvertisementData(adv_data);
        s_ble_advertising->setScanResponseData(scan_data);
        s_ble_advertising->setName(BLE_DEVICE_NAME);
        s_ble_advertising->addServiceUUID(BLE_SERVICE_UUID);
        s_ble_advertising->setScanResponse(true);

        s_ble_initialized = true;
        Serial.println("BLE: Stack initialized");
    } else {
        Serial.println("BLE: Stack already initialized, reusing...");
    }

    // 启动广播
    Serial.println("BLE: Starting advertising...");
    s_ble_advertising->start();
    s_ble_enabled = true;
    Serial.println("BLE: Advertising started");
}

void ble_config_stop(void) {
    if (!s_ble_enabled) {
        Serial.println("BLE: Already disabled");
        return;
    }

    Serial.println("BLE: Stopping BLE...");

    // 主动断开所有客户端连接
    if (s_ble_server && s_client_connected) {
        Serial.println("BLE: Disconnecting clients...");
        s_ble_server->disconnect(s_ble_server->getConnId());
        s_client_connected = false;
        delay(100); // 等待断开完成
    }

    // 停止广播
    Serial.println("BLE: Stopping advertising...");
    if (s_ble_advertising) {
        s_ble_advertising->stop();
    }

    // 让蓝牙进入休眠模式
    Serial.println("BLE: Entering sleep mode...");
    BLEDevice::deinit(false); // false表示不释放内存，保留配置以便快速重启

    // 清理状态
    s_pending_cmd = "";
    s_has_pending_cmd = false;
    s_client_connected = false;
    s_ble_enabled = false;
    s_ble_initialized = false; // 标记为未初始化，下次需要重新初始化

    Serial.println("BLE: BLE stopped and entered sleep mode");
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
    Serial.print("BLE: toggle called, current state: ");
    Serial.println(s_ble_enabled ? "enabled" : "disabled");

    if (s_ble_enabled) {
        Serial.println("BLE: Stopping BLE...");
        ble_config_stop();
        FastLED.clear();
        FastLED.show();
        Serial.println("BLE: BLE stopped");
    } else {
        Serial.println("BLE: Starting BLE...");
        ble_config_init();
        draw_ble_icon();
        Serial.println("BLE: BLE started");
    }
}

void ble_config_render_mode(void) {
    if (!s_ble_enabled) return;
    draw_ble_icon();
}

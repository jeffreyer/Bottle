#include "module_registry.h"
#include "common.h"
#include "candle.h"
#include "rhythm_lua.h"
#include "sandglass.h"
#include "sim_manager.h"
#include "lua_hardware_api.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <vector>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#define MAX_DYNAMIC_MODULES 20

// Built-in modules (static) - configs now stored in NVS as JSON
// 移除 const 以便在初始化时更新 config_count
static module_descriptor_t k_builtin_modules[] = {
  {"rhythm", "音乐律动", "3.0.0", "Bottle", "Lua-powered audio spectrum visualizer", "lua", "lua-5.4.7", nullptr, setup_rhythm_lua_module, unload_rhythm_lua_module, loop_rhythm_lua_module, nullptr, 0, true},
  {"water", "海洋流体", "1.0.0", "Bottle", "Gravity liquid simulation", "native", "native", nullptr, setup_fluid, unload_fluid, fluid_loop, nullptr, 0, true},
  {"candle", "蜡烛焰火", "1.0.0", "Bottle", "Gravity-aware candle flame", "native", "native", nullptr, setup_candle, unload_candle, candle_loop, nullptr, 0, true},
  {"sandglass", "沙漏", "1.0.0", "Bottle", "Gravity sandglass", "native", "native", nullptr, setup_sand, unload_sand, sand_loop, nullptr, 0, true},
};

// Dynamic modules storage
static module_descriptor_t s_dynamic_modules[MAX_DYNAMIC_MODULES];
static uint8_t s_dynamic_module_count = 0;

// Combined module list
static const module_descriptor_t* s_all_modules[sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]) + MAX_DYNAMIC_MODULES];
static uint8_t s_total_module_count = 0;

// Enabled state for all modules
static bool s_enabled[sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]) + MAX_DYNAMIC_MODULES];

// Dynamic module string storage (for id, name, etc.)
// 增加缓冲区大小以容纳 UTF-8 中文字符（每个中文字符占3字节）
static char s_dynamic_strings[MAX_DYNAMIC_MODULES][6][128]; // 6 strings per module, 128 chars each

// Lua state for each dynamic module
static lua_State* s_dynamic_lua_states[MAX_DYNAMIC_MODULES];
static bool s_dynamic_lua_loaded[MAX_DYNAMIC_MODULES];

// Forward declarations for dynamic Lua module functions
static int dynamic_lua_setup(void);
static int dynamic_lua_loop(void);
static int dynamic_lua_unload(void);

// Current dynamic module being executed
static int s_current_dynamic_module = -1;

static String json_escape(const char* value) {
  String s;
  if (!value) return s;
  while (*value) {
    if (*value == '"' || *value == '\\') {
      s += '\\';
    }
    s += *value;
    value++;
  }
  return s;
}

// 初始化Native模块的配置定义（首次运行时保存到NVS）
static void init_native_module_configs() {
  // Rhythm module config (18 items)
  String rhythm_config = "[";
  rhythm_config += "{\"type\":\"select\",\"key\":\"style\",\"label\":\"风格\",\"default\":0,";
  rhythm_config += "\"options\":[{\"label\":\"绿峰\",\"value\":0},{\"label\":\"彩虹\",\"value\":1},{\"label\":\"分裂\",\"value\":2},{\"label\":\"流动\",\"value\":3}]}";
  rhythm_config += ",{\"type\":\"slider\",\"key\":\"sensitivity\",\"label\":\"灵敏度\",\"min\":1,\"max\":50,\"step\":1,\"default\":12}";

  // 17 frequency band gains
  const char* band_labels[] = {
    "低音0", "低音1", "低音2", "低音3",
    "中低4", "中低5", "中音6", "中音7",
    "中音8", "中音9", "中高10", "中高11",
    "高音12", "高音13", "高音14", "高音15",
    "超高16"
  };
  const int band_defaults[] = {4, 5, 5, 5, 6, 8, 11, 11, 15, 17, 30, 34, 36, 36, 38, 38, 10};

  for (int i = 0; i < 17; i++) {
    rhythm_config += ",{\"type\":\"slider\",\"key\":\"band_" + String(i) + "\",\"label\":\"" + String(band_labels[i]) + "\"";
    rhythm_config += ",\"min\":0,\"max\":100,\"step\":1,\"default\":" + String(band_defaults[i]) + "}";
  }
  rhythm_config += "]";

  String saved;
  String rhythm_key = "cfg_rhythm";
  if (load_config_string(rhythm_key).length() == 0) {
    Serial.print("Module registry: Saving rhythm config (");
    Serial.print(rhythm_config.length());
    Serial.println(" bytes)");
    save_config_string(rhythm_key, rhythm_config);
    saved = load_config_string(rhythm_key);
    Serial.print("Module registry: Verified rhythm config length: ");
    Serial.println(saved.length());
  }

  // Water module config
  String water_config = "[";
  water_config += "{\"type\":\"switch\",\"key\":\"color_custom\",\"label\":\"自定义颜色\",\"default\":0}";
  water_config += ",{\"type\":\"color\",\"key\":\"color_hue\",\"label\":\"颜色\",\"default\":\"#00FFAE\"}";
  water_config += ",{\"type\":\"switch\",\"key\":\"dynamic_tide\",\"label\":\"动态水位\",\"default\":0}";
  water_config += ",{\"type\":\"slider\",\"key\":\"fixed_level\",\"label\":\"固定水位高度\",\"min\":1,\"max\":100,\"step\":1,\"default\":70,\"unit\":\"%\"}";
  water_config += ",{\"type\":\"slider\",\"key\":\"tide_peak\",\"label\":\"涨潮峰值时间\",\"min\":0,\"max\":23,\"step\":1,\"default\":12,\"unit\":\"时\"}";
  water_config += "]";

  String water_key = "cfg_water";
  String existing = load_config_string(water_key);
  Serial.print("Module registry: cfg_water existing length: ");
  Serial.println(existing.length());
  if (existing.length() == 0) {
    Serial.print("Module registry: Saving water config (");
    Serial.print(water_config.length());
    Serial.println(" bytes)");
    save_config_string(water_key, water_config);
    // 验证保存
    saved = load_config_string(water_key);
    Serial.print("Module registry: Verified water config length: ");
    Serial.println(saved.length());
  }

  // Candle module config
  String candle_config = "[";
  candle_config += "{\"type\":\"slider\",\"key\":\"candle_index\",\"label\":\"Color\",\"min\":0,\"max\":99,\"step\":1,\"default\":0}";
  candle_config += "]";

  String candle_key = "cfg_candle";
  if (load_config_string(candle_key).length() == 0) {
    Serial.print("Module registry: Saving candle config (");
    Serial.print(candle_config.length());
    Serial.println(" bytes)");
    save_config_string(candle_key, candle_config);
    saved = load_config_string(candle_key);
    Serial.print("Module registry: Verified candle config length: ");
    Serial.println(saved.length());
  }

  // Sandglass module config
  String sand_config = "[";
  sand_config += "{\"type\":\"slider\",\"key\":\"sand_speed\",\"label\":\"Speed\",\"min\":1,\"max\":10,\"step\":1,\"default\":5}";
  sand_config += "]";

  String sand_key = "cfg_sandglass";
  if (load_config_string(sand_key).length() == 0) {
    Serial.print("Module registry: Saving sandglass config (");
    Serial.print(sand_config.length());
    Serial.println(" bytes)");
    save_config_string(sand_key, sand_config);
    saved = load_config_string(sand_key);
    Serial.print("Module registry: Verified sandglass config length: ");
    Serial.println(saved.length());
  }
}

// 更新所有模块的 config_count（从NVS读取配置定义）
static void update_module_config_counts() {
  uint8_t builtin_count = sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]);

  // 更新 builtin 模块的 config_count
  for (uint8_t i = 0; i < builtin_count; i++) {
    module_descriptor_t* module = &k_builtin_modules[i];
    if (module->id != nullptr && strlen(module->id) > 0) {
      String config_def_key = String("cfg_") + module->id;
      String config_def = load_config_string(config_def_key);
      if (config_def.length() > 0) {
        // 计算配置项数量
        int count = 0;
        int pos = 0;
        while ((pos = config_def.indexOf("\"key\"", pos)) >= 0) {
          count++;
          pos += 5;
        }
        module->config_count = count;
        Serial.print("Module registry: Updated ");
        Serial.print(module->id);
        Serial.print(" config_count = ");
        Serial.println(count);
      }
    }
  }
}

static String config_type_name(module_config_type_t type) {
  switch (type) {
    case MODULE_CONFIG_BOOL: return "bool";
    case MODULE_CONFIG_SELECT: return "select";
    case MODULE_CONFIG_INT:
    default: return "int";
  }
}

// Parse metadata from Lua file comments
// Format: -- @key: value
static void parse_lua_metadata(const String& content, module_descriptor_t* module, uint8_t module_idx) {
  String id, name, version, author, description;

  // Default values
  name = "Unknown";
  version = "1.0.0";
  author = "Unknown";
  description = "Lua module";

  // Parse metadata from comments
  int pos = 0;
  while (pos < (int)content.length()) {
    int line_end = content.indexOf('\n', pos);
    if (line_end < 0) line_end = content.length();

    String line = content.substring(pos, line_end);
    line.trim();

    if (line.startsWith("-- @name:")) {
      name = line.substring(9);
      name.trim();
    } else if (line.startsWith("-- @version:")) {
      version = line.substring(12);
      version.trim();
    } else if (line.startsWith("-- @author:")) {
      author = line.substring(11);
      author.trim();
    } else if (line.startsWith("-- @description:")) {
      description = line.substring(16);
      description.trim();
    } else if (line.startsWith("-- @id:")) {
      id = line.substring(7);
      id.trim();
    }

    // Stop parsing after first non-comment line
    if (!line.startsWith("--") && line.length() > 0) {
      break;
    }

    pos = line_end + 1;
  }

  // Store strings in static storage
  if (id.length() == 0) {
    // Generate ID from filename if not specified
    id = String(module->script_path);
    id.replace(".lua", "");
  }

  // 使用 snprintf 确保字符串以 null 结尾，缓冲区大小为 128 字节
  snprintf(s_dynamic_strings[module_idx][0], 128, "%s", id.c_str());
  snprintf(s_dynamic_strings[module_idx][1], 128, "%s", name.c_str());
  snprintf(s_dynamic_strings[module_idx][2], 128, "%s", version.c_str());
  snprintf(s_dynamic_strings[module_idx][3], 128, "%s", author.c_str());
  snprintf(s_dynamic_strings[module_idx][4], 128, "%s", description.c_str());

  module->id = s_dynamic_strings[module_idx][0];
  module->name = s_dynamic_strings[module_idx][1];
  module->version = s_dynamic_strings[module_idx][2];
  module->author = s_dynamic_strings[module_idx][3];
  module->description = s_dynamic_strings[module_idx][4];
}

// Load a dynamic Lua module from SPIFFS
static bool load_dynamic_lua_module(const char* filename) {
  if (s_dynamic_module_count >= MAX_DYNAMIC_MODULES) {
    Serial.println("Module registry: Maximum dynamic modules reached");
    return false;
  }

  String filepath = String("/spiffs/") + filename;
  File file = SPIFFS.open(filepath.c_str(), FILE_READ);
  if (!file) {
    Serial.print("Module registry: Failed to open ");
    Serial.println(filepath);
    return false;
  }

  String content = file.readString();
  file.close();

  if (content.length() == 0) {
    Serial.print("Module registry: Empty file ");
    Serial.println(filepath);
    return false;
  }

  Serial.print("Module registry: Loading ");
  Serial.print(filename);
  Serial.print(" (");
  Serial.print(content.length());
  Serial.println(" bytes)");

  // Create module descriptor
  uint8_t idx = s_dynamic_module_count;
  module_descriptor_t* module = &s_dynamic_modules[idx];

  // Store script path
  snprintf(s_dynamic_strings[idx][5], 128, "%s", filename);
  module->script_path = s_dynamic_strings[idx][5];

  // Parse metadata from file content
  parse_lua_metadata(content, module, idx);

  // Set module properties
  module->host = "lua";
  module->runtime = "lua-5.4.7";
  module->setup = dynamic_lua_setup;
  module->unload = dynamic_lua_unload;
  module->loop = dynamic_lua_loop;
  module->configs = nullptr;
  module->config_count = 0;
  module->built_in = false;

  // 从 NVS 读取配置定义并计算 config_count
  if (module->id != nullptr && strlen(module->id) > 0) {
    String config_def_key = String("cfg_") + module->id;
    String config_def = load_config_string(config_def_key);
    if (config_def.length() > 0) {
      // 计算配置项数量（简单统计 "key" 出现次数）
      int count = 0;
      int pos = 0;
      while ((pos = config_def.indexOf("\"key\"", pos)) >= 0) {
        count++;
        pos += 5;
      }
      module->config_count = count;
    }
  }

  Serial.print("  ID: ");
  Serial.print(module->id ? module->id : "NULL");
  Serial.print(", Name: ");
  Serial.println(module->name ? module->name : "NULL");

  s_dynamic_module_count++;
  return true;
}

// Scan SPIFFS for Lua modules
static void scan_dynamic_modules(void) {
  s_dynamic_module_count = 0;

  // Initialize Lua state arrays
  for (uint8_t i = 0; i < MAX_DYNAMIC_MODULES; i++) {
    s_dynamic_lua_states[i] = nullptr;
    s_dynamic_lua_loaded[i] = false;
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("Module registry: SPIFFS mount failed");
    return;
  }

  File root = SPIFFS.open("/spiffs");
  if (!root || !root.isDirectory()) {
    Serial.println("Module registry: Failed to open /spiffs directory");
    return;
  }

  Serial.println("Module registry: Scanning for dynamic modules...");

  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());

    // Remove path prefix if present
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
      filename = filename.substring(lastSlash + 1);
    }

    // Check if it's a Lua file
    if (filename.endsWith(".lua")) {
      load_dynamic_lua_module(filename.c_str());
    }

    file = root.openNextFile();
  }

  Serial.print("Module registry: Found ");
  Serial.print(s_dynamic_module_count);
  Serial.println(" dynamic modules");
}

// Build combined module list
static void build_module_list(void) {
  s_total_module_count = 0;

  // Add built-in modules
  uint8_t builtin_count = sizeof(k_builtin_modules) / sizeof(k_builtin_modules[0]);
  for (uint8_t i = 0; i < builtin_count; i++) {
    s_all_modules[s_total_module_count++] = &k_builtin_modules[i];
  }

  // Add dynamic modules
  for (uint8_t i = 0; i < s_dynamic_module_count; i++) {
    s_all_modules[s_total_module_count++] = &s_dynamic_modules[i];
  }

  Serial.print("Module registry: Total modules: ");
  Serial.println(s_total_module_count);
}

void module_registry_init(void) {
  Serial.println("Module registry: Initializing...");

  // Initialize native module configs (first run only)
  init_native_module_configs();

  // Scan for dynamic modules
  scan_dynamic_modules();

  // Build combined module list
  build_module_list();

  // Update config_count for all modules from NVS
  update_module_config_counts();

  // Load enabled state from preferences
  Preferences prefs;
  prefs.begin("modules", true);
  for (uint8_t i = 0; i < s_total_module_count; i++) {
    String key = String(s_all_modules[i]->id) + "_en";
    s_enabled[i] = prefs.getBool(key.c_str(), true);
  }
  prefs.end();

  // Ensure at least one module is enabled
  bool any_enabled = false;
  for (uint8_t i = 0; i < s_total_module_count; i++) {
    any_enabled = any_enabled || s_enabled[i];
  }
  if (!any_enabled && s_total_module_count > 0) {
    s_enabled[0] = true;
  }

  Serial.println("Module registry: Initialization complete");
}

uint8_t module_registry_count(void) {
  return s_total_module_count;
}

const module_descriptor_t* module_registry_get(uint8_t index) {
  if (index >= s_total_module_count) return nullptr;
  return s_all_modules[index];
}

bool module_registry_is_enabled(uint8_t index) {
  if (index >= s_total_module_count) return false;
  return s_enabled[index];
}

void module_registry_set_enabled(uint8_t index, bool enabled) {
  if (index >= s_total_module_count) return;

  if (!enabled) {
    bool other_enabled = false;
    for (uint8_t i = 0; i < s_total_module_count; i++) {
      if (i != index && s_enabled[i]) {
        other_enabled = true;
        break;
      }
    }
    if (!other_enabled) {
      Serial.println("module_registry_set_enabled: 拒绝禁用，至少需要保留一个启用的模块");
      return;
    }
  }

  s_enabled[index] = enabled;
  Preferences prefs;
  prefs.begin("modules", false);
  String key = String(s_all_modules[index]->id) + "_en";
  prefs.putBool(key.c_str(), enabled);
  prefs.end();

}

int32_t module_registry_next_enabled(int32_t index) {
  if (s_total_module_count == 0) return -1;
  for (uint8_t step = 1; step <= s_total_module_count; step++) {
    int32_t next = (index + step) % s_total_module_count;
    if (module_registry_is_enabled((uint8_t)next)) return next;
  }
  return -1;
}

int32_t module_registry_normalize_index(int32_t index) {
  if (s_total_module_count == 0) return -1;
  if (index < 0 || index >= s_total_module_count || !module_registry_is_enabled((uint8_t)index)) {
    return module_registry_next_enabled(index);
  }
  return index;
}

// Helper function to inject current config values into config JSON
static String inject_config_values(const String& config_json, const char* module_id) {
  if (config_json.length() == 0 || config_json == "[]") {
    return config_json;
  }

  String ns = String(module_id);
  String result = "";
  int pos = 0;

  while (pos < (int)config_json.length()) {
    // Find next config object
    int obj_start = config_json.indexOf('{', pos);
    if (obj_start < 0) {
      result += config_json.substring(pos);
      break;
    }

    // Copy everything before the object
    result += config_json.substring(pos, obj_start + 1);

    // Find the end of this object (handle nested braces)
    int obj_end = obj_start + 1;
    int brace_count = 1;
    bool in_string = false;
    bool escape_next = false;

    while (obj_end < (int)config_json.length() && brace_count > 0) {
      char c = config_json[obj_end];

      if (escape_next) {
        escape_next = false;
      } else if (c == '\\') {
        escape_next = true;
      } else if (c == '"') {
        in_string = !in_string;
      } else if (!in_string) {
        if (c == '{') {
          brace_count++;
        } else if (c == '}') {
          brace_count--;
        }
      }

      if (brace_count > 0) {
        obj_end++;
      }
    }

    if (brace_count != 0) {
      result += config_json.substring(obj_start + 1);
      break;
    }

    String obj_content = config_json.substring(obj_start + 1, obj_end);

    // Extract key
    String key = "";
    int key_pos = obj_content.indexOf("\"key\"");
    if (key_pos >= 0) {
      int colon = obj_content.indexOf(':', key_pos);
      if (colon >= 0) {
        int quote1 = obj_content.indexOf('"', colon);
        int quote2 = obj_content.indexOf('"', quote1 + 1);
        if (quote1 >= 0 && quote2 > quote1) {
          key = obj_content.substring(quote1 + 1, quote2);
        }
      }
    }

    // Extract type
    String type = "";
    int type_pos = obj_content.indexOf("\"type\"");
    if (type_pos >= 0) {
      int colon = obj_content.indexOf(':', type_pos);
      if (colon >= 0) {
        int quote1 = obj_content.indexOf('"', colon);
        int quote2 = obj_content.indexOf('"', quote1 + 1);
        if (quote1 >= 0 && quote2 > quote1) {
          type = obj_content.substring(quote1 + 1, quote2);
        }
      }
    }

    // Read current value from NVS (only if key exists)
    String value_str = "";
    if (key.length() > 0 && type.length() > 0) {
      Preferences prefs;
      prefs.begin(ns.c_str(), true);
      bool key_exists = prefs.isKey(key.c_str());

      if (key_exists) {
        if (type == "slider" || type == "number") {
          // Try both float and int (slider values might be saved as either type)
          float float_value = prefs.getFloat(key.c_str(), 0.0f);
          int int_value = prefs.getInt(key.c_str(), 0);

          // Prefer non-zero float, then non-zero int
          if (float_value != 0.0f) {
            value_str = String(float_value);
          } else if (int_value != 0) {
            value_str = String((float)int_value);
          }
        } else if (type == "switch") {
          int int_value = prefs.getInt(key.c_str(), 0);
          value_str = String(int_value);
        } else if (type == "text" || type == "color") {
          String str_value = prefs.getString(key.c_str(), "");
          if (str_value.length() > 0) {
            value_str = "\"" + json_escape(str_value.c_str()) + "\"";
          }
        } else if (type == "select") {
          // Select can be either string or int, try int first then string
          int int_value = prefs.getInt(key.c_str(), -999);  // Use sentinel value
          if (int_value != -999) {
            // Found int value (including 0)
            value_str = String(int_value);
          } else {
            // Try string value
            String str_value = prefs.getString(key.c_str(), "");
            if (str_value.length() > 0) {
              value_str = "\"" + json_escape(str_value.c_str()) + "\"";
            }
          }
        }
      }

      prefs.end();
    }

    // Copy object content
    result += obj_content;

    // Add value field if we have one
    if (value_str.length() > 0) {
      result += ",\"value\":" + value_str;
    }

    result += "}";
    pos = obj_end + 1;
  }

  return result;
}

String module_registry_manifest_json(int32_t index) {
  const module_descriptor_t* module = module_registry_get((uint8_t)index);
  if (!module) return "{}";

  String s = "{";
  s += "\"id\":\"" + json_escape(module->id ? module->id : "") + "\"";
  s += ",\"name\":\"" + json_escape(module->name ? module->name : "") + "\"";
  s += ",\"version\":\"" + json_escape(module->version ? module->version : "") + "\"";
  s += ",\"author\":\"" + json_escape(module->author ? module->author : "") + "\"";
  s += ",\"description\":\"" + json_escape(module->description ? module->description : "") + "\"";
  s += ",\"host\":\"" + json_escape(module->host ? module->host : "") + "\"";
  s += ",\"runtime\":\"" + json_escape(module->runtime ? module->runtime : "") + "\"";

  if (module->script_path) {
    s += ",\"script_path\":\"" + json_escape(module->script_path) + "\"";
  }

  s += ",\"builtin\":" + String(module->built_in ? "true" : "false");
  s += ",\"enabled\":" + String(module_registry_is_enabled((uint8_t)index) ? "true" : "false");
  s += ",\"configs\":";

  // 统一从 NVS 读取配置定义（Native和Lua模块都使用JSON格式）
  if (module->id != nullptr) {
    String config_def_key = String("cfg_") + module->id;
    String config_def = load_config_string(config_def_key);
    if (config_def.length() > 0) {
      // 注入当前配置值
      String config_with_values = inject_config_values(config_def, module->id);
      s += config_with_values;
    } else {
      s += "[]";
    }
  } else {
    s += "[]";
  }

  s += "}";
  return s;
}

// Dynamic Lua module execution functions
static int dynamic_lua_setup(void) {
  // Find which dynamic module is being set up
  // We need to track this through the current page_index
  extern int32_t page_index;

  if (page_index < 0 || page_index >= s_total_module_count) {
    Serial.println("Dynamic Lua setup: Invalid page index");
    return -1;
  }

  const module_descriptor_t* module = s_all_modules[page_index];
  if (!module || module->built_in || !module->script_path) {
    Serial.println("Dynamic Lua setup: Not a dynamic module");
    return -1;
  }

  // Find the dynamic module index
  int dynamic_idx = -1;
  for (uint8_t i = 0; i < s_dynamic_module_count; i++) {
    if (&s_dynamic_modules[i] == module) {
      dynamic_idx = i;
      break;
    }
  }

  if (dynamic_idx < 0) {
    Serial.println("Dynamic Lua setup: Module not found in dynamic list");
    return -1;
  }

  s_current_dynamic_module = dynamic_idx;

  Serial.print("Dynamic Lua setup: Loading ");
  Serial.println(module->script_path);

  // Create Lua state
  lua_State* L = luaL_newstate();
  if (!L) {
    Serial.println("Dynamic Lua setup: Failed to create Lua state");
    return -1;
  }

  luaL_openlibs(L);
  register_lua_hardware_apis(L);

  // 注入 CONFIG 全局表（从 NVS 读取配置）
  inject_lua_config_table(L, module->id);

  // Load script from SPIFFS
  String filepath = String("/spiffs/") + module->script_path;
  File file = SPIFFS.open(filepath.c_str(), FILE_READ);
  if (!file) {
    Serial.print("Dynamic Lua setup: Failed to open ");
    Serial.println(filepath);
    lua_close(L);
    return -1;
  }

  String script = file.readString();
  file.close();

  // Execute script
  if (luaL_dostring(L, script.c_str()) != LUA_OK) {
    const char* error = lua_tostring(L, -1);
    Serial.print("Dynamic Lua setup: Script error: ");
    Serial.println(error);
    lua_pop(L, 1);
    lua_close(L);
    return -1;
  }

  // Call setup function if it exists
  lua_getglobal(L, "setup");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(L, -1);
      Serial.print("Dynamic Lua setup: Error in setup(): ");
      Serial.println(error);
      lua_pop(L, 1);
      lua_close(L);
      return -1;
    }
  } else {
    lua_pop(L, 1);
  }

  // Start hardware resources declared by use()
  lua_hardware_start_resources();

  s_dynamic_lua_states[dynamic_idx] = L;
  s_dynamic_lua_loaded[dynamic_idx] = true;

  Serial.println("Dynamic Lua setup: Success");
  return 0;
}

static int dynamic_lua_loop(void) {
  if (s_current_dynamic_module < 0 || s_current_dynamic_module >= s_dynamic_module_count) {
    return 0;
  }

  lua_State* L = s_dynamic_lua_states[s_current_dynamic_module];
  if (!L || !s_dynamic_lua_loaded[s_current_dynamic_module]) {
    return 0;
  }

  // Update hardware state for Lua API
  lua_hardware_update_gravity();

  // Call loop function
  lua_getglobal(L, "loop");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(L, -1);
      Serial.print("Dynamic Lua loop: Error: ");
      Serial.println(error);
      lua_pop(L, 1);
      s_dynamic_lua_loaded[s_current_dynamic_module] = false;
      return -1;
    }
  } else {
    lua_pop(L, 1);
  }

  return 0;
}

static int dynamic_lua_unload(void) {
  if (s_current_dynamic_module < 0 || s_current_dynamic_module >= s_dynamic_module_count) {
    return 0;
  }

  lua_State* L = s_dynamic_lua_states[s_current_dynamic_module];
  if (L && s_dynamic_lua_loaded[s_current_dynamic_module]) {
    // Call unload function if it exists
    lua_getglobal(L, "unload");
    if (lua_isfunction(L, -1)) {
      lua_pcall(L, 0, 0, 0);
    } else {
      lua_pop(L, 1);
    }

    // Stop hardware resources
    lua_hardware_stop_resources();

    lua_close(L);
    s_dynamic_lua_states[s_current_dynamic_module] = nullptr;
    s_dynamic_lua_loaded[s_current_dynamic_module] = false;
  }

  s_current_dynamic_module = -1;

  Serial.println("Dynamic Lua unload: Success");
  return 0;
}

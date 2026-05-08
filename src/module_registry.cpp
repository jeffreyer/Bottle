#include "module_registry.h"
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

static const module_config_item_t water_configs[] = {
  {"sim_index", "Palette", MODULE_CONFIG_INT, 0, 99, 0, nullptr},
};

static const module_config_item_t candle_configs[] = {
  {"candle_index", "Color", MODULE_CONFIG_INT, 0, 99, 0, nullptr},
};

static const module_config_item_t sand_configs[] = {
  {"sand_speed", "Speed", MODULE_CONFIG_INT, 1, 10, 5, nullptr},
};

// Built-in modules (static)
static const module_descriptor_t k_builtin_modules[] = {
  {"rhythm", "跳动音律", "3.0.0", "Bottle", "Lua-powered audio spectrum visualizer", "lua", "lua-5.4.7", nullptr, setup_rhythm_lua_module, unload_rhythm_lua_module, loop_rhythm_lua_module, nullptr, 0, true},
  {"water", "海洋流体", "1.0.0", "Bottle", "Gravity liquid simulation", "native", "native", nullptr, setup_fluid, unload_fluid, fluid_loop, water_configs, (uint8_t)(sizeof(water_configs) / sizeof(water_configs[0])), true},
  {"candle", "蜡烛焰火", "1.0.0", "Bottle", "Gravity-aware candle flame", "native", "native", nullptr, setup_candle, unload_candle, candle_loop, candle_configs, (uint8_t)(sizeof(candle_configs) / sizeof(candle_configs[0])), true},
  {"sandglass", "沙漏", "1.0.0", "Bottle", "Gravity sandglass", "native", "native", nullptr, setup_sand, unload_sand, sand_loop, sand_configs, (uint8_t)(sizeof(sand_configs) / sizeof(sand_configs[0])), true},
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

  Serial.print("  ID: ");
  Serial.print(module->id);
  Serial.print(", Name: ");
  Serial.println(module->name);

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
      // Skip built-in Lua modules
      if (filename != "rhythm.lua") {
        load_dynamic_lua_module(filename.c_str());
      }
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

  // Scan for dynamic modules
  scan_dynamic_modules();

  // Build combined module list
  build_module_list();

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

  Serial.print("module_registry_set_enabled: index=");
  Serial.print(index);
  Serial.print(", enabled=");
  Serial.println(enabled);

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

  Serial.print("module_registry_set_enabled: 已保存到 Preferences, key=");
  Serial.println(key);
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

String module_registry_manifest_json(int32_t index) {
  const module_descriptor_t* module = module_registry_get((uint8_t)index);
  if (!module) return "{}";

  String s = "{";
  s += "\"id\":\"" + json_escape(module->id) + "\"";
  s += ",\"name\":\"" + json_escape(module->name) + "\"";
  s += ",\"version\":\"" + json_escape(module->version) + "\"";
  s += ",\"author\":\"" + json_escape(module->author) + "\"";
  s += ",\"description\":\"" + json_escape(module->description) + "\"";
  s += ",\"host\":\"" + json_escape(module->host) + "\"";
  s += ",\"runtime\":\"" + json_escape(module->runtime) + "\"";
  if (module->script_path) {
    s += ",\"script_path\":\"" + json_escape(module->script_path) + "\"";
  }
  s += ",\"builtin\":" + String(module->built_in ? "true" : "false");
  s += ",\"enabled\":" + String(module_registry_is_enabled((uint8_t)index) ? "true" : "false");
  if (String(module->id) == "rhythm") {
    // s += ",\"runtime_status\":" + rhythm_lua_module_runtime_status_json();
  }
  s += ",\"configs\":";
  if (String(module->id) == "rhythm") {
    // s += rhythm_lua_module_configs_json();
  } else {
    s += "[";
    for (uint8_t i = 0; i < module->config_count; i++) {
      const module_config_item_t& cfg = module->configs[i];
      if (i > 0) s += ",";
      s += "{";
      s += "\"key\":\"" + json_escape(cfg.key) + "\"";
      s += ",\"label\":\"" + json_escape(cfg.label) + "\"";
      s += ",\"type\":\"" + config_type_name(cfg.type) + "\"";
      s += ",\"min\":" + String(cfg.min_value);
      s += ",\"max\":" + String(cfg.max_value);
      s += ",\"default\":" + String(cfg.default_value);
      if (cfg.options) {
        s += ",\"options\":\"" + json_escape(cfg.options) + "\"";
      }
      s += "}";
    }
    s += "]";
  }
  s += "}";
  return s;
}

String module_registry_status_json(void) {
  String s = "[";
  for (uint8_t i = 0; i < s_total_module_count; i++) {
    const module_descriptor_t* module = s_all_modules[i];

    if (i > 0) s += ",";

    s += "{";
    s += "\"i\":" + String(i);
    s += ",\"id\":\"" + json_escape(module->id) + "\"";
    s += ",\"n\":\"" + json_escape(module->name) + "\"";
    s += ",\"b\":" + String(module->built_in ? "1" : "0");
    s += ",\"e\":" + String(module_registry_is_enabled(i) ? "1" : "0");
    s += ",\"c\":" + String(module->config_count);
    s += "}";
  }
  s += "]";
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

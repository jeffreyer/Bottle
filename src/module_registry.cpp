#include "module_registry.h"
#include "breakout.h"
#include "candle.h"
#include "rhythm_module.h"
#include "sandglass.h"
#include "sim_manager.h"
#include <Preferences.h>

static const module_config_item_t water_configs[] = {
  {"sim_index", "Palette", MODULE_CONFIG_INT, 0, 99, 0, nullptr},
};

static const module_config_item_t candle_configs[] = {
  {"candle_index", "Color", MODULE_CONFIG_INT, 0, 99, 0, nullptr},
};

static const module_config_item_t sand_configs[] = {
  {"sand_speed", "Speed", MODULE_CONFIG_INT, 1, 10, 5, nullptr},
};

static const module_config_item_t breakout_configs[] = {
  {"breakout_level", "Level", MODULE_CONFIG_INT, 0, 9, 0, nullptr},
};

static const module_descriptor_t k_modules[] = {
  {"rhythm", "Rhythm", "2.0.0", "Bottle", "Runtime-style audio spectrum visualizer", "native_host", "bottle-vm@0.1", "/rhythm_spectrum/main.bottle", setup_rhythm_module, unload_rhythm_module, loop_rhythm_module, nullptr, 0, true},
  {"water", "Water", "1.0.0", "Bottle", "Gravity liquid simulation", "native", "native", nullptr, setup_fluid, unload_fluid, fluid_loop, water_configs, (uint8_t)(sizeof(water_configs) / sizeof(water_configs[0])), true},
  {"candle", "Candle", "1.0.0", "Bottle", "Gravity-aware candle flame", "native", "native", nullptr, setup_candle, unload_candle, candle_loop, candle_configs, (uint8_t)(sizeof(candle_configs) / sizeof(candle_configs[0])), true},
  {"sandglass", "Sandglass", "1.0.0", "Bottle", "Gravity sandglass", "native", "native", nullptr, setup_sand, unload_sand, sand_loop, sand_configs, (uint8_t)(sizeof(sand_configs) / sizeof(sand_configs[0])), true},
  {"breakout", "Breakout", "1.0.0", "Bottle", "Tiny motion game", "native", "native", nullptr, setup_breakout, unload_breakout, breakout_loop, breakout_configs, (uint8_t)(sizeof(breakout_configs) / sizeof(breakout_configs[0])), true},
};

static bool s_enabled[sizeof(k_modules) / sizeof(k_modules[0])];

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

void module_registry_init(void) {
  Preferences prefs;
  prefs.begin("modules", true);
  for (uint8_t i = 0; i < module_registry_count(); i++) {
    String key = String(k_modules[i].id) + "_en";
    s_enabled[i] = prefs.getBool(key.c_str(), true);
  }
  prefs.end();

  bool any_enabled = false;
  for (uint8_t i = 0; i < module_registry_count(); i++) {
    any_enabled = any_enabled || s_enabled[i];
  }
  if (!any_enabled && module_registry_count() > 0) {
    s_enabled[0] = true;
  }
}

uint8_t module_registry_count(void) {
  return (uint8_t)(sizeof(k_modules) / sizeof(k_modules[0]));
}

const module_descriptor_t* module_registry_get(uint8_t index) {
  if (index >= module_registry_count()) return nullptr;
  return &k_modules[index];
}

bool module_registry_is_enabled(uint8_t index) {
  if (index >= module_registry_count()) return false;
  return s_enabled[index];
}

void module_registry_set_enabled(uint8_t index, bool enabled) {
  if (index >= module_registry_count()) return;

  if (!enabled) {
    bool other_enabled = false;
    for (uint8_t i = 0; i < module_registry_count(); i++) {
      if (i != index && s_enabled[i]) {
        other_enabled = true;
        break;
      }
    }
    if (!other_enabled) return;
  }

  s_enabled[index] = enabled;
  Preferences prefs;
  prefs.begin("modules", false);
  String key = String(k_modules[index].id) + "_en";
  prefs.putBool(key.c_str(), enabled);
  prefs.end();
}

int32_t module_registry_next_enabled(int32_t index) {
  uint8_t count = module_registry_count();
  if (count == 0) return -1;
  for (uint8_t step = 1; step <= count; step++) {
    int32_t next = (index + step) % count;
    if (module_registry_is_enabled((uint8_t)next)) return next;
  }
  return -1;
}

int32_t module_registry_normalize_index(int32_t index) {
  uint8_t count = module_registry_count();
  if (count == 0) return -1;
  if (index < 0 || index >= count || !module_registry_is_enabled((uint8_t)index)) {
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
    s += ",\"runtime_status\":" + rhythm_module_runtime_status_json();
  }
  s += ",\"configs\":";
  if (String(module->id) == "rhythm") {
    s += rhythm_module_configs_json();
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
  for (uint8_t i = 0; i < module_registry_count(); i++) {
    if (i > 0) s += ",";
    const module_descriptor_t& module = k_modules[i];
    s += "{";
    s += "\"index\":" + String(i);
    s += ",\"id\":\"" + json_escape(module.id) + "\"";
    s += ",\"name\":\"" + json_escape(module.name) + "\"";
    s += ",\"version\":\"" + json_escape(module.version) + "\"";
    s += ",\"host\":\"" + json_escape(module.host) + "\"";
    s += ",\"runtime\":\"" + json_escape(module.runtime) + "\"";
    if (module.script_path) {
      s += ",\"script_path\":\"" + json_escape(module.script_path) + "\"";
    }
    s += ",\"builtin\":" + String(module.built_in ? "true" : "false");
    s += ",\"enabled\":" + String(module_registry_is_enabled(i) ? "true" : "false");
    s += ",\"config_count\":" + String(module.config_count);
    s += "}";
  }
  s += "]";
  return s;
}

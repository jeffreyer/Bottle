#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*module_func_t)(void);

typedef enum {
  MODULE_CONFIG_INT = 0,
  MODULE_CONFIG_BOOL = 1,
  MODULE_CONFIG_SELECT = 2,
} module_config_type_t;

typedef struct {
  const char* key;
  const char* label;
  module_config_type_t type;
  int32_t min_value;
  int32_t max_value;
  int32_t default_value;
  const char* options;
} module_config_item_t;

typedef struct {
  const char* id;
  const char* name;
  const char* version;
  const char* author;
  const char* description;
  const char* host;
  const char* runtime;
  const char* script_path;
  module_func_t setup;
  module_func_t unload;
  module_func_t loop;
  const module_config_item_t* configs;
  uint8_t config_count;
  bool built_in;
} module_descriptor_t;

void module_registry_init(void);
uint8_t module_registry_count(void);
const module_descriptor_t* module_registry_get(uint8_t index);
bool module_registry_is_enabled(uint8_t index);
void module_registry_set_enabled(uint8_t index, bool enabled);
int32_t module_registry_next_enabled(int32_t index);
int32_t module_registry_normalize_index(int32_t index);
String module_registry_manifest_json(int32_t index);

#ifdef __cplusplus
}
#endif

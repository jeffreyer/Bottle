#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

int setup_colorwave_lua_module(void);
int unload_colorwave_lua_module(void);
int loop_colorwave_lua_module(void);

#ifdef __cplusplus
}
String colorwave_lua_module_runtime_status_json(void);
String colorwave_lua_module_configs_json(void);
#endif

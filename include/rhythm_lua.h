#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

int setup_rhythm_lua_module(void);
int unload_rhythm_lua_module(void);
int loop_rhythm_lua_module(void);

#ifdef __cplusplus
}
#endif

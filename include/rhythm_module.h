#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

int setup_rhythm_module(void);
int unload_rhythm_module(void);
int loop_rhythm_module(void);

#ifdef __cplusplus
}
String rhythm_module_runtime_status_json(void);
String rhythm_module_configs_json(void);
#endif

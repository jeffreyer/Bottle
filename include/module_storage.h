#pragma once

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

bool module_storage_init(void);
String module_storage_read_text(const char* path);
bool module_storage_write_text(const char* path, const char* content);
bool module_storage_exists(const char* path);
void module_storage_ensure_defaults(void);

#ifdef __cplusplus
}
#endif

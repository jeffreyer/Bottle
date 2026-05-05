#pragma once

#include <stdint.h>
#include "module_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Value types supported by the VM
typedef enum {
  BOTTLE_TYPE_INT = 0,
  BOTTLE_TYPE_FLOAT = 1,
  BOTTLE_TYPE_BOOL = 2,
  BOTTLE_TYPE_COLOR = 3,
} bottle_value_type_t;

// Tagged union for runtime values
typedef struct {
  bottle_value_type_t type;
  union {
    int32_t i;
    float f;
    bool b;
    module_rgb_t color;
  } as;
} bottle_value_t;

// Helper constructors
static inline bottle_value_t bottle_int(int32_t value) {
  bottle_value_t v;
  v.type = BOTTLE_TYPE_INT;
  v.as.i = value;
  return v;
}

static inline bottle_value_t bottle_float(float value) {
  bottle_value_t v;
  v.type = BOTTLE_TYPE_FLOAT;
  v.as.f = value;
  return v;
}

static inline bottle_value_t bottle_bool(bool value) {
  bottle_value_t v;
  v.type = BOTTLE_TYPE_BOOL;
  v.as.b = value;
  return v;
}

static inline bottle_value_t bottle_color(module_rgb_t value) {
  bottle_value_t v;
  v.type = BOTTLE_TYPE_COLOR;
  v.as.color = value;
  return v;
}

// Type conversion helpers
static inline bool bottle_is_truthy(bottle_value_t v) {
  switch (v.type) {
    case BOTTLE_TYPE_INT: return v.as.i != 0;
    case BOTTLE_TYPE_FLOAT: return v.as.f != 0.0f;
    case BOTTLE_TYPE_BOOL: return v.as.b;
    case BOTTLE_TYPE_COLOR: return true; // Colors are always truthy
    default: return false;
  }
}

static inline float bottle_to_float(bottle_value_t v) {
  switch (v.type) {
    case BOTTLE_TYPE_INT: return (float)v.as.i;
    case BOTTLE_TYPE_FLOAT: return v.as.f;
    case BOTTLE_TYPE_BOOL: return v.as.b ? 1.0f : 0.0f;
    default: return 0.0f;
  }
}

static inline int32_t bottle_to_int(bottle_value_t v) {
  switch (v.type) {
    case BOTTLE_TYPE_INT: return v.as.i;
    case BOTTLE_TYPE_FLOAT: return (int32_t)v.as.f;
    case BOTTLE_TYPE_BOOL: return v.as.b ? 1 : 0;
    default: return 0;
  }
}

static inline uint8_t bottle_to_uint8(bottle_value_t v) {
  int32_t i = bottle_to_int(v);
  if (i < 0) return 0;
  if (i > 255) return 255;
  return (uint8_t)i;
}

#ifdef __cplusplus
}
#endif

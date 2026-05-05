#pragma once

#include <stdint.h>

// Forward declare MATRIX_WIDTH if not already defined
#ifndef MATRIX_WIDTH
#define MATRIX_WIDTH 17
#define MATRIX_HEIGHT 8
#endif

// Mock module_runtime.h for native testing

struct module_rgb_t {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct module_gravity_t {
  bool valid;
  float x;
  float y;
  float z;
};

struct module_sensor_api_t {
  uint8_t spectrum[MATRIX_WIDTH];
  module_gravity_t gravity;
};

struct module_led_api_t {
  void clear() {}
  void set(uint8_t x, uint8_t y, module_rgb_t color) {}
  void show() {}
};

struct module_config_api_t {
  int32_t get_int(const char* key, int32_t default_value) { return default_value; }
  void set_int(const char* key, int32_t value) {}
  int32_t style() { return 0; }
};

struct module_context_t {
  module_sensor_api_t sensor;
  module_led_api_t led;
  module_config_api_t config;
  uint32_t now_ms;
  void* state;
};

inline module_rgb_t module_hsv(uint8_t h, uint8_t s, uint8_t v) {
  return {0, 0, 0};
}

inline module_rgb_t module_blend(module_rgb_t a, module_rgb_t b, uint8_t amount) {
  return {0, 0, 0};
}

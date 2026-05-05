#include "module_runtime.h"

#include <FastLED.h>
#include "app_control.h"

void module_led_api_t::clear() {

  FastLED.clear();
}

void module_led_api_t::set(uint8_t x, uint8_t y, module_rgb_t color) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;

  leds(x, y) = CRGB(color.r, color.g, color.b);
}

void module_led_api_t::show() {

  FastLED.show();
}

int32_t module_config_api_t::get_int(const char* key, int32_t default_value) {
  if (strcmp(key, "style") == 0) {
    return app_get_subpage_index();
  }
  return load_config(String(key));
}

void module_config_api_t::set_int(const char* key, int32_t value) {
  save_config(String(key), value);
}

int32_t module_config_api_t::style() {
  return app_get_subpage_index();
}

module_rgb_t module_hsv(uint8_t h, uint8_t s, uint8_t v) {
  CRGB c;
  hsv2rgb_rainbow(CHSV(h, s, v), c);
  return {c.r, c.g, c.b};
}

module_rgb_t module_blend(module_rgb_t a, module_rgb_t b, uint8_t amount) {
  CRGB ca(a.r, a.g, a.b);
  CRGB cb(b.r, b.g, b.b);
  CRGB c = blend(ca, cb, amount);
  return {c.r, c.g, c.b};
}

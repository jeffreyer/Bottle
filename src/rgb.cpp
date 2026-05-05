#include "rgb.h"
#include "common.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <esp_sleep.h>

void rgb_init(void) {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds[0], NUM_LEDS);
  
  // 检查是否从深度睡眠唤醒，如果是则设置亮度为0（避免闪烁）
  // 否则设置最大亮度
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    // 从深度睡眠唤醒，设置亮度为0，等待后续渐变恢复
    FastLED.setBrightness(0);
  }

  FastLED.show();
}

void rgb_deinit(void) {
  // FastLED库不需要显式清理
}

void rgb_set(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b){
  leds(x,y) = CRGB(r,g,b);
}

void rgb_set_fast(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
  // if (index >= NUM_LEDS) {
  //   return;
  // }
  // leds[index] = CRGB(r, g, b);
}

void rgb_clear(void) {
  FastLED.clear();
}

void rgb_set_hsv(uint32_t index, uint16_t hue, uint16_t light) {
  // if (index >= NUM_LEDS) {
  //   return;
  // }

  // hue %= 360;
  // light = (light > PANEL_LED_VALUE_MAX) ? PANEL_LED_VALUE_MAX : light;
  
  // CHSV hsv(hue, 255, light);
  // CRGB rgb;
  // hsv2rgb_spectrum(hsv, rgb);
  // leds[index] = rgb;
}

void rgb_show(void) {
  FastLED.show();
  delayMicroseconds(80);
}

void rgb_set_brightness(uint8_t brightness) {
  FastLED.setBrightness(brightness);
}

uint8_t rgb_get_brightness(void) {
  return FastLED.getBrightness();
}

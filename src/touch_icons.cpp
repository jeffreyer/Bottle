#include "touch_icons.h"
#include "common.h"
#include "rgb.h"
#include <FastLED.h>

void show_module_hold_hint(void) {
  // Clear screen first
  rgb_clear();
  FastLED.show();
  delay(5);

  // Draw "next" icon: two separate right-pointing arrows >
  rgb_set_brightness(brightness_max);

  const uint8_t center_y = MATRIX_HEIGHT / 2;

  // First arrow (left)
  const uint8_t x1 = MATRIX_WIDTH / 2 - 3;
  // Top part of arrow
  rgb_set(x1, center_y - 2, 0, 50, 80);
  rgb_set(x1 + 1, center_y - 1, 0, 50, 80);
  rgb_set(x1 + 2, center_y, 0, 50, 80);
  rgb_set(x1 + 1, center_y + 1, 0, 50, 80);
  rgb_set(x1, center_y + 2, 0, 50, 80);

  // Second arrow (right, closer)
  const uint8_t x2 = MATRIX_WIDTH / 2 + 1;
  // Top part of arrow
  rgb_set(x2, center_y - 2, 0, 50, 80);
  rgb_set(x2 + 1, center_y - 1, 0, 50, 80);
  rgb_set(x2 + 2, center_y, 0, 50, 80);
  rgb_set(x2 + 1, center_y + 1, 0, 50, 80);
  rgb_set(x2, center_y + 2, 0, 50, 80);

  FastLED.show();
}

void show_ble_hold_hint(bool show_slash) {
  // Clear screen first
  rgb_clear();
  FastLED.show();
  delay(5);

  // Draw Bluetooth icon (same as in ble_config.cpp)
  rgb_set_brightness(brightness_max);

  const uint8_t blue = 48;
  const uint8_t cyan = 20;
  auto px = [&](uint8_t x, uint8_t y) {
    rgb_set(x, y, 0, cyan, blue);
  };

  // 8-row Bluetooth glyph, centered on the 17x8 panel.
  px(8, 0);
  px(8, 1); px(9, 1);
  px(5, 2); px(7, 2); px(8, 2); px(10, 2);
  px(6, 3); px(8, 3); px(9, 3);
  px(6, 4); px(8, 4); px(9, 4);
  px(5, 5); px(7, 5); px(8, 5); px(10, 5);
  px(8, 6); px(9, 6);
  px(8, 7);

  // Add red diagonal slash only when show_slash is true (BLE mode = turn off)
  if (show_slash) {
    // Diagonal line from top-left to bottom-right across the BLE icon
    rgb_set(4, 1, 80, 0, 0);
    rgb_set(5, 2, 80, 0, 0);
    rgb_set(6, 3, 80, 0, 0);
    rgb_set(7, 4, 80, 0, 0);
    rgb_set(8, 4, 80, 0, 0);
    rgb_set(9, 5, 80, 0, 0);
    rgb_set(10, 6, 80, 0, 0);
    rgb_set(11, 7, 80, 0, 0);
  }

  FastLED.show();
}

void show_sleep_hold_hint(void) {
  // Clear screen first
  rgb_clear();
  FastLED.show();
  delay(5);

  rgb_set_brightness(brightness_max);

  const uint8_t center_x = MATRIX_WIDTH / 2;  // 8
  const uint8_t center_y = MATRIX_HEIGHT / 2; // 4

  // Draw power off icon: bottom vertical line + circle with gap at bottom (upside down)
  // Circle with gap at bottom (incomplete circle) - radius increased by 1 pixel

  // Left side (extended)
  rgb_set(center_x - 3, center_y - 2, 60, 0, 0);
  rgb_set(center_x - 3, center_y - 1, 60, 0, 0);
  rgb_set(center_x - 3, center_y, 60, 0, 0);
  rgb_set(center_x - 3, center_y + 1, 60, 0, 0);
  // rgb_set(center_x - 3, center_y + 2, 60, 0, 0);

  // Left middle
  rgb_set(center_x - 2, center_y - 3, 60, 0, 0);
  rgb_set(center_x - 2, center_y + 2, 60, 0, 0);

  // Top left corner
  rgb_set(center_x - 1, center_y - 4, 60, 0, 0);

  // Top
  rgb_set(center_x, center_y - 4, 60, 0, 0);

  // Top right corner
  rgb_set(center_x + 1, center_y - 4, 60, 0, 0);

  // Right middle
  rgb_set(center_x + 2, center_y - 3, 60, 0, 0);
  rgb_set(center_x + 2, center_y + 2, 60, 0, 0);

  // Right side (extended)
  // rgb_set(center_x + 3, center_y + 2, 60, 0, 0);
  rgb_set(center_x + 3, center_y + 1, 60, 0, 0);
  rgb_set(center_x + 3, center_y, 60, 0, 0);
  rgb_set(center_x + 3, center_y - 1, 60, 0, 0);
  rgb_set(center_x + 3, center_y - 2, 60, 0, 0);

  // Bottom vertical line (power indicator, 3 pixels)
  rgb_set(center_x, center_y + 1, 60, 0, 0);
  rgb_set(center_x, center_y + 2, 60, 0, 0);
  rgb_set(center_x, center_y + 3, 60, 0, 0);

  FastLED.show();
}

void draw_low_battery_hint(void) {
  for (int a=0;a<16;a++){
    rgb_clear();

    for (int y = 1; y < MATRIX_HEIGHT-1; y++) {
      rgb_set(2, y, 60, 0, 0);
    }
    if (a%2==0){
      for (int y = 1; y < MATRIX_HEIGHT-1; y++) {
        rgb_set(3, y, 60, 0, 0);
      }
    }
    for (int x = 2; x < MATRIX_WIDTH-4; x++) {
      rgb_set(x, 1, 60, 0, 0);
    }
    for (int x = 2; x < MATRIX_WIDTH-4; x++) {
      rgb_set(x, 6, 60, 0, 0);
    }
    for (int y = 1; y < MATRIX_HEIGHT-1; y++) {
      rgb_set(MATRIX_WIDTH-5, y, 60, 0, 0);
    }
    for (int y = 2; y < MATRIX_HEIGHT-2; y++) {
      rgb_set(MATRIX_WIDTH-4, y, 60, 0, 0);
    }
    FastLED.show();
    delay(300);
  }
  
}
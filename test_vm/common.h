#pragma once

#include <stdint.h>

#define SERIAL_PRINTF_ENABLED 1

// Mock LED matrix dimensions
#define LED_SWITCH_PIN 4
#define DATA_PIN     48
#define TOUCH_PIN    12

#define MATRIX_WIDTH     17
#define MATRIX_HEIGHT    8
#define NUM_LEDS    (MATRIX_WIDTH * MATRIX_HEIGHT)

#define PANEL_LED_VALUE_MAX 30

// Mock LED matrix class
template<int WIDTH, int HEIGHT, int LAYOUT>
struct cLEDMatrix {
    // Empty mock
};

// Mock global variables
extern cLEDMatrix<-MATRIX_WIDTH, -MATRIX_HEIGHT, 0> leds;
extern int32_t page_index, subpage_index;
extern uint8_t brightness_max;
extern uint8_t user_brightness_max;

// Mock String class
class String {
public:
    String() {}
    String(const char* s) {}
};

// Mock functions
inline int load_config(String key) { return 0; }
inline void save_config(String key, int value) {}

// Mock FastLED constants
#define VERTICAL_ZIGZAG_MATRIX 0

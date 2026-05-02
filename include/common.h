#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <LEDMatrix.h>

#define LED_SWITCH_PIN 4
#define DATA_PIN     48
#define TOUCH_PIN    12

#define MATRIX_WIDTH     17
#define MATRIX_HEIGHT    8
#define NUM_LEDS    (MATRIX_WIDTH * MATRIX_HEIGHT)

extern cLEDMatrix<-MATRIX_WIDTH, -MATRIX_HEIGHT, VERTICAL_ZIGZAG_MATRIX> leds;

extern int32_t page_index,subpage_index;

extern uint8_t brightness_max;

// Unified max value for LED channels used by all simulators.
#define PANEL_LED_VALUE_MAX 30

int load_config(String key);

void save_config(String key,int value);
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

// #define MPU6050
#define LIS3DH

extern cLEDMatrix<-MATRIX_WIDTH, -MATRIX_HEIGHT, VERTICAL_ZIGZAG_MATRIX> leds;

extern int32_t page_index,subpage_index;

extern uint8_t brightness_max;
extern uint8_t user_brightness_max;

// Unified max value for LED channels used by all simulators.
#define PANEL_LED_VALUE_MAX 30

int load_config(String key);

void save_config(String key,int value);

// 字符串配置支持
String load_config_string(String key);
void save_config_string(String key, String value);

// 浮点数配置支持
float load_config_float(String key);
void save_config_float(String key, float value);

// 带命名空间的配置函数
void save_config_ns(String ns, String key, int value);
int load_config_ns(String ns, String key);
void save_config_string_ns(String ns, String key, String value);
String load_config_string_ns(String ns, String key);
void save_config_float_ns(String ns, String key, float value);
float load_config_float_ns(String ns, String key);

void main_load_config();
void main_save_config();
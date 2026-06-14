#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <LEDMatrix.h>

// 固件版本号
#define FIRMWARE_VERSION "1.1.0"

// USB CDC 重定向
#if !ARDUINO_USB_CDC_ON_BOOT
#include <USBCDC.h>
extern USBCDC USBSerial;
#define Serial USBSerial
#endif

#define LED_SWITCH_PIN 4
#define DATA_PIN     48
#define TOUCH_PIN    12

#define BOTTLE_V4

// LED矩阵配置
#ifdef BOTTLE_V4
#define MATRIX_HEIGHT    8
#define MATRIX_WIDTH     32
extern cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, VERTICAL_ZIGZAG_MATRIX> leds;
#else
#define MATRIX_HEIGHT    8
#define MATRIX_WIDTH     17
extern cLEDMatrix<-MATRIX_WIDTH, -MATRIX_HEIGHT, VERTICAL_ZIGZAG_MATRIX> leds;
#endif
#define NUM_LEDS    (MATRIX_WIDTH * MATRIX_HEIGHT)

//加速度计
// #define MPU6050
#define LIS3DH

//麦克风
// #define MIC_I2S
#define MIC_PDM

//蓝牙名称和设备型号
#ifdef BOTTLE_V4
#define BLE_DEVICE_NAME "BottleLED"
#define DEVICE_MODEL "Bottle-V4"  // 设备型号，用于区分应用市场可用的应用
#else
#define BLE_DEVICE_NAME "BottleLED"
#define DEVICE_MODEL "Bottle-V1"  // 设备型号，用于区分应用市场可用的应用
#endif

//电池检测已在battery.h文件处理

extern int32_t page_index,subpage_index;

extern uint8_t brightness_max;
extern uint8_t user_brightness_max;
extern bool is_chk_bat;

// Unified max value for LED channels used by all simulators.
#define PANEL_LED_VALUE_MAX 30

int load_config(String key);
void save_config(String key,int value);

// 字符串配置支持
String load_config_string(String key);
void save_config_string(String key, String value);

// 配置定义加载（从文件读取，根据 script_path 决定文件系统）
String load_config_definition(const char* module_id, const char* script_path);

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

// 时区恢复函数（从 RTC 内存恢复时区设置）
void restore_timezone();
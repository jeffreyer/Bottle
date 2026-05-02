#include "sleep_manager.h"
#include "common.h"
#include "rgb.h"
#include "gravity.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"

uint8_t brightness_max=10;
uint8_t fade_delay_ms=20;

uint32_t s_idle_timeout_ms = IDLE_TIMEOUT_DEFAULT*1000;
static uint32_t s_last_fade_time = 0;
static bool s_initialized = false;
static bool s_fading = false;
static bool s_preparing_sleep = false;
static uint32_t last_active = 0;
static float last_gx=0,last_gy=0,last_gz=0;
static uint32_t last_p = 0;
long last_trigger_time = 0;

// 检查按键是否被按下（低电平）
bool is_key_pressed(void) {
  return false;
}

bool fade_brightness_step(void) {
  uint8_t current = rgb_get_brightness();
  
  if (current == brightness_max) {
    s_fading = false;
    return true;  // 渐变完成
  }
  
  uint32_t now = millis();
  if (now - s_last_fade_time < fade_delay_ms) {
    return false;  // 还没到时间，继续等待
  }
  
  s_last_fade_time = now;
  s_fading = true;
  
  int step = (brightness_max > current) ? BRIGHTNESS_FADE_STEP : -BRIGHTNESS_FADE_STEP;
  current += step;
  
  if ((step > 0 && current >= brightness_max) || (step < 0 && current <= brightness_max)) {
    current = brightness_max;
  }
  
  rgb_set_brightness(current);
  
  if (current == brightness_max) {
    s_fading = false;
    return true;  // 渐变完成
  }
  
  return false;  // 还在渐变中
}

// 进入深度睡眠
void enter_deep_sleep(void) {
  Serial.println("Entering deep sleep mode...");

  gravity_sensor_sleep();

  esp_deep_sleep_start();
}

int sleep_manager_init(uint8_t key_pin) {
  if (s_initialized) {
    return -1;
  }
  s_last_fade_time = 0;
  s_initialized = true;
  s_fading = false;
  s_preparing_sleep = false;

  return 0;
}

int sleep_manager_start(void) {
  if (!s_initialized) {
    return -1;
  }
  
  return 0;
}

void sleep_manager_update() {
  if (!s_initialized) {
    return;
  }
  
  uint32_t now = millis();
  
  // 如果正在准备睡眠（降低亮度），执行渐变步骤并检查是否完成
  if (s_preparing_sleep) {
    // 执行一步渐变
    bool fade_complete = fade_brightness_step();
    
    if (fade_complete) {
      // 渐变完成，检查是否降到0
      if (rgb_get_brightness() == 0) {
        s_preparing_sleep = false;
        enter_deep_sleep();
        return;
      }
    }
    return;
  }
  
  // 如果不在准备睡眠状态，但目标亮度不是当前亮度，继续渐变
  if (rgb_get_brightness() != brightness_max) {
    fade_brightness_step();
  }

  if (s_idle_timeout_ms==0)
    return;

  gravity_xy_t g = gravity_get();
  float gx = g.valid ? g.gx : 0.0f;
  float gy = g.valid ? g.gy : 0.0f;
  float gz = g.valid ? g.gz : 0.0f;
  if (millis()-last_p>300){
    last_p=millis();
    // Serial.printf("mpu: %f %f | %f %f | %f %f\n", gx,last_gx,gy,last_gy,gz,last_gz);
  }
  if (abs(gx-last_gx)>0.02||abs(gy-last_gy)>0.02){
    last_active=now;
  }

  last_gx=gx;
  last_gy=gy;
  last_gz=gz;

  uint32_t elapsed = now - last_active;
  
  if (elapsed >= s_idle_timeout_ms) {
    // 开始准备睡眠：设置目标亮度为0，开始非阻塞渐变
    if (rgb_get_brightness() > 0) {
      s_preparing_sleep = true;
      fade_delay_ms=200/brightness_max;
      brightness_max = 0;
      s_last_fade_time = 0;  // 立即开始渐变
      Serial.println("Starting sleep preparation: fading brightness to 0");
    } else {
      // 亮度已经是0，直接进入睡眠
      enter_deep_sleep();
    }
    return;
  }
}


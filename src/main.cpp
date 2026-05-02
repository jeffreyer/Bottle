#include "common.h"
#include "rhythm.h"
#include "rgb.h"
#include "touch.h"
// #include "record.h"
#include <Wire.h>
#include "esp_sleep.h"
#include "sim_manager.h"
#include "sleep_manager.h"
#include "candle.h"
#include "text.h"
#include "sandglass.h"
#include "breakout.h"
#include "ble_config.h"
#include "app_control.h"
#include <Preferences.h>

uint8_t btn_status=0; //1 click,2 long click,3 sleep
uint32_t tm_touch_begin;

typedef int (*FuncPtr)();

FuncPtr func_setup[]={setup_rhythm, setup_fluid, setup_candle, setup_sand, setup_breakout, /*setup_text*/};
FuncPtr func_unload[]={unload_rhythm, unload_fluid, unload_candle, unload_sand, unload_breakout, /*unload_text*/};
FuncPtr func_loop[]={draw_rtythm, fluid_loop, candle_loop, sand_loop, breakout_loop, /*unload_text*/};

uint8_t page_cnt=sizeof(func_setup)/sizeof(func_setup[0]);

void main_load_config(){
  Preferences prefs;
  prefs.begin("bottle", true);
  page_index = prefs.getInt("page_index");
  user_brightness_max = prefs.getInt("brightness", user_brightness_max);
  brightness_max = user_brightness_max;
  s_idle_timeout_ms = prefs.getInt("sleep_sec",IDLE_TIMEOUT_DEFAULT)*1000;
  prefs.end();
}

void main_save_config(){
  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt("page_index",page_index);
  prefs.putInt("brightness",user_brightness_max);
  prefs.end();

  func_unload[page_index]();
}

int32_t app_get_page_count(void) {
  return page_cnt;
}

void app_set_subpage(int32_t subpage) {
  subpage_index = subpage;
}

void app_set_page(int32_t page, int32_t subpage) {
  if (page < 0 || page >= page_cnt) {
    return;
  }

  if (page == page_index) {
    subpage_index = subpage;
    return;
  }

  func_unload[page_index]();
  page_index = page;
  subpage_index = subpage;
  func_setup[page_index]();
  brightness_max = user_brightness_max;
  rgb_set_brightness(brightness_max);

  Preferences prefs;
  prefs.begin("bottle", false);
  prefs.putInt("page_index", page_index);
  prefs.end();
}

static bool touch_on_active_cb(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx)
{
    tm_touch_begin=millis();
    return false;
}

static bool touch_on_inactive_cb(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx)
{
    uint32_t now=millis();
    if (now-tm_touch_begin>4800){
      btn_status=3;
    }
    else if (now-tm_touch_begin>800){
      btn_status=2;
    }
    else if (now-tm_touch_begin>100){
      btn_status=1;
    }
    
    tm_touch_begin=0;
    return false;
}

void check_btn(){
  uint32_t now = millis();
  if (tm_touch_begin>0 && millis()-tm_touch_begin>5000){
    main_save_config();
    enter_deep_sleep();
  }
  if (btn_status==1){
    btn_status=0;
    if (ble_config_handle_tap()) {
      return;
    }
    if (ble_config_is_enabled()) {
      return;
    }
    subpage_index++;
    // Serial.println(subpage_index);
  }
  else if (btn_status==2){
    if (ble_config_is_enabled()) {
      btn_status=0;
      return;
    }
    btn_status=0;
    int ret=func_unload[page_index]();
    // Serial.printf("unload %d:%d\n",page_index,ret);
    
    page_index++;
    if (page_index>=page_cnt)
      page_index=0;
    subpage_index=0;

    // Serial.printf("page:%d\n",page_index);
    ret=func_setup[page_index]();
    brightness_max = user_brightness_max;
    FastLED.setBrightness(brightness_max);
    // Serial.printf("setup %d:%d\n",page_index,ret);

  }
  else if (btn_status==3){
    main_save_config();
    enter_deep_sleep();
  }
}

void check_cmd(){
  if (Serial.available() > 0) {
    String command = Serial.readString();

    if (command.startsWith("sleep=")) {
      int sec=command.substring(6).toInt();
      save_config("sleep_sec",sec);
      s_idle_timeout_ms=sec*1000;
      Serial.printf("set sleep delay seconds:%d",sec);
    }
  }

}

void setup() {
  pinMode(LED_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_SWITCH_PIN, LOW);

  main_load_config();

  rgb_init();

  Serial.begin(115200);
  
  // record();

  touch_sleep_init(touch_on_active_cb,touch_on_inactive_cb);

  func_setup[page_index]();

  sleep_manager_init(0);
  sleep_manager_start();
}

void loop() {

  check_btn();

  check_cmd();

  ble_config_update();

  if (ble_config_is_enabled()) {
    ble_config_render_mode();
    delay(30);
    return;
  }

  sleep_manager_update();

  func_loop[page_index]();

  delay(10);

}

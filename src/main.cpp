#include "common.h"
#include "rgb.h"
#include "touch.h"
#include "touch_icons.h"
#include <Wire.h>
#include "esp_sleep.h"
#include "sim_manager.h"
#include "sleep_manager.h"
#include "ble_config.h"
#include "app_control.h"
#include "module_registry.h"
#include <Preferences.h>

// Button status enumeration for better code readability
enum ButtonStatus {
  BTN_NONE = 0,      // No action
  BTN_CLICK = 1,     // Short click (subpage switch)
  BTN_MODULE = 2,    // Long press - switch module
  BTN_SLEEP = 3,     // Very long press - enter sleep
  BTN_BLE = 4        // Long press - toggle BLE config
};

ButtonStatus btn_status = BTN_NONE;
uint32_t tm_touch_begin;
uint8_t touch_hold_hint = 0;
uint8_t touch_hold_cycle = 0; //0=module, 1=ble, 2=sleep
uint32_t touch_hold_cycle_time = 0;

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

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->unload) {
    module->unload();
  }
}

int32_t app_get_page_count(void) {
  return module_registry_count();
}

int32_t app_get_page_index(void) {
  return page_index;
}

int32_t app_get_subpage_index(void) {
  return subpage_index;
}

void app_set_subpage(int32_t subpage) {
  subpage_index = subpage;
  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && String(module->id) == "rhythm") {
    save_config_ns("rhythm", "style", subpage_index);
  }
}

bool app_set_module_enabled(int32_t page, bool enabled) {
  if (page < 0 || page >= module_registry_count()) {
    return false;
  }

  bool was_enabled = module_registry_is_enabled((uint8_t)page);
  module_registry_set_enabled((uint8_t)page, enabled);
  bool now_enabled = module_registry_is_enabled((uint8_t)page);
  if (was_enabled == now_enabled) {
    return now_enabled == enabled;
  }

  if (!now_enabled && page == page_index) {
    const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
    if (current && current->unload) {
      current->unload();
    }
    page_index = module_registry_normalize_index(page_index);
    subpage_index = 0;
    const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
    if (next && next->setup) {
      next->setup();
    }
    brightness_max = user_brightness_max;
    rgb_set_brightness(brightness_max);
  }

  return true;
}

void app_set_page(int32_t page, int32_t subpage) {
  if (page < 0 || page >= module_registry_count()) {
    return;
  }
  if (!module_registry_is_enabled((uint8_t)page)) {
    return;
  }

  if (page == page_index) {
    app_set_subpage(subpage);
    return;
  }

  const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
  if (current && current->unload) {
    current->unload();
  }
  page_index = page;
  subpage_index = subpage;
  const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
  if (next && next->setup) {
    next->setup();
  }
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
    touch_hold_hint=0;
    return false;
}

static bool touch_on_inactive_cb(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx)
{
    uint32_t now=millis();
    if (now-tm_touch_begin>800){
      // User held for more than 800ms, use the cycle indicator to determine action
      bool in_ble_mode = ble_config_is_enabled();

      Serial.print("Touch released after long press. BLE mode: ");
      Serial.print(in_ble_mode);
      Serial.print(", cycle: ");
      Serial.println(touch_hold_cycle);

      if (in_ble_mode) {
        // In BLE mode: cycle 0 = BLE toggle, cycle 1 = Sleep
        if (touch_hold_cycle == 0) {
          btn_status = BTN_BLE;
          Serial.println("Set btn_status = BTN_BLE (turn off)");
        } else if (touch_hold_cycle == 1) {
          btn_status = BTN_SLEEP;
          Serial.println("Set btn_status = BTN_SLEEP");
        }
      } else {
        // Normal mode: cycle 0 = Module, cycle 1 = BLE, cycle 2 = Sleep
        if (touch_hold_cycle == 0) {
          btn_status = BTN_MODULE;
          Serial.println("Set btn_status = BTN_MODULE");
        } else if (touch_hold_cycle == 1) {
          btn_status = BTN_BLE;
          Serial.println("Set btn_status = BTN_BLE (turn on)");
        } else if (touch_hold_cycle == 2) {
          btn_status = BTN_SLEEP;
          Serial.println("Set btn_status = BTN_SLEEP");
        }
      }
    }
    else if (now-tm_touch_begin>100){
      btn_status = BTN_CLICK;
    }

    tm_touch_begin=0;
    touch_hold_hint=0;
    touch_hold_cycle=0;
    return false;
}

void check_btn(){
  uint32_t now = millis();
  if (tm_touch_begin>0) {
    uint32_t held = now - tm_touch_begin;
    if (held > 800 && touch_hold_hint < 1) {
      // Start cycling through icons
      touch_hold_hint = 1;
      bool in_ble_mode = ble_config_is_enabled();
      // In BLE mode, start from BLE icon (cycle 0), otherwise start from module icon (cycle 0)
      touch_hold_cycle = 0;
      touch_hold_cycle_time = now;

      if (in_ble_mode) {
        show_ble_hold_hint(true);  // Show slash in BLE mode (turn off)
      } else {
        show_module_hold_hint();
      }
    } else if (held > 15000) {
      btn_status = BTN_SLEEP;
    } else if (held > 800) {
      // Cycling mode: update icon every 1000ms
      if (now - touch_hold_cycle_time > 1000) {
        bool in_ble_mode = ble_config_is_enabled();

        if (in_ble_mode) {
          // In BLE mode: cycle between BLE (0) and Sleep (1)
          touch_hold_cycle = (touch_hold_cycle + 1) % 2;
          touch_hold_cycle_time = now;

          if (touch_hold_cycle == 0) {
            show_ble_hold_hint(true);  // Show slash in BLE mode (turn off)
          } else if (touch_hold_cycle == 1) {
            show_sleep_hold_hint();
          }
        } else {
          // Normal mode: cycle between Module (0), BLE (1), Sleep (2)
          touch_hold_cycle = (touch_hold_cycle + 1) % 3;
          touch_hold_cycle_time = now;

          if (touch_hold_cycle == 0) {
            show_module_hold_hint();
          } else if (touch_hold_cycle == 1) {
            show_ble_hold_hint(false);  // No slash in normal mode (turn on)
          } else if (touch_hold_cycle == 2) {
            show_sleep_hold_hint();
          }
        }
      }
    }
  }
  if (btn_status == BTN_CLICK){
    btn_status = BTN_NONE;
    if (ble_config_is_enabled()) {
      return;
    }
    subpage_index++;
  }
  else if (btn_status == BTN_MODULE){
    if (ble_config_is_enabled()) {
      btn_status = BTN_NONE;
      return;
    }
    btn_status = BTN_NONE;
    const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
    int ret = 0;
    if (current && current->unload) {
      ret = current->unload();
    }
    // Serial.printf("unload %d:%d\n",page_index,ret);

    page_index = module_registry_next_enabled(page_index);
    if (page_index < 0) {
      page_index = module_registry_normalize_index(0);
    }
    subpage_index=0;

    // Serial.printf("page:%d\n",page_index);
    const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
    if (next && next->setup) {
      ret = next->setup();
    }
    brightness_max = user_brightness_max;
    FastLED.setBrightness(brightness_max);
    Serial.printf("[main] After module setup, set brightness to user_brightness_max=%d\n", user_brightness_max);
    // Serial.printf("setup %d:%d\n",page_index,ret);

  }
  else if (btn_status == BTN_BLE){
    Serial.print("Processing BTN_BLE, current BLE state: ");
    Serial.println(ble_config_is_enabled() ? "enabled" : "disabled");
    btn_status = BTN_NONE;
    ble_config_toggle();
    if (!ble_config_is_enabled()){ //退出蓝牙后重新启动模块
      const module_descriptor_t* current = module_registry_get((uint8_t)page_index);
      int ret;
      if (current && current->unload) {
        ret = current->unload();
      }
      if (current && current->setup) {
        ret = current->setup();
      }
    }
  }
  else if (btn_status == BTN_SLEEP){
    // Sleep action - save config and enter deep sleep
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
  Serial.begin(115200);

  pinMode(LED_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_SWITCH_PIN, LOW);

  main_load_config();

  module_registry_init();

  page_index = module_registry_normalize_index(page_index);
  Serial.printf("[Setup] Page index: %d\n", page_index);

  rgb_init();

  touch_sleep_init(touch_on_active_cb,touch_on_inactive_cb);

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->setup) {
    Serial.printf("[Setup] Running module setup for: %s\n", module->name);
    module->setup();
  }

  // Restore user brightness after module setup
  brightness_max = user_brightness_max;
  FastLED.setBrightness(brightness_max);
  Serial.printf("[Setup] Restored user brightness to %d\n", user_brightness_max);

  sleep_manager_init(0);

  sleep_manager_start();
}

void loop() {

  check_btn();

  if (tm_touch_begin > 0 && touch_hold_hint > 0) {
    delay(10);
    return;
  }

  check_cmd();

  ble_config_update();

  if (ble_config_is_enabled()) {
    ble_config_render_mode();
    delay(30);
    return;
  }

  sleep_manager_update();

  // Skip module rendering when showing touch hold hint
  if (tm_touch_begin > 0 && touch_hold_hint > 0) {
    delay(10);
    return;
  }

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->loop) {
    module->loop();
  }

  delay(10);

}

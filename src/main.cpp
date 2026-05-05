#include "common.h"
#include "rhythm.h"
#include "rgb.h"
#include "touch.h"
#include "touch_icons.h"
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
#include "module_registry.h"
#include "module_storage.h"
#include <Preferences.h>

uint8_t btn_status=0; //1 click,2 module,3 sleep,4 ble
uint32_t tm_touch_begin;
uint8_t touch_hold_hint = 0;
uint8_t touch_hold_cycle = 0; //0=module, 1=ble, 2=sleep
uint32_t touch_hold_cycle_time = 0;

static void flash_fill(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  FastLED.clear();
  rgb_set_brightness(brightness_max);
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      rgb_set(x, y, r, g, b);
    }
  }
  FastLED.show();
  delay(ms);
  FastLED.clear();
  FastLED.show();
}

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
    save_config("style", subpage_index);
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
      if (touch_hold_cycle == 0) {
        btn_status=2;  // module
      } else if (touch_hold_cycle == 1) {
        btn_status=4;  // ble
      } else if (touch_hold_cycle == 2) {
        btn_status=3;  // sleep (but won't actually sleep based on new check_btn logic)
      }
    }
    else if (now-tm_touch_begin>100){
      btn_status=1;
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
      touch_hold_cycle = 0;
      touch_hold_cycle_time = now;
      show_module_hold_hint();
    } else if (held > 800) {
      // Cycling mode: update icon every 1000ms
      if (now - touch_hold_cycle_time > 1000) {
        touch_hold_cycle = (touch_hold_cycle + 1) % 3;
        touch_hold_cycle_time = now;
        
        if (touch_hold_cycle == 0) {
          show_module_hold_hint();
        } else if (touch_hold_cycle == 1) {
          show_ble_hold_hint();
        } else if (touch_hold_cycle == 2) {
          show_sleep_hold_hint();
        }
      }
    }
  }
  if (btn_status==1){
    btn_status=0;
    if (ble_config_is_enabled()) {
      return;
    }
    subpage_index++;
    const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
    if (module && String(module->id) == "rhythm") {
      subpage_index = subpage_index % 4;
      save_config("style", subpage_index);
      flash_style_hint((uint8_t)subpage_index);
      Serial.printf("style=%d\n", (int)subpage_index);
    }
    // Serial.println(subpage_index);
  }
  else if (btn_status==2){
    if (ble_config_is_enabled()) {
      btn_status=0;
      return;
    }
    btn_status=0;
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
  else if (btn_status==4){
    btn_status=0;
    bool was_enabled = ble_config_is_enabled();
    ble_config_toggle();
    if (was_enabled) {
      flash_fill(0, 70, 20, 100);
    }
  }
  else if (btn_status==3){
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
    } else if (command.startsWith("status")) {
      Serial.println(module_registry_status_json());
    } else if (command.startsWith("manifest=")) {
      int index = command.substring(9).toInt();
      Serial.println(module_registry_manifest_json(index));
    } else if (command.startsWith("rhythm_status")) {
      Serial.println(module_registry_manifest_json(0));
    }
  }

}

void setup() {
  Serial.begin(115200);
  delay(5000);  // Wait for serial monitor to connect
  Serial.println("\n\n=== Bottle System Starting ===");

  pinMode(LED_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_SWITCH_PIN, LOW);
  Serial.println("[Setup] GPIO initialized");

  main_load_config();
  Serial.println("[Setup] Config loaded");

  module_registry_init();
  Serial.println("[Setup] Module registry initialized");

  page_index = module_registry_normalize_index(page_index);
  Serial.printf("[Setup] Page index: %d\n", page_index);

  module_storage_init();
  Serial.println("[Setup] Module storage initialized");

  module_storage_ensure_defaults();
  Serial.println("[Setup] Module defaults ensured");

  rgb_init();
  Serial.println("[Setup] RGB initialized");

  // record();

  touch_sleep_init(touch_on_active_cb,touch_on_inactive_cb);
  Serial.println("[Setup] Touch sleep initialized");

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->setup) {
    Serial.printf("[Setup] Running module setup for: %s\n", module->name);
    module->setup();
  }

  sleep_manager_init(0);
  Serial.println("[Setup] Sleep manager initialized");

  sleep_manager_start();
  Serial.println("[Setup] Sleep manager started");
  Serial.println("=== Setup Complete ===\n");
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

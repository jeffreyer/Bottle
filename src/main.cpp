#include "common.h"
#include "rgb.h"
#include "touch.h"
#include "touch_icons.h"
#include "esp_sleep.h"
#include "sim_manager.h"
#include "sleep_manager.h"
#include "ble_config.h"
#include "app_control.h"
#include "module_registry.h"
#include "lua_hardware_api.h"
#include "auto_ota.h"
#include "time_calibration.h"
#include <Preferences.h>
#include "storage_flash.h"
#include "battery.h"
#include "gravity.h"
#include "audio_fft.h"
#include "usb_msc.h"
#include "cmd_handler.h"
#include <USB.h>
#include "tusb.h"
#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;  // 定义全局对象（在 common.h 中声明为 extern）
#endif

// Button status enumeration for better code readability
enum ButtonStatus {
  BTN_NONE = 0,      // No action
  BTN_CLICK = 1,     // Short click (subpage switch)
  BTN_MODULE = 2,    // Long press - switch module
  BTN_SLEEP = 3,     // Very long press - enter sleep
  BTN_BLE = 4        // Long press - toggle BLE config
};

ButtonStatus btn_status = BTN_NONE;
uint32_t tm_chk_bat=0;
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
  is_i2s_mic = prefs.getInt("i2s_mic",0);
  is_chk_bat = prefs.getInt("chk_bat",1);
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
    uint32_t hold_duration = now - tm_touch_begin;

    // 如果当前模块声明了 button 权限，清除按住状态
    if (lua_hardware_is_button_used()) {
      lua_hardware_set_button_holding(false);
    }

    // 只有实际使用了 is_holding() 的模块才延长系统长按触发时间
    // 但在蓝牙模式下不延迟，保持快速退出蓝牙的能力
    uint32_t long_press_threshold = (lua_hardware_is_holding_used() && !ble_config_is_enabled()) ? 9000 : 800;

    if (hold_duration > long_press_threshold){
      // User held for more than 800ms, use the cycle indicator to determine action
      bool in_ble_mode = ble_config_is_enabled();

      if (in_ble_mode) {
        // In BLE mode: cycle 0 = BLE toggle, cycle 1 = Sleep
        if (touch_hold_cycle == 0) {
          btn_status = BTN_BLE;
        } else if (touch_hold_cycle == 1) {
          btn_status = BTN_SLEEP;
        }
      } else {
        // Normal mode: cycle 0 = Module, cycle 1 = BLE, cycle 2 = Sleep
        if (touch_hold_cycle == 0) {
          btn_status = BTN_MODULE;
        } else if (touch_hold_cycle == 1) {
          btn_status = BTN_BLE;
        } else if (touch_hold_cycle == 2) {
          btn_status = BTN_SLEEP;
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

    // 如果当前模块声明了 button 权限，在按住超过300ms后设置holding状态
    if (lua_hardware_is_button_used() && held > 300) {
      lua_hardware_set_button_holding(true);
    }

    // 只有实际使用了 is_holding() 的模块才延长系统长按触发时间，避免与 Lua 脚本的长按逻辑冲突
    // 但在蓝牙模式下不延迟，保持快速退出蓝牙的能力
    uint32_t long_press_threshold = (lua_hardware_is_holding_used() && !ble_config_is_enabled()) ? 9000 : 800;

    if (held > long_press_threshold && touch_hold_hint < 1) {
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
    } else if (held > long_press_threshold) {
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
    // 检查当前模块是否声明了 button 权限
    if (lua_hardware_is_button_used()) {
      lua_hardware_send_button_event(1);  // 1 = click
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
    if (current && current->unload) {
      current->unload();
    }

    page_index = module_registry_next_enabled(page_index);
    if (page_index < 0) {
      page_index = module_registry_normalize_index(0);
    }
    subpage_index=0;

    const module_descriptor_t* next = module_registry_get((uint8_t)page_index);
    if (next && next->setup) {
      next->setup();
    }
    brightness_max = user_brightness_max;
    FastLED.setBrightness(brightness_max);

  }
  else if (btn_status == BTN_BLE){
    btn_status = BTN_NONE;
    ble_config_toggle();
    if (!ble_config_is_enabled()){ //退出蓝牙后重新启动模块
      // 重置休眠计时器，避免立即休眠
      sleep_manager_reset_idle_timer();

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

void setup() {
  // 1. 先初始化 USB CDC
  #if !ARDUINO_USB_CDC_ON_BOOT
  USBSerial.begin();
  #endif

  // 2. 启动 USB 栈（仅 CDC，不启动 MSC）
  USB.begin();
  Serial.setDebugOutput(true);
  log_e("=== Bottle System Starting ===");
  log_e("Firmware Version: %s", FIRMWARE_VERSION);
  log_e("Build Date: %s %s", __DATE__, __TIME__);

  // 5. 初始化存储（正常挂载文件系统供设备使用）
  storage_init();

  pinMode(LED_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_SWITCH_PIN, LOW);
  rgb_init();

  // 6. 检查并执行自动 OTA 更新（如果有 firmware.bin）
  if (auto_ota_check_and_update()) {
    // OTA 更新执行中或失败，函数内部会处理重启或清理
    // 如果到这里说明更新失败，继续正常启动
    log_e("[Setup] OTA update failed or completed, continuing normal boot");
  }

  // 7. 恢复时区设置
  restore_timezone();

  // 8. 初始化时间校准模块
  TimeCalibration::init();

  if (usb_msc_init()) {
    log_e("[Setup] USB MSC enabled - device is now a USB drive");
  } else {
    log_e("[Setup] Failed to enable USB MSC");
  }

  if (is_chk_bat)
    check_battery_init();

  main_load_config();

  gravity_init();
  if (s_idle_timeout_ms>0)
    gravity_sensor_start();

  touch_sleep_init(touch_on_active_cb,touch_on_inactive_cb);

  module_registry_init();

  page_index = module_registry_normalize_index(page_index);

  const module_descriptor_t* module = module_registry_get((uint8_t)page_index);
  if (module && module->setup) {
    module->setup();
  }

  // Restore user brightness after module setup
  brightness_max = user_brightness_max;
  FastLED.setBrightness(brightness_max);

  sleep_manager_init();

  sleep_manager_start();
}

void loop() {

  check_btn();

  if (is_chk_bat && millis() - tm_chk_bat > 30000) { // 每30秒检查一次电池状态
    tm_chk_bat = millis();
    check_bat();
    if (is_low_bat) {
      draw_low_battery_hint();
      enter_deep_sleep();
    }
  }

  if (tm_touch_begin > 0 && touch_hold_hint > 0) {
    delay(10);
    return;
  }

  check_cmd();

  // 检测 USB 连接状态
  static bool last_usb_connected = false;
  bool usb_connected = tud_connected() && tud_mounted() && !tud_suspended();

  // 额外检测：如果 MSC 已启用但 USB 未连接，说明 USB 被拔掉了
  if (usb_msc_is_enabled() && !usb_connected) {
    static uint32_t last_disconnect_check = 0;
    if (millis() - last_disconnect_check > 1000) {
      last_disconnect_check = millis();
      log_e("[USB] MSC enabled but USB not connected - forcing deinit");
      usb_msc_deinit();
      last_usb_connected = false;
    }
  }

  if (usb_connected != last_usb_connected) {
    if (usb_connected) {
      log_e("[USB] Connected and mounted");
      if (!usb_msc_is_enabled()) {
        log_e("[USB] Auto-enabling MSC");

        usb_msc_init();
      }
    } else {
      log_e("[USB] Disconnected");
      if (usb_msc_is_enabled()) {
        log_e("[USB] Auto-disabling MSC, remounting storage");
        usb_msc_deinit();
      }
    }
    last_usb_connected = usb_connected;
  }

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

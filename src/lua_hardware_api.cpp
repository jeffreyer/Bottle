#include "lua_hardware_api.h"

#include <Arduino.h>
#include <FastLED.h>
#include "app_control.h"
#include "audio_fft.h"
#include "common.h"
#include "gravity.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace {

// 全局重力数据快照（由调用者更新）
struct GravitySnapshot {
  float x, y, z;
  bool valid;
};

GravitySnapshot g_gravity_snapshot = {0, 0, 0, false};

}  // namespace

// 更新重力传感器快照（由外部调用）
void lua_hardware_update_gravity() {
  gravity_xy_t g = gravity_get();
  g_gravity_snapshot.valid = g.valid;
  g_gravity_snapshot.x = g.gx;
  g_gravity_snapshot.y = g.gy;
  g_gravity_snapshot.z = g.gz;
}

// ============================================================================
// LED API
// ============================================================================

// led.clear()
static int lua_led_clear(lua_State* L) {
  FastLED.clear();
  return 0;
}

// led.show()
static int lua_led_show(lua_State* L) {
  FastLED.show();
  return 0;
}

// led.set(x, y, r, g, b)
static int lua_led_set(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int g = (int)luaL_checknumber(L, 4);
  int b = (int)luaL_checknumber(L, 5);

  if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
    leds(x, y) = CRGB(r, g, b);
  }
  return 0;
}

// led.hsv(h, s, v) -> returns r, g, b
static int lua_led_hsv(lua_State* L) {
  int h = (int)luaL_checknumber(L, 1);
  int s = (int)luaL_checknumber(L, 2);
  int v = (int)luaL_checknumber(L, 3);

  CRGB color = CHSV(h, s, v);
  lua_pushnumber(L, color.r);
  lua_pushnumber(L, color.g);
  lua_pushnumber(L, color.b);
  return 3;
}

static const luaL_Reg led_lib[] = {
  {"clear", lua_led_clear},
  {"show", lua_led_show},
  {"set", lua_led_set},
  {"hsv", lua_led_hsv},
  {NULL, NULL}
};

// ============================================================================
// FFT API (raw magnitude data)
// ============================================================================

// fft.get(index) -> returns magnitude at index
static int lua_fft_get(lua_State* L) {
  int index = (int)luaL_checknumber(L, 1);
  const double* magnitude = audio_fft_get_magnitude();
  int length = audio_fft_get_magnitude_length();

  if (index >= 0 && index < length) {
    lua_pushnumber(L, magnitude[index]);
  } else {
    lua_pushnumber(L, 0.0);
  }
  return 1;
}

// fft.count() -> returns FFT bin count (512)
static int lua_fft_count(lua_State* L) {
  lua_pushnumber(L, audio_fft_get_magnitude_length());
  return 1;
}

static const luaL_Reg fft_lib[] = {
  {"get", lua_fft_get},
  {"count", lua_fft_count},
  {NULL, NULL}
};

// ============================================================================
// Spectrum API (deprecated, use fft instead)
// ============================================================================

// spectrum.get(index) -> returns magnitude value
static int lua_spectrum_get(lua_State* L) {
  int index = (int)luaL_checknumber(L, 1);
  const double* magnitude = audio_fft_get_magnitude();
  int length = audio_fft_get_magnitude_length();

  if (index >= 0 && index < length) {
    lua_pushnumber(L, magnitude[index]);
  } else {
    lua_pushnumber(L, 0.0);
  }
  return 1;
}

// spectrum.count() -> returns FFT bin count (512)
static int lua_spectrum_count(lua_State* L) {
  lua_pushnumber(L, audio_fft_get_magnitude_length());
  return 1;
}

static const luaL_Reg spectrum_lib[] = {
  {"get", lua_spectrum_get},
  {"count", lua_spectrum_count},
  {NULL, NULL}
};

// ============================================================================
// Gravity API
// ============================================================================

// gravity.get() -> returns x, y, z, valid
static int lua_gravity_get(lua_State* L) {
  lua_pushnumber(L, g_gravity_snapshot.x);
  lua_pushnumber(L, g_gravity_snapshot.y);
  lua_pushnumber(L, g_gravity_snapshot.z);
  lua_pushboolean(L, g_gravity_snapshot.valid);
  return 4;
}

static const luaL_Reg gravity_lib[] = {
  {"get", lua_gravity_get},
  {NULL, NULL}
};

// ============================================================================
// Config API
// ============================================================================

// config.get(key) -> returns int value
static int lua_config_get(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  int value = load_config(key);
  lua_pushnumber(L, value);
  return 1;
}

static const luaL_Reg config_lib[] = {
  {"get", lua_config_get},
  {NULL, NULL}
};

// ============================================================================
// Time API
// ============================================================================

// time.millis() -> returns milliseconds since boot
static int lua_time_millis(lua_State* L) {
  lua_pushnumber(L, millis());
  return 1;
}

static const luaL_Reg time_lib[] = {
  {"millis", lua_time_millis},
  {NULL, NULL}
};

// ============================================================================
// Math Extensions
// ============================================================================

// math.clamp(value, min, max)
static int lua_math_clamp(lua_State* L) {
  lua_Number value = luaL_checknumber(L, 1);
  lua_Number min_val = luaL_checknumber(L, 2);
  lua_Number max_val = luaL_checknumber(L, 3);
  lua_pushnumber(L, constrain(value, min_val, max_val));
  return 1;
}

// ============================================================================
// 注册所有 API
// ============================================================================

void register_lua_hardware_apis(lua_State* L) {
  // Register led library
  luaL_newlib(L, led_lib);
  lua_setglobal(L, "led");

  // Register fft library
  luaL_newlib(L, fft_lib);
  lua_setglobal(L, "fft");

  // Register spectrum library (deprecated)
  luaL_newlib(L, spectrum_lib);
  lua_setglobal(L, "spectrum");

  // Register gravity library
  luaL_newlib(L, gravity_lib);
  lua_setglobal(L, "gravity");

  // Register config library
  luaL_newlib(L, config_lib);
  lua_setglobal(L, "config");

  // Register time library
  luaL_newlib(L, time_lib);
  lua_setglobal(L, "time");

  // Add clamp to math library
  lua_getglobal(L, "math");
  lua_pushcfunction(L, lua_math_clamp);
  lua_setfield(L, -2, "clamp");
  lua_pop(L, 1);

  // Register constants
  lua_pushnumber(L, MATRIX_WIDTH);
  lua_setglobal(L, "WIDTH");

  lua_pushnumber(L, MATRIX_HEIGHT);
  lua_setglobal(L, "HEIGHT");
}

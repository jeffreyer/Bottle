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

// led.palette(palette_table, index, brightness) -> returns r, g, b
// Interpolates color from a palette table
// palette_table format: {{index, r, g, b}, {index, r, g, b}, ...}
static int lua_led_palette(lua_State* L) {
  // Arg 1: palette table
  luaL_checktype(L, 1, LUA_TTABLE);
  // Arg 2: color index (0-255)
  int color_index = (int)luaL_checknumber(L, 2);
  // Arg 3: brightness (0-255, optional, default 255)
  int brightness = 255;
  if (lua_gettop(L) >= 3) {
    brightness = (int)luaL_checknumber(L, 3);
  }

  // Clamp inputs
  color_index = constrain(color_index, 0, 255);
  brightness = constrain(brightness, 0, 255);

  // Read palette entries
  int palette_size = lua_rawlen(L, 1);
  if (palette_size < 1) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  // Find the two entries to interpolate between
  int entry1_idx = -1;
  int entry1_pos = 0, entry1_r = 0, entry1_g = 0, entry1_b = 0;
  int entry2_idx = -1;
  int entry2_pos = 255, entry2_r = 0, entry2_g = 0, entry2_b = 0;

  for (int i = 1; i <= palette_size; i++) {
    lua_rawgeti(L, 1, i);  // Get palette[i]
    if (lua_istable(L, -1)) {
      lua_rawgeti(L, -1, 1);  // Get position
      int pos = (int)lua_tonumber(L, -1);
      lua_pop(L, 1);

      if (pos <= color_index) {
        entry1_idx = i;
        entry1_pos = pos;
        lua_rawgeti(L, -1, 2); entry1_r = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 3); entry1_g = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 4); entry1_b = (int)lua_tonumber(L, -1); lua_pop(L, 1);
      }

      if (pos >= color_index && entry2_idx == -1) {
        entry2_idx = i;
        entry2_pos = pos;
        lua_rawgeti(L, -1, 2); entry2_r = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 3); entry2_g = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 4); entry2_b = (int)lua_tonumber(L, -1); lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);  // Pop palette[i]
  }

  // Handle edge cases
  if (entry1_idx == -1) {
    entry1_pos = entry2_pos;
    entry1_r = entry2_r;
    entry1_g = entry2_g;
    entry1_b = entry2_b;
  }
  if (entry2_idx == -1) {
    entry2_pos = entry1_pos;
    entry2_r = entry1_r;
    entry2_g = entry1_g;
    entry2_b = entry1_b;
  }

  // Linear interpolation
  float blend = 0.0f;
  if (entry2_pos != entry1_pos) {
    blend = (float)(color_index - entry1_pos) / (float)(entry2_pos - entry1_pos);
  }

  int r = entry1_r + (int)((entry2_r - entry1_r) * blend);
  int g = entry1_g + (int)((entry2_g - entry1_g) * blend);
  int b = entry1_b + (int)((entry2_b - entry1_b) * blend);

  // Apply brightness
  r = (r * brightness) / 255;
  g = (g * brightness) / 255;
  b = (b * brightness) / 255;

  lua_pushnumber(L, r);
  lua_pushnumber(L, g);
  lua_pushnumber(L, b);
  return 3;
}

static const luaL_Reg led_lib[] = {
  {"clear", lua_led_clear},
  {"show", lua_led_show},
  {"set", lua_led_set},
  {"hsv", lua_led_hsv},
  {"palette", lua_led_palette},
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

// time.delay(ms) -> blocking delay
static int lua_time_delay(lua_State* L) {
  unsigned long ms = luaL_checknumber(L, 1);
  delay(ms);
  return 0;
}

static const luaL_Reg time_lib[] = {
  {"millis", lua_time_millis},
  {"delay", lua_time_delay},
  {NULL, NULL}
};

static int lua_sys_page_index(lua_State* L) {
  lua_pushnumber(L, subpage_index);
  return 1;
}

static const luaL_Reg sys_lib[] = {
  {"page_index", lua_sys_page_index},
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

  luaL_newlib(L, sys_lib);
  lua_setglobal(L, "sys");

  // Register constants
  lua_pushnumber(L, MATRIX_WIDTH);
  lua_setglobal(L, "WIDTH");

  lua_pushnumber(L, MATRIX_HEIGHT);
  lua_setglobal(L, "HEIGHT");
}

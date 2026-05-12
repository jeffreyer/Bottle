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

// 资源使用标志
bool g_use_gravity = false;
bool g_use_audio = false;
bool g_use_button = false;

// 按键事件回调
struct ButtonEvent {
  enum Type { NONE = 0, CLICK = 1, LONG_PRESS = 2 };
  Type type;
  uint32_t timestamp;
};

ButtonEvent g_button_event = {ButtonEvent::NONE, 0};
bool g_button_is_holding = false;  // 按键是否正在被按住

}  // namespace

// 更新重力传感器快照（由外部调用）
void lua_hardware_update_gravity() {
  gravity_xy_t g = gravity_get();
  g_gravity_snapshot.valid = g.valid;
  g_gravity_snapshot.x = g.gx;
  g_gravity_snapshot.y = g.gy;
  g_gravity_snapshot.z = g.gz;
}

// 启动已声明的硬件资源
void lua_hardware_start_resources() {
  if (g_use_gravity) {
    Serial.println("Lua: Starting gravity sensor...");
    int result = gravity_sensor_start();
    if (result == 0) {
      Serial.println("Lua: Gravity sensor started");
    } else {
      Serial.println("Lua: Failed to start gravity sensor");
    }
  }

  if (g_use_audio) {
    Serial.println("Lua: Starting audio FFT...");
    int init_result = audio_fft_init();
    if (init_result == 0) {
      int start_result = audio_fft_start();
      if (start_result == 0) {
        Serial.println("Lua: Audio FFT started");
      } else {
        Serial.println("Lua: Failed to start audio FFT");
      }
    } else {
      Serial.println("Lua: Failed to init audio FFT");
    }
  }
}

// 停止所有硬件资源
void lua_hardware_stop_resources() {
  if (g_use_gravity) {
    Serial.println("Lua: Stopping gravity sensor...");
    gravity_sensor_sleep();
    g_use_gravity = false;
  }

  if (g_use_audio) {
    Serial.println("Lua: Stopping audio FFT...");
    audio_fft_stop();
    g_use_audio = false;
  }

  if (g_use_button) {
    Serial.println("Lua: Releasing button control...");
    g_use_button = false;
    g_button_event.type = ButtonEvent::NONE;
  }
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

// Complete 3x5 font for ASCII characters (space to z)
// Each character is 3 columns wide, 5 rows tall
// Bit 4 = top row, Bit 0 = bottom row
// All values use only bits 0-4 (0x00-0x1F range)
static const uint8_t font3x5_complete[][3] = {
  {0x00, 0x00, 0x00}, // 0x20 space
  {0x00, 0x17, 0x00}, // 0x21 !
  {0x03, 0x00, 0x03}, // 0x22 "
  {0x0A, 0x1F, 0x0A}, // 0x23 #
  {0x12, 0x1F, 0x09}, // 0x24 $
  {0x13, 0x04, 0x19}, // 0x25 %
  {0x0A, 0x15, 0x0A}, // 0x26 &
  {0x00, 0x03, 0x00}, // 0x27 '
  {0x00, 0x0E, 0x11}, // 0x28 (
  {0x11, 0x0E, 0x00}, // 0x29 )
  {0x0A, 0x04, 0x0A}, // 0x2A *
  {0x04, 0x0E, 0x04}, // 0x2B +
  {0x00, 0x18, 0x00}, // 0x2C ,
  {0x04, 0x04, 0x04}, // 0x2D -
  {0x00, 0x10, 0x00}, // 0x2E .
  {0x18, 0x04, 0x03}, // 0x2F /
  {0x1F, 0x11, 0x1F}, // 0x30 0
  {0x00, 0x1F, 0x00}, // 0x31 1
  {0x1D, 0x15, 0x17}, // 0x32 2
  {0x11, 0x15, 0x1F}, // 0x33 3
  {0x07, 0x04, 0x1F}, // 0x34 4
  {0x17, 0x15, 0x1D}, // 0x35 5
  {0x1F, 0x15, 0x1D}, // 0x36 6
  {0x01, 0x01, 0x1F}, // 0x37 7
  {0x1F, 0x15, 0x1F}, // 0x38 8
  {0x17, 0x15, 0x1F}, // 0x39 9
  {0x00, 0x0A, 0x00}, // 0x3A :
  {0x00, 0x1A, 0x00}, // 0x3B ;
  {0x04, 0x0A, 0x11}, // 0x3C <
  {0x0A, 0x0A, 0x0A}, // 0x3D =
  {0x11, 0x0A, 0x04}, // 0x3E >
  {0x01, 0x15, 0x02}, // 0x3F ?
  {0x0E, 0x15, 0x16}, // 0x40 @
  {0x1E, 0x05, 0x1E}, // 0x41 A
  {0x1F, 0x15, 0x0A}, // 0x42 B
  {0x0E, 0x11, 0x11}, // 0x43 C
  {0x1F, 0x11, 0x0E}, // 0x44 D
  {0x1F, 0x15, 0x11}, // 0x45 E
  {0x1F, 0x05, 0x01}, // 0x46 F
  {0x0E, 0x11, 0x1D}, // 0x47 G
  {0x1F, 0x04, 0x1F}, // 0x48 H
  {0x11, 0x1F, 0x11}, // 0x49 I
  {0x08, 0x10, 0x0F}, // 0x4A J
  {0x1F, 0x04, 0x1B}, // 0x4B K
  {0x1F, 0x10, 0x10}, // 0x4C L
  {0x1F, 0x02, 0x1F}, // 0x4D M
  {0x1F, 0x01, 0x1F}, // 0x4E N
  {0x0E, 0x11, 0x0E}, // 0x4F O
  {0x1F, 0x05, 0x02}, // 0x50 P
  {0x0E, 0x19, 0x1E}, // 0x51 Q
  {0x1F, 0x05, 0x1A}, // 0x52 R
  {0x12, 0x15, 0x09}, // 0x53 S
  {0x01, 0x1F, 0x01}, // 0x54 T
  {0x0F, 0x10, 0x0F}, // 0x55 U
  {0x07, 0x18, 0x07}, // 0x56 V
  {0x1F, 0x08, 0x1F}, // 0x57 W
  {0x1B, 0x04, 0x1B}, // 0x58 X
  {0x03, 0x1C, 0x03}, // 0x59 Y
  {0x19, 0x15, 0x13}, // 0x5A Z
  {0x00, 0x1F, 0x11}, // 0x5B [
  {0x03, 0x04, 0x18}, // 0x5C backslash
  {0x11, 0x1F, 0x00}, // 0x5D ]
  {0x02, 0x01, 0x02}, // 0x5E ^
  {0x10, 0x10, 0x10}, // 0x5F _
  {0x00, 0x01, 0x02}, // 0x60 `
  {0x0C, 0x14, 0x1C}, // 0x61 a (lowercase, smaller)
  {0x1F, 0x14, 0x08}, // 0x62 b
  {0x08, 0x14, 0x14}, // 0x63 c (lowercase, smaller)
  {0x08, 0x14, 0x1F}, // 0x64 d
  {0x08, 0x14, 0x18}, // 0x65 e (lowercase, smaller)
  {0x04, 0x1E, 0x05}, // 0x66 f
  {0x08, 0x14, 0x1C}, // 0x67 g (lowercase, no descender)
  {0x1F, 0x04, 0x18}, // 0x68 h
  {0x00, 0x1D, 0x00}, // 0x69 i
  {0x10, 0x1D, 0x00}, // 0x6A j (lowercase, no descender)
  {0x1F, 0x08, 0x14}, // 0x6B k
  {0x00, 0x1F, 0x10}, // 0x6C l
  {0x1C, 0x04, 0x1C}, // 0x6D m (lowercase, smaller)
  {0x1C, 0x04, 0x18}, // 0x6E n (lowercase, smaller)
  {0x08, 0x14, 0x08}, // 0x6F o (lowercase, smaller)
  {0x1C, 0x14, 0x08}, // 0x70 p (lowercase, no descender)
  {0x08, 0x14, 0x1C}, // 0x71 q (lowercase, no descender)
  {0x1C, 0x04, 0x04}, // 0x72 r (lowercase, smaller)
  {0x10, 0x14, 0x08}, // 0x73 s (lowercase, smaller)
  {0x04, 0x1E, 0x10}, // 0x74 t
  {0x0C, 0x10, 0x1C}, // 0x75 u (lowercase, smaller)
  {0x0C, 0x10, 0x0C}, // 0x76 v (lowercase, smaller)
  {0x1C, 0x08, 0x1C}, // 0x77 w (lowercase, smaller)
  {0x14, 0x08, 0x14}, // 0x78 x (lowercase, smaller)
  {0x04, 0x18, 0x04}, // 0x79 y (lowercase, no descender)
  {0x14, 0x1C, 0x14}  // 0x7A z (lowercase, smaller)
};

// Original 5-column font for other characters (space to Z)
static const uint8_t font3x5[][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // space
  {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
  {0x00, 0x07, 0x00, 0x07, 0x00}, // "
  {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
  {0x23, 0x13, 0x08, 0x64, 0x62}, // %
  {0x36, 0x49, 0x55, 0x22, 0x50}, // &
  {0x00, 0x05, 0x03, 0x00, 0x00}, // '
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
  {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
  {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
  {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
  {0x08, 0x08, 0x08, 0x08, 0x08}, // -
  {0x00, 0x60, 0x60, 0x00, 0x00}, // .
  {0x20, 0x10, 0x08, 0x04, 0x02}, // /
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
  {0x00, 0x36, 0x36, 0x00, 0x00}, // :
  {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
  {0x00, 0x08, 0x14, 0x22, 0x41}, // <
  {0x14, 0x14, 0x14, 0x14, 0x14}, // =
  {0x41, 0x22, 0x14, 0x08, 0x00}, // >
  {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
  {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
  {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
  {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
  {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
  {0x46, 0x49, 0x49, 0x49, 0x31}, // S
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
  {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
  {0x63, 0x14, 0x08, 0x14, 0x63}, // X
  {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
  {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

// led.text(x, y, text, r, g, b)
// Draws text at position (x, y) with color (r, g, b)
// Uses 3x5 font for all characters
static int lua_led_text(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* text = luaL_checkstring(L, 3);
  int r = (int)luaL_checknumber(L, 4);
  int g = (int)luaL_checknumber(L, 5);
  int b = (int)luaL_checknumber(L, 6);

  int cursor_x = x;

  for (int i = 0; text[i] != '\0'; i++) {
    unsigned char c = (unsigned char)text[i];

    // Check if character is in our font range (space to z)
    if (c >= 0x20 && c <= 0x7A) {
      int char_index = c - 0x20;
      const uint8_t* glyph = font3x5_complete[char_index];

      // Draw character (3 columns wide, 5 rows tall)
      for (int col = 0; col < 3; col++) {
        if (cursor_x + col >= MATRIX_WIDTH) break;

        for (int row = 0; row < 5; row++) {
          if (y + row >= MATRIX_HEIGHT) continue;

          // Check if pixel is set in font bitmap (read from bit 4 to bit 0)
          bool pixel = (glyph[col] >> (4 - row)) & 0x01;

          if (pixel && cursor_x + col >= 0 && y + row >= 0) {
            leds(cursor_x + col, y + row) = CRGB(r, g, b);
          }
        }
      }

      cursor_x += 4; // 3 pixels + 1 pixel spacing
    }
  }

  return 0;
}

static const luaL_Reg led_lib[] = {
  {"clear", lua_led_clear},
  {"show", lua_led_show},
  {"set", lua_led_set},
  {"hsv", lua_led_hsv},
  {"palette", lua_led_palette},
  {"text", lua_led_text},
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
// Button API
// ============================================================================

// button.poll() -> returns event_type (0=none, 1=click, 2=long_press)
static int lua_button_poll(lua_State* L) {
  int event_type = (int)g_button_event.type;
  g_button_event.type = ButtonEvent::NONE;  // 清除事件
  lua_pushnumber(L, event_type);
  return 1;
}

// button.is_holding() -> returns true if button is currently being held
static int lua_button_is_holding(lua_State* L) {
  lua_pushboolean(L, g_button_is_holding);
  return 1;
}

static const luaL_Reg button_lib[] = {
  {"poll", lua_button_poll},
  {"is_holding", lua_button_is_holding},
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
// Resource Declaration API
// ============================================================================

// use(resource_name) -> declare resource usage
static int lua_use(lua_State* L) {
  const char* resource = luaL_checkstring(L, 1);

  if (strcmp(resource, "gravity") == 0) {
    g_use_gravity = true;
    Serial.println("Lua: Declared use of gravity sensor");
  } else if (strcmp(resource, "audio") == 0) {
    g_use_audio = true;
    Serial.println("Lua: Declared use of audio FFT");
  } else if (strcmp(resource, "button") == 0) {
    g_use_button = true;
    Serial.println("Lua: Declared use of button control");
  } else {
    Serial.print("Lua: Unknown resource: ");
    Serial.println(resource);
  }

  return 0;
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

  // Register button library
  luaL_newlib(L, button_lib);
  lua_setglobal(L, "button");

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

  // Register use() function
  lua_pushcfunction(L, lua_use);
  lua_setglobal(L, "use");

  // Register constants
  lua_pushnumber(L, MATRIX_WIDTH);
  lua_setglobal(L, "WIDTH");

  lua_pushnumber(L, MATRIX_HEIGHT);
  lua_setglobal(L, "HEIGHT");
}

// 检查当前模块是否声明了 button 权限
bool lua_hardware_is_button_used() {
  return g_use_button;
}

// 发送按键事件给 Lua 模块
void lua_hardware_send_button_event(int event_type) {
  g_button_event.type = (ButtonEvent::Type)event_type;
  g_button_event.timestamp = millis();
}

// 设置按键按住状态
void lua_hardware_set_button_holding(bool holding) {
  g_button_is_holding = holding;
}


// 注入 CONFIG 全局表（从 NVS 读取配置）
void inject_lua_config_table(lua_State* L, const char* module_id) {
  if (!L || !module_id) return;

  // 创建 CONFIG 表
  lua_newtable(L);

  // 使用 module_id 作为命名空间
  String ns = String(module_id);

  // 从 NVS 读取配置定义 (使用短键名)
  String config_def_key = String("cfg_") + module_id;
  String config_def_json = load_config_string(config_def_key);

  if (config_def_json.length() == 0) {
    // 没有配置定义，设置空的 CONFIG 表
    lua_setglobal(L, "CONFIG");
    return;
  }

  // 解析配置定义 JSON（简单的手动解析）
  // 格式: [{"key":"move_interval","type":"slider",...},...]
  int pos = 0;
  while (pos < config_def_json.length()) {
    // 查找下一个配置项
    int obj_start = config_def_json.indexOf("{", pos);
    if (obj_start < 0) break;

    int obj_end = config_def_json.indexOf("}", obj_start);
    if (obj_end < 0) break;

    String config_item = config_def_json.substring(obj_start, obj_end + 1);

    // 提取 key
    int key_pos = config_item.indexOf("\"key\"");
    if (key_pos >= 0) {
      int key_start = config_item.indexOf("\"", key_pos + 6);
      if (key_start < 0) {
        pos = obj_end + 1;
        continue;
      }
      key_start += 1;

      int key_end = config_item.indexOf("\"", key_start);
      if (key_end < 0 || key_end <= key_start) {
        pos = obj_end + 1;
        continue;
      }

      String key = config_item.substring(key_start, key_end);
      if (key.length() == 0) {
        pos = obj_end + 1;
        continue;
      }

      // 提取 type
      int type_pos = config_item.indexOf("\"type\"");
      String type = "";
      if (type_pos >= 0) {
        int type_start = config_item.indexOf("\"", type_pos + 7);
        if (type_start >= 0) {
          type_start += 1;
          int type_end = config_item.indexOf("\"", type_start);
          if (type_end > type_start) {
            type = config_item.substring(type_start, type_end);
          }
        }
      }

      // 根据类型从 NVS 读取配置值（使用命名空间）
      if (type == "slider" || type == "number") {
        // 尝试读取浮点数
        float float_value = load_config_float_ns(ns, key);
        if (float_value != 0.0f) {
          lua_pushstring(L, key.c_str());
          lua_pushnumber(L, float_value);
          lua_settable(L, -3);
        } else {
          // 尝试读取整数
          int int_value = load_config_ns(ns, key);
          if (int_value != 0) {
            lua_pushstring(L, key.c_str());
            lua_pushnumber(L, int_value);
            lua_settable(L, -3);
          }
        }
      } else if (type == "text" || type == "color" || type == "select") {
        // 字符串类型
        String string_value = load_config_string_ns(ns, key);
        if (string_value.length() > 0) {
          lua_pushstring(L, key.c_str());
          lua_pushstring(L, string_value.c_str());
          lua_settable(L, -3);
        }
      } else if (type == "switch") {
        // 布尔类型
        int bool_value = load_config_ns(ns, key);
        lua_pushstring(L, key.c_str());
        lua_pushboolean(L, bool_value != 0);
        lua_settable(L, -3);
      }
    }

    pos = obj_end + 1;
  }

  // 设置为全局变量 CONFIG
  lua_setglobal(L, "CONFIG");
}

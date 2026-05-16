#include "rhythm_lua.h"

#include <Arduino.h>
#include <FastLED.h>
#include "app_control.h"
#include "audio_fft.h"
#include "common.h"
#include "gravity.h"
#include "lua_hardware_api.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace {

struct RhythmLuaHost {
  lua_State* L = nullptr;
  int saved_subpage = -1;
  bool script_loaded = false;
};

RhythmLuaHost g_host;

// Default Lua script for rhythm visualization
const char* kDefaultLuaScript = R"lua(
-- Rhythm Spectrum Visualizer - 1:1 port from rhythm.h
-- State variables
use("gravity")
use("audio")
use("button")
local num_bands = WIDTH
local num_vals = HEIGHT
local direction = 0
local color_timer = 0

local bar_height = {}
local peak_height = {}
local prev_fft_value = {}

-- Initialize arrays
for i = 0, 31 do
  bar_height[i] = 0
  peak_height[i] = 0
  prev_fft_value[i] = 0
end

-- Color Palettes (rhythm.h lines 260-278)
-- green_to_red palette
local green_red_palette = {
  {0, 173, 255, 47},      -- green
  {127, 255, 218, 0},     -- yellow
  {255, 231, 0, 0}        -- red
}

-- purple_to_blue palette
local purple_blue_palette = {
  {0, 141, 0, 100},       -- purple
  {127, 255, 192, 0},     -- yellow
  {255, 0, 5, 255}        -- blue
}

-- red_to_mistyrose palette
local red_mistyrose_palette = {
  {0, 255, 228, 225},     -- MistyRose
  {64, 255, 69, 0},       -- OrangeRed
  {127, 255, 0, 0},       -- red
  {128, 255, 0, 0},       -- red
  {192, 255, 69, 0},      -- OrangeRed
  {255, 255, 228, 225}    -- MistyRose
}

-- FFT frequency band grouping (ported from audio_fft.cpp)
-- Groups 512 FFT bins into 17 bands with boost for higher frequencies
local fft_band_ranges = {
  {6, 7},      -- Band 0: bins 6-7, avg /2
  {8, 10},     -- Band 1: bins 8-10, avg /3
  {11, 15},    -- Band 2: bins 11-15, avg /5
  {16, 20},    -- Band 3: bins 16-20, avg /5
  {21, 25},    -- Band 4: bins 21-25, avg /5
  {26, 31},    -- Band 5: bins 26-31, avg /6
  {32, 37},    -- Band 6: bins 32-37, avg /6
  {38, 43},    -- Band 7: bins 38-43, avg /6
  {44, 49},    -- Band 8: bins 44-49, avg /6
  {50, 55},    -- Band 9: bins 50-55, avg /6
  {56, 61},    -- Band 10: bins 56-61, avg /6
  {62, 67},    -- Band 11: bins 62-67, avg /6
  {68, 73},    -- Band 12: bins 68-73, avg /6
  {74, 79},    -- Band 13: bins 74-79, avg /6
  {80, 85},    -- Band 14: bins 80-85, avg /6
  {86, 91},    -- Band 15: bins 86-91, avg /6
  {92, 133}    -- Band 16: max of bins 92-133 in groups of 6, then avg /6
}

-- Load sensitivity from config
local sensitivity = (CONFIG and CONFIG.sensitivity) or 50  -- default

-- Boost multipliers for each band (from original audio_fft.cpp)
-- Load from config, or use defaults
local fft_band_boost = {}
for i = 0, 16 do
  local band_value = (CONFIG and CONFIG["band_" .. i]) or 0
  if band_value > 0 then
    print(i, band_value)
    fft_band_boost[i + 1] = band_value / 10.0  -- Convert from 0-100 to 0.0-10.0
  else
    -- Default values
    local defaults = {0.4, 0.5, 0.5, 0.5, 0.6, 0.8, 1.1, 1.1, 1.5,
                      1.7, 3.0, 3.4, 3.6, 3.6, 3.8, 3.8, 1.0}
    fft_band_boost[i + 1] = defaults[i + 1]
  end
end

-- Process raw FFT data into frequency bands
function process_fft_bands()
  local bands = {}

  -- Process bands 0-15 (average bins in range)
  for band_idx = 1, 16 do
    local range = fft_band_ranges[band_idx]
    local start_bin = range[1]
    local end_bin = range[2]
    local sum = 0
    local count = end_bin - start_bin + 1

    for bin = start_bin, end_bin do
      sum = sum + fft.get(bin)
    end

    local avg = sum / count
    -- Apply boost and scaling: boost[i] * sensitivity / 50.0
    local scaled = avg * fft_band_boost[band_idx] * sensitivity / 50.0
    bands[band_idx - 1] = math.min(255, math.max(0, math.floor(scaled)))
  end

  -- Band 16: max of bins 92-133 in groups of 6
  local high = 0
  high = math.max(high, fft.get(92) + fft.get(93) + fft.get(94) + fft.get(95) + fft.get(96) + fft.get(97))
  high = math.max(high, fft.get(98) + fft.get(99) + fft.get(100) + fft.get(101) + fft.get(102) + fft.get(103))
  high = math.max(high, fft.get(104) + fft.get(105) + fft.get(106) + fft.get(107) + fft.get(108) + fft.get(109))
  high = math.max(high, fft.get(110) + fft.get(111) + fft.get(112) + fft.get(113) + fft.get(114) + fft.get(115))
  high = math.max(high, fft.get(116) + fft.get(117) + fft.get(118) + fft.get(119) + fft.get(120) + fft.get(121))
  high = math.max(high, fft.get(122) + fft.get(123) + fft.get(124) + fft.get(125) + fft.get(126) + fft.get(127))
  high = math.max(high, fft.get(128) + fft.get(129) + fft.get(130) + fft.get(131) + fft.get(132) + fft.get(133))
  local avg_high = high / 6
  local scaled_high = avg_high * fft_band_boost[17] * sensitivity / 50.0
  bands[16] = math.min(255, math.max(0, math.floor(scaled_high)))

  return bands
end

-- Coordinate transformation based on direction (rhythm.h lines 280-297)
function get_cord(x, y)
  local mx, my
  if direction == 0 then
    mx = x
    my = y
  elseif direction == 2 then
    mx = WIDTH - x - 1
    my = HEIGHT - y - 1
  elseif direction == 1 then
    mx = WIDTH - y - 1
    my = x
  elseif direction == 3 then
    mx = y
    my = HEIGHT - x - 1
  end
  return mx, my
end

-- Bar rendering patterns (rhythm.h lines 300-340)
function green_red_bars(band, bar)
  for y = 0, bar - 1 do
    local mx, my = get_cord(band, y)
    local color_index = math.floor(y * 255 / bar)
    local r, g, b = led.palette(green_red_palette, color_index, 180)
    led.set(mx, my, r, g, b)
  end
end

function rainbow_bars(band, bar)
  for y = 0, bar - 1 do
    local mx, my = get_cord(band, y)
    local hue = math.floor(band * 255 / num_bands)
    local r, g, b = led.hsv(hue, 255, 180)
    led.set(mx, my, r, g, b)
  end
end

function half_rainbow_bars(band, bar)
  if band % 2 == 0 then
    for y = 0, bar - 1 do
      local mx, my = get_cord(band, y)
      local hue = math.floor(band * 255 / num_bands)
      local r, g, b = led.hsv(hue, 255, 180)
      led.set(mx, my, r, g, b)
    end
  else
    for y = num_vals - 1, num_vals - bar, -1 do
      local mx, my = get_cord(band, y)
      local hue = math.floor(band * 255 / num_bands)
      local r, g, b = led.hsv(hue, 255, 180)
      led.set(mx, my, r, g, b)
    end
  end
end

function changing_bars(band, bar)
  for y = 0, bar - 1 do
    local mx, my = get_cord(band, y)
    local hue = math.floor(y * 255 / HEIGHT + color_timer)
    local r, g, b = led.hsv(hue, 255, 180)
    led.set(mx, my, r, g, b)
  end
end

function center_bars(band, bar)
  if bar % 2 == 0 then bar = bar - 1 end
  local y_start = math.floor((HEIGHT - bar) / 2)
  for y = y_start, y_start + bar do
    local color_index = math.max(0, math.min(255, (y - y_start) * 255 / bar))
    local r, g, b = led.palette(red_mistyrose_palette, color_index, 180)
    led.set(band, y, r, g, b)
  end
end

-- Peak rendering patterns (rhythm.h lines 343-369)
function yellow_white_peak(band)
  local mx, my = get_cord(band, peak_height[band])
  led.set(mx, my, 180, 180, 180)  -- White peak
  if peak_height[band] > 0 then
    mx, my = get_cord(band, 0)
    led.set(mx, my, 122, 180, 33)  -- GreenYellow base
  end
end

function white_peak(band)
  local mx, my = get_cord(band, peak_height[band])
  led.set(mx, my, 180, 180, 180)
end

function half_white_peak(band)
  if band % 2 == 0 then
    local mx, my = get_cord(band, peak_height[band])
    led.set(mx, my, 180, 180, 180)
  else
    local mx, my = get_cord(band, num_vals - peak_height[band] - 1)
    led.set(mx, my, 180, 180, 180)
  end
end

function changing_peak(band)
  local mx, my = get_cord(band, peak_height[band])
  local color_index = math.floor(peak_height[band] * 255 / HEIGHT)
  local r, g, b = led.palette(purple_blue_palette, color_index, 180)
  led.set(mx, my, r, g, b)
end

-- Timing variables
local last_peak_decay = 0
local last_color_update = 0
local style = (CONFIG and CONFIG.style) or 0

function setup()
  led.clear()
  led.show()
end

function loop()
  local current_time = time.millis()
  local btn_event = button.poll()
  if btn_event == 1 then
    style = (style + 1) % 4
  end
  if style < 0 or style > 3 then
    style = 0
  end

  led.clear()

  -- Read gravity and determine direction (rhythm.h lines 374-398)
  local gx, gy, gz, valid = gravity.get()
  if not valid then
    gx, gy, gz = 0, 0, 0
  end

  if gy > 0.7 then
    num_bands = HEIGHT
    num_vals = WIDTH
    direction = 1
  elseif gy < -0.7 then
    num_bands = HEIGHT
    num_vals = WIDTH
    direction = 3
  end

  if gx > 0.7 then
    num_bands = WIDTH
    num_vals = HEIGHT
    direction = 2
  elseif gx < -0.7 then
    num_bands = WIDTH
    num_vals = HEIGHT
    direction = 0
  end

  -- Process FFT data (rhythm.h lines 400-421)
  local fft_bands = process_fft_bands()  -- Get 17 processed bands

  if direction % 2 == 1 then
    -- Vertical orientation: merge adjacent bands
    for i = 0, HEIGHT - 1 do
      local fft_value
      if i * 2 + 1 < 17 then
        fft_value = (fft_bands[i * 2] + fft_bands[i * 2 + 1]) / 2
      else
        fft_value = fft_bands[i * 2]
      end
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4
      bar_height[i] = math.floor(fft_value / (255 // (WIDTH - 1)))
      if bar_height[i] > peak_height[i] then
        peak_height[i] = math.min(WIDTH - 1, bar_height[i])
      end
      prev_fft_value[i] = fft_value
    end
  else
    -- Horizontal orientation: use all bands
    for i = 0, WIDTH - 1 do
      local fft_value = fft_bands[i] or 0
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4
      bar_height[i] = math.floor(fft_value / (255 // (HEIGHT - 1)))
      if bar_height[i] > peak_height[i] then
        peak_height[i] = math.min(HEIGHT - 1, bar_height[i])
      end
      prev_fft_value[i] = fft_value
    end
  end

  -- Render bars and peaks (rhythm.h lines 423-446)
  for band = 0, num_bands - 1 do
    if style == 0 then
      green_red_bars(band, bar_height[band])
      yellow_white_peak(band)
    elseif style == 1 then
      rainbow_bars(band, bar_height[band])
      white_peak(band)
    elseif style == 2 then
      half_rainbow_bars(band, bar_height[band])
      half_white_peak(band)
    elseif style == 3 then
      changing_bars(band, bar_height[band])
      changing_peak(band)
      -- Update color timer every 80ms
      if current_time - last_color_update >= 80 then
        last_color_update = current_time
        color_timer = (color_timer + 1) % 256
      end
    end
  end

  -- Peak decay every 100ms (rhythm.h lines 448-451)
  if current_time - last_peak_decay >= 100 then
    last_peak_decay = current_time
    for band = 0, num_bands - 1 do
      if peak_height[band] > 0 then
        peak_height[band] = peak_height[band] - 1
      end
    end
  end

  led.show()
end

function unload()
  led.clear()
  led.show()
end
)lua";

}  // namespace

int setup_rhythm_lua_module(void) {
  
  // Set brightness
  brightness_max = 10;
  FastLED.setBrightness(10);

  // Initialize Lua
  Serial.println("[rhythm_lua] Initializing Lua...");
  g_host.L = luaL_newstate();
  if (!g_host.L) {
    Serial.println("[rhythm_lua] ERROR: Failed to create Lua state");
    return -5;
  }

  // Open standard libraries
  luaL_openlibs(g_host.L);

  // Register hardware APIs (from common library)
  register_lua_hardware_apis(g_host.L);

  // Inject CONFIG table from NVS
  inject_lua_config_table(g_host.L, "rhythm");

  // Load and execute script
  Serial.println("[rhythm_lua] Loading Lua script...");
  if (luaL_dostring(g_host.L, kDefaultLuaScript) != LUA_OK) {
    const char* error = lua_tostring(g_host.L, -1);
    Serial.printf("[rhythm_lua] ERROR: %s\n", error);
    lua_close(g_host.L);
    g_host.L = nullptr;
    return -6;
  }

  g_host.script_loaded = true;
  Serial.println("[rhythm_lua] Lua script loaded successfully");

  // Call setup function
  lua_getglobal(g_host.L, "setup");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[rhythm_lua] ERROR in setup: %s\n", error);
      lua_pop(g_host.L, 1);
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

int unload_rhythm_lua_module(void) {
  // Call unload function
  if (g_host.L && g_host.script_loaded) {
    lua_getglobal(g_host.L, "unload");
    if (lua_isfunction(g_host.L, -1)) {
      lua_pcall(g_host.L, 0, 0, 0);
    } else {
      lua_pop(g_host.L, 1);
    }

    lua_close(g_host.L);
    g_host.L = nullptr;
    g_host.script_loaded = false;
  }

  lua_hardware_stop_resources();
  return 0;
}

int loop_rhythm_lua_module(void) {
  if (!g_host.L || !g_host.script_loaded) {
    return 0;
  }

  // Update gravity snapshot for Lua API
  lua_hardware_update_gravity();

  // Call loop function
  lua_getglobal(g_host.L, "loop");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[rhythm_lua] ERROR in loop: %s\n", error);
      lua_pop(g_host.L, 1);
      g_host.script_loaded = false;  // Stop executing after error
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

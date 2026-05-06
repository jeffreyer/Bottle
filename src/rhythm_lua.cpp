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

const char* kDefaultConfigKey = "style";

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
    local hue = math.floor(y * 80 / HEIGHT + 70)
    local r, g, b = led.hsv(hue, 255, 180)
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
  local hue = math.floor(peak_height[band] * 255 / HEIGHT)
  local r, g, b = led.hsv(hue, 255, 180)
  led.set(mx, my, r, g, b)
end

-- Timing variables
local last_peak_decay = 0
local last_color_update = 0

function setup()
  led.clear()
  led.show()
end

function loop()
  local current_time = time.millis()
  local style = config.get("style")

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
  if direction % 2 == 1 then
    -- Vertical orientation: merge adjacent bands
    for i = 0, HEIGHT - 1 do
      local fft_value
      if i * 2 + 1 < WIDTH then
        fft_value = (spectrum.get(i * 2) + spectrum.get(i * 2 + 1)) / 2
      else
        fft_value = spectrum.get(i * 2)
      end
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4
      bar_height[i] = math.floor(fft_value / (255 / (WIDTH - 1)))
      if bar_height[i] > peak_height[i] then
        peak_height[i] = math.min(WIDTH - 1, bar_height[i])
      end
      prev_fft_value[i] = fft_value
    end
  else
    -- Horizontal orientation: use all bands
    for i = 0, WIDTH - 1 do
      local fft_value = spectrum.get(i)
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4
      bar_height[i] = math.floor(fft_value / (255 / (HEIGHT - 1)))
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
  // Initialize gravity sensor
  gravity_init();
  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("[rhythm_lua] MPU start failed");
  }

  // Initialize audio FFT
  audio_fft_init();
  err = audio_fft_start();
  if (err != 0) {
    Serial.printf("[rhythm_lua] Audio FFT start failed: %d\n", err);
    return err;
  }

  // Set brightness
  brightness_max = 10;
  FastLED.setBrightness(10);

  // Load saved config
  if (g_host.saved_subpage < 0) {
    g_host.saved_subpage = load_config(kDefaultConfigKey);
  }
  subpage_index = g_host.saved_subpage;

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
  g_host.saved_subpage = subpage_index;
  save_config(kDefaultConfigKey, subpage_index);

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

  gravity_sensor_sleep();
  audio_fft_stop();
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

String rhythm_lua_module_runtime_status_json(void) {
  String s = "{";
  s += "\"script_loaded\":" + String(g_host.script_loaded ? "true" : "false");
  s += ",\"engine\":\"lua-5.4.7\"";
  s += ",\"style\":" + String(subpage_index);
  s += "}";
  return s;
}

String rhythm_lua_module_configs_json(void) {
  String s = "[";
  s += "{";
  s += "\"key\":\"style\"";
  s += ",\"label\":\"Style\"";
  s += ",\"type\":\"select\"";
  s += ",\"default\":0";
  s += ",\"options\":\"Green Peak|Rainbow|Split Rainbow|Color Flow\"";
  s += "}";
  s += "]";
  return s;
}

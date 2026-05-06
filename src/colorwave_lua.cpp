#include "colorwave_lua.h"

#include <Arduino.h>
#include <FastLED.h>
#include "app_control.h"
#include "common.h"
#include "gravity.h"
#include "lua_hardware_api.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace {

const char* kDefaultConfigKey = "wave_style";

struct ColorWaveLuaHost {
  lua_State* L = nullptr;
  int saved_subpage = -1;
  bool script_loaded = false;
};

ColorWaveLuaHost g_host;

// Lua script for colorwave visualization
const char* kDefaultLuaScript = R"lua(
-- ColorWave - Full screen color scrolling effect
-- State variables
local phase = 0
local direction = 0  -- 0: horizontal, 1: vertical, 2: diagonal, 3: radial

-- Rainbow palette (full spectrum)
local rainbow_palette = {
  {0, 255, 0, 0},       -- red
  {42, 255, 165, 0},    -- orange
  {85, 255, 255, 0},    -- yellow
  {127, 0, 255, 0},     -- green
  {170, 0, 255, 255},   -- cyan
  {212, 0, 0, 255},     -- blue
  {255, 255, 0, 255}    -- magenta
}

-- Fire palette (warm colors)
local fire_palette = {
  {0, 0, 0, 0},         -- black
  {64, 128, 0, 0},      -- dark red
  {128, 255, 0, 0},     -- red
  {192, 255, 128, 0},   -- orange
  {255, 255, 255, 0}    -- yellow
}

-- Ocean palette (cool colors)
local ocean_palette = {
  {0, 0, 0, 64},        -- deep blue
  {64, 0, 64, 128},     -- blue
  {128, 0, 128, 255},   -- cyan
  {192, 0, 255, 200},   -- light cyan
  {255, 128, 255, 255}  -- white-cyan
}

-- Sunset palette
local sunset_palette = {
  {0, 255, 0, 128},     -- purple
  {64, 255, 0, 0},      -- red
  {128, 255, 128, 0},   -- orange
  {192, 255, 200, 100}, -- peach
  {255, 255, 255, 200}  -- light yellow
}

-- Select palette based on style
local palettes = {rainbow_palette, fire_palette, ocean_palette, sunset_palette}
local current_palette = rainbow_palette

-- Timing
local last_update = 0
local update_interval = 30  -- ms

function setup()
  led.clear()
  led.show()
end

function loop()
  local current_time = time.millis()
  local style = sys.page_index() % 4

  -- Select palette
  current_palette = palettes[style + 1]

  -- Read gravity to determine scroll direction
  local gx, gy, gz, valid = gravity.get()
  if valid then
    if math.abs(gx) > math.abs(gy) then
      if gx > 0.5 then
        direction = 0  -- horizontal right
      elseif gx < -0.5 then
        direction = 1  -- horizontal left
      end
    else
      if gy > 0.5 then
        direction = 2  -- vertical down
      elseif gy < -0.5 then
        direction = 3  -- vertical up
      end
    end
  end

  -- Update phase
  if current_time - last_update >= update_interval then
    last_update = current_time
    phase = (phase + 2) % 256
  end

  led.clear()

  -- Render color wave
  if direction == 0 then
    -- Horizontal scroll (left to right)
    for x = 0, WIDTH - 1 do
      local color_index = (phase + x * 256 / WIDTH) % 256
      local r, g, b = led.palette(current_palette, color_index, 255)
      for y = 0, HEIGHT - 1 do
        led.set(x, y, r, g, b)
      end
    end
  elseif direction == 1 then
    -- Horizontal scroll (right to left)
    for x = 0, WIDTH - 1 do
      local color_index = (phase - x * 256 / WIDTH) % 256
      local r, g, b = led.palette(current_palette, color_index, 255)
      for y = 0, HEIGHT - 1 do
        led.set(x, y, r, g, b)
      end
    end
  elseif direction == 2 then
    -- Vertical scroll (top to bottom)
    for y = 0, HEIGHT - 1 do
      local color_index = (phase + y * 256 / HEIGHT) % 256
      local r, g, b = led.palette(current_palette, color_index, 255)
      for x = 0, WIDTH - 1 do
        led.set(x, y, r, g, b)
      end
    end
  elseif direction == 3 then
    -- Vertical scroll (bottom to top)
    for y = 0, HEIGHT - 1 do
      local color_index = (phase - y * 256 / HEIGHT) % 256
      local r, g, b = led.palette(current_palette, color_index, 255)
      for x = 0, WIDTH - 1 do
        led.set(x, y, r, g, b)
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

int setup_colorwave_lua_module(void) {
  // Initialize gravity sensor
  gravity_init();
  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("[colorwave_lua] Gravity sensor start failed");
  }

  // Set brightness
  brightness_max = 30;
  FastLED.setBrightness(30);

  // Load saved config
  if (g_host.saved_subpage < 0) {
    g_host.saved_subpage = load_config(kDefaultConfigKey);
  }
  subpage_index = g_host.saved_subpage;

  // Initialize Lua
  Serial.println("[colorwave_lua] Initializing Lua...");
  g_host.L = luaL_newstate();
  if (!g_host.L) {
    Serial.println("[colorwave_lua] ERROR: Failed to create Lua state");
    return -1;
  }

  // Open standard libraries
  luaL_openlibs(g_host.L);

  // Register hardware APIs
  register_lua_hardware_apis(g_host.L);

  // Register sys.page_index() function
  lua_newtable(g_host.L);
  lua_pushcfunction(g_host.L, [](lua_State* L) -> int {
    lua_pushnumber(L, subpage_index);
    return 1;
  });
  lua_setfield(g_host.L, -2, "page_index");
  lua_setglobal(g_host.L, "sys");

  // Load and execute script
  Serial.println("[colorwave_lua] Loading Lua script...");
  if (luaL_dostring(g_host.L, kDefaultLuaScript) != LUA_OK) {
    const char* error = lua_tostring(g_host.L, -1);
    Serial.printf("[colorwave_lua] ERROR: %s\n", error);
    lua_close(g_host.L);
    g_host.L = nullptr;
    return -2;
  }

  g_host.script_loaded = true;
  Serial.println("[colorwave_lua] Lua script loaded successfully");

  // Call setup function
  lua_getglobal(g_host.L, "setup");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[colorwave_lua] ERROR in setup: %s\n", error);
      lua_pop(g_host.L, 1);
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

int unload_colorwave_lua_module(void) {
  g_host.saved_subpage = subpage_index;
  save_config(kDefaultConfigKey, subpage_index % 4);

  // Call unload function
  if (g_host.L && g_host.script_loaded) {
    lua_getglobal(g_host.L, "unload");
    if (lua_isfunction(g_host.L, -1)) {
      if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(g_host.L, -1);
        Serial.printf("[colorwave_lua] ERROR in unload: %s\n", error);
        lua_pop(g_host.L, 1);
      }
    } else {
      lua_pop(g_host.L, 1);
    }
  }

  // Close Lua state
  if (g_host.L) {
    lua_close(g_host.L);
    g_host.L = nullptr;
  }
  g_host.script_loaded = false;

  // Stop gravity sensor
  gravity_sensor_sleep();

  Serial.println("[colorwave_lua] Module unloaded");
  return 0;
}

int loop_colorwave_lua_module(void) {
  if (!g_host.L || !g_host.script_loaded) {
    return -1;
  }

  // Update gravity snapshot
  lua_hardware_update_gravity();

  // Call loop function
  lua_getglobal(g_host.L, "loop");
  if (lua_isfunction(g_host.L, -1)) {
    if (lua_pcall(g_host.L, 0, 0, 0) != LUA_OK) {
      const char* error = lua_tostring(g_host.L, -1);
      Serial.printf("[colorwave_lua] ERROR in loop: %s\n", error);
      lua_pop(g_host.L, 1);
      return -2;
    }
  } else {
    lua_pop(g_host.L, 1);
  }

  return 0;
}

String colorwave_lua_module_runtime_status_json(void) {
  return "{}";
}

String colorwave_lua_module_configs_json(void) {
  return "[]";
}

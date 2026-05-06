#include "rhythm_lua.h"

#include <Arduino.h>
#include <FastLED.h>
#include <arduinoFFT.h>
#include "app_control.h"
#include "common.h"
#include "driver/i2s_pdm.h"
#include "gravity.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#define I2S_WS_PIN 10
#define I2S_BCK_PIN 7
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 512
#define SAMPLE_FREQ 16000

namespace {

const char* kDefaultConfigKey = "style";

struct RhythmLuaHost {
  lua_State* L = nullptr;
  i2s_chan_handle_t rx_handle = nullptr;
  TaskHandle_t fft_task = nullptr;
  int16_t* dma_buffer = nullptr;
  volatile bool exit_requested = false;
  int saved_subpage = -1;
  bool script_loaded = false;

  // Sensor data accessible from Lua
  float spectrum[MATRIX_WIDTH];
  float gravity_x, gravity_y, gravity_z;
  bool gravity_valid;
};

RhythmLuaHost g_host;

double g_real[DMA_BUF_LEN];
double g_imag[DMA_BUF_LEN];
double g_fft_mag[DMA_BUF_LEN];
ArduinoFFT<double> g_fft(g_real, g_imag, DMA_BUF_LEN, SAMPLE_FREQ);

double fft_add(int from, int to) {
  double result = 0.0;
  for (int i = from; i <= to; i++) {
    result += g_fft_mag[i];
  }
  return result;
}

void publish_spectrum(const double* fft_data) {
  static const float boost[MATRIX_WIDTH] = {
    0.4f, 0.5f, 0.5f, 0.5f, 0.6f, 0.8f, 1.1f, 1.1f, 1.5f,
    1.7f, 3.0f, 3.4f, 3.6f, 3.6f, 3.8f, 3.8f, 1.0f
  };

  for (int i = 0; i < MATRIX_WIDTH; i++) {
    float v = (float)fft_data[i] * boost[i] * 12.0f / 50.0f;
    g_host.spectrum[i] = constrain((int)v, 0, 255);
  }
}

void fft_task_entry(void*) {
  while (!g_host.exit_requested) {
    size_t bytes_read = 0;
    if (i2s_channel_read(g_host.rx_handle, g_host.dma_buffer, DMA_BUF_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
      continue;
    }

    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_real[i] = g_host.dma_buffer[i];
      g_imag[i] = 0.0;
    }

    g_fft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    g_fft.compute(FFT_FORWARD);
    g_fft.complexToMagnitude();
    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_fft_mag[i] = abs(g_real[i]);
    }

    double fft_data[MATRIX_WIDTH];
    fft_data[0]  = fft_add(6, 7) / 2;
    fft_data[1]  = fft_add(8, 10) / 3;
    fft_data[2]  = fft_add(11, 15) / 5;
    fft_data[3]  = fft_add(16, 20) / 5;
    fft_data[4]  = fft_add(21, 25) / 5;
    fft_data[5]  = fft_add(26, 31) / 6;
    fft_data[6]  = fft_add(32, 37) / 6;
    fft_data[7]  = fft_add(38, 43) / 6;
    fft_data[8]  = fft_add(44, 49) / 6;
    fft_data[9]  = fft_add(50, 55) / 6;
    fft_data[10] = fft_add(56, 61) / 6;
    fft_data[11] = fft_add(62, 67) / 6;
    fft_data[12] = fft_add(68, 73) / 6;
    fft_data[13] = fft_add(74, 79) / 6;
    fft_data[14] = fft_add(80, 85) / 6;
    fft_data[15] = fft_add(86, 91) / 6;

    double high = fft_add(92, 97);
    high = max(high, fft_add(98, 103));
    high = max(high, fft_add(104, 109));
    high = max(high, fft_add(110, 115));
    high = max(high, fft_add(116, 121));
    high = max(high, fft_add(122, 127));
    high = max(high, fft_add(128, 133));
    fft_data[16] = high / 6;

    publish_spectrum(fft_data);
  }

  g_host.fft_task = nullptr;
  vTaskDelete(nullptr);
}

int start_audio_capture() {
  g_host.exit_requested = false;
  if (!g_host.dma_buffer) {
    g_host.dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
    if (!g_host.dma_buffer) return -1;
  }

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.id = I2S_NUM_0;
  chan_cfg.dma_desc_num = DMA_BUF_COUNT;
  chan_cfg.dma_frame_num = DMA_BUF_LEN;
  if (i2s_new_channel(&chan_cfg, nullptr, &g_host.rx_handle) != ESP_OK) return -2;

  i2s_pdm_rx_config_t pdm_cfg = {
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),
    .slot_cfg = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_mode = I2S_SLOT_MODE_MONO,
      .slot_mask = I2S_PDM_SLOT_LEFT,
    },
    .gpio_cfg = {
      .clk = (gpio_num_t)I2S_BCK_PIN,
      .din = (gpio_num_t)I2S_WS_PIN,
    },
  };
  if (i2s_channel_init_pdm_rx_mode(g_host.rx_handle, &pdm_cfg) != ESP_OK) return -3;
  if (i2s_channel_enable(g_host.rx_handle) != ESP_OK) return -4;

  xTaskCreatePinnedToCore(fft_task_entry, "RhythmFFT", 10000, nullptr, 1, &g_host.fft_task, 0);
  return 0;
}

void stop_audio_capture() {
  g_host.exit_requested = true;
  delay(50);

  if (g_host.rx_handle) {
    i2s_channel_disable(g_host.rx_handle);
    i2s_del_channel(g_host.rx_handle);
    g_host.rx_handle = nullptr;
  }

  if (g_host.dma_buffer) {
    free(g_host.dma_buffer);
    g_host.dma_buffer = nullptr;
  }
}

void update_gravity_snapshot() {
  gravity_xy_t g = gravity_get();
  g_host.gravity_valid = g.valid;
  g_host.gravity_x = g.gx;
  g_host.gravity_y = g.gy;
  g_host.gravity_z = g.gz;
}

// Lua C API bindings

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

// spectrum.get(index) -> returns float value
static int lua_spectrum_get(lua_State* L) {
  int index = (int)luaL_checknumber(L, 1);
  if (index >= 0 && index < MATRIX_WIDTH) {
    lua_pushnumber(L, g_host.spectrum[index]);
  } else {
    lua_pushnumber(L, 0.0);
  }
  return 1;
}

// spectrum.count() -> returns WIDTH
static int lua_spectrum_count(lua_State* L) {
  lua_pushnumber(L, MATRIX_WIDTH);
  return 1;
}

// gravity.get() -> returns x, y, z, valid
static int lua_gravity_get(lua_State* L) {
  lua_pushnumber(L, g_host.gravity_x);
  lua_pushnumber(L, g_host.gravity_y);
  lua_pushnumber(L, g_host.gravity_z);
  lua_pushboolean(L, g_host.gravity_valid);
  return 4;
}

// config.get(key) -> returns int value
static int lua_config_get(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  int value = load_config(key);
  lua_pushnumber(L, value);
  return 1;
}

// time.millis() -> returns milliseconds since boot
static int lua_time_millis(lua_State* L) {
  lua_pushnumber(L, millis());
  return 1;
}

// math.clamp(value, min, max)
static int lua_math_clamp(lua_State* L) {
  lua_Number value = luaL_checknumber(L, 1);
  lua_Number min_val = luaL_checknumber(L, 2);
  lua_Number max_val = luaL_checknumber(L, 3);
  lua_pushnumber(L, constrain(value, min_val, max_val));
  return 1;
}

// Register LED API
static const luaL_Reg led_lib[] = {
  {"clear", lua_led_clear},
  {"show", lua_led_show},
  {"set", lua_led_set},
  {"hsv", lua_led_hsv},
  {NULL, NULL}
};

// Register spectrum API
static const luaL_Reg spectrum_lib[] = {
  {"get", lua_spectrum_get},
  {"count", lua_spectrum_count},
  {NULL, NULL}
};

// Register gravity API
static const luaL_Reg gravity_lib[] = {
  {"get", lua_gravity_get},
  {NULL, NULL}
};

// Register config API
static const luaL_Reg config_lib[] = {
  {"get", lua_config_get},
  {NULL, NULL}
};

// Register time API
static const luaL_Reg time_lib[] = {
  {"millis", lua_time_millis},
  {NULL, NULL}
};

void register_lua_apis(lua_State* L) {
  // Register led library
  luaL_newlib(L, led_lib);
  lua_setglobal(L, "led");

  // Register spectrum library
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

-- Frequency boost array (from rhythm.h line 35-39)
local fft_freq_boost = {0.4, 0.5, 0.5, 0.5, 0.6, 0.8, 1.1, 1.1, 1.5, 1.7, 3.0, 3.4, 3.6, 3.6, 3.8, 3.8, 1.0}
local gain = 12

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
  gravity_init();

  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("[rhythm_lua] MPU start failed");
  }

  brightness_max = 10;
  FastLED.setBrightness(10);

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

  // Open standard libraries (only base, table, string, math)
  luaL_openlibs(g_host.L);

  // Register custom APIs
  register_lua_apis(g_host.L);

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

  return start_audio_capture();
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
  stop_audio_capture();
  return 0;
}

int loop_rhythm_lua_module(void) {
  if (!g_host.L || !g_host.script_loaded) {
    return 0;
  }

  update_gravity_snapshot();

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

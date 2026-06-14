-- @name: 音乐律动
-- @version: 1.0.0
-- @author: Bottle Team
-- @description: 根据环境音动态显示频谱效果，支持多种视觉风格，支持频段增益调节，支持重力方向切换。
-- @id: rhythm-long

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

-- FFT frequency band grouping (expanded for 32-wide LED matrix)
-- Groups 512 FFT bins into 32 bands with better high-frequency resolution
local fft_band_ranges = {
  {6, 7},      -- Band 0: bins 6-7
  {8, 9},      -- Band 1: bins 8-9
  {10, 11},    -- Band 2: bins 10-11
  {12, 13},    -- Band 3: bins 12-13
  {14, 16},    -- Band 4: bins 14-16
  {17, 19},    -- Band 5: bins 17-19
  {20, 22},    -- Band 6: bins 20-22
  {23, 25},    -- Band 7: bins 23-25
  {26, 28},    -- Band 8: bins 26-28
  {29, 32},    -- Band 9: bins 29-32
  {33, 36},    -- Band 10: bins 33-36
  {37, 40},    -- Band 11: bins 37-40
  {41, 44},    -- Band 12: bins 41-44
  {45, 48},    -- Band 13: bins 45-48
  {49, 52},    -- Band 14: bins 49-52
  {53, 56},    -- Band 15: bins 53-56
  {57, 60},    -- Band 16: bins 57-60
  {61, 64},    -- Band 17: bins 61-64
  {65, 68},    -- Band 18: bins 65-68
  {69, 72},    -- Band 19: bins 69-72
  {73, 76},    -- Band 20: bins 73-76
  {77, 80},    -- Band 21: bins 77-80
  {81, 85},    -- Band 22: bins 81-85
  {86, 90},    -- Band 23: bins 86-90
  {91, 95},    -- Band 24: bins 91-95
  {96, 100},   -- Band 25: bins 96-100
  {101, 105},  -- Band 26: bins 101-105
  {106, 110},  -- Band 27: bins 106-110
  {111, 116},  -- Band 28: bins 111-116
  {117, 122},  -- Band 29: bins 117-122
  {123, 128},  -- Band 30: bins 123-128
  {129, 135}   -- Band 31: bins 129-135
}

-- Load sensitivity from config
local sensitivity = (CONFIG and CONFIG.sensitivity) or 50  -- default

-- Boost multipliers for each band (32 bands for 32-wide matrix)
-- Load from config, or use defaults
local fft_band_boost = {}
for i = 0, 31 do
  local band_value = (CONFIG and CONFIG["band_" .. i]) or 0
  if band_value > 0 then
    print(i, band_value)
    fft_band_boost[i + 1] = band_value / 10.0  -- Convert from 0-100 to 0.0-10.0
  else
    -- Default values with progressive boost for higher frequencies
    local defaults = {
      0.4, 0.5, 0.5, 0.5, 0.6, 0.8, 0.9, 1.0,  -- 0-7: low frequencies
      1.1, 1.2, 1.3, 1.5, 1.7, 1.9, 2.1, 2.3,  -- 8-15: mid frequencies
      2.5, 2.7, 2.9, 3.1, 3.3, 3.5, 3.7, 3.9,  -- 16-23: mid-high frequencies
      4.1, 4.3, 4.5, 4.7, 4.9, 5.1, 5.3, 5.5   -- 24-31: high frequencies
    }
    fft_band_boost[i + 1] = defaults[i + 1]
  end
end

-- Process raw FFT data into frequency bands
function process_fft_bands()
  local bands = {}

  -- Process all 32 bands (average bins in range)
  for band_idx = 1, 32 do
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

  -- Process FFT data
  local fft_bands = process_fft_bands()  -- Get 32 processed bands

  if direction % 2 == 1 then
    -- Vertical orientation: merge adjacent bands for 8-pixel height
    for i = 0, HEIGHT - 1 do
      local fft_value
      if i * 4 + 3 < 32 then
        -- Average 4 bands into 1 (32 bands -> 8 bands)
        fft_value = (fft_bands[i * 4] + fft_bands[i * 4 + 1] + fft_bands[i * 4 + 2] + fft_bands[i * 4 + 3]) / 4
      else
        fft_value = fft_bands[i * 4] or 0
      end
      fft_value = ((prev_fft_value[i] * 3) + fft_value) / 4
      bar_height[i] = math.floor(fft_value / (255 // (WIDTH - 1)))
      if bar_height[i] > peak_height[i] then
        peak_height[i] = math.min(WIDTH - 1, bar_height[i])
      end
      prev_fft_value[i] = fft_value
    end
  else
    -- Horizontal orientation: use all 32 bands for 32-pixel width
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

  -- Render bars and peaks
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

  -- Peak decay every 100ms
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

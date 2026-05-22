#include "water_sim.h"
#include "flip.h"
#include "gravity.h"
#include "common.h"
#include "rgb.h"
#include <math.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <battery.h>
#include "ble_config.h"

#define W MATRIX_HEIGHT
#define H MATRIX_WIDTH
#define LED_COUNT NUM_LEDS

#define SIM_FPS 30

#define LED_VAL_MAX_I PANEL_LED_VALUE_MAX
#define LED_VAL_MAX_F ((float)PANEL_LED_VALUE_MAX)
#define LED_LEVELS (LED_VAL_MAX_I + 1)

// 简单的潮汐模型：基于当前小时计算潮汐因子
// 返回值：0.0 = 最低潮, 1.0 = 最高潮
// peak_hour: 涨潮峰值时间（0-23小时）
static float calculate_tide_level(int peak_hour) {
    // 获取当前时间（小时）
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // 如果无法获取时间，返回中等潮位
        return 0.4f;
    }

    // 潮汐周期约12小时（一天两次高潮两次低潮）
    // 使用正弦波模拟，峰值在 peak_hour 时刻
    float hour = timeinfo.tm_hour + timeinfo.tm_min / 60.0f;
    // 计算相对于峰值时间的偏移
    float offset = hour - (float)peak_hour;
    // 使用余弦波，使得在 peak_hour 时达到最高潮
    float tide = 0.25f + 0.5f * cosf(2.0f * M_PI * offset / 12.0f);

    return tide;
}

// 根据潮汐因子计算颜色
// tide: 0.0 = 最低潮（深蓝）, 1.0 = 最高潮（青绿）
static CRGB get_tide_color(float tide) {
    // 低潮：深蓝色 (Hue ≈ 160)
    // 高潮：青绿色 (Hue ≈ 128)
    uint8_t hue = (uint8_t)(160 - tide * 32);
    return CHSV(hue, 255, 200);
}

int mem_subindex2=-1;

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;


static inline float grid_get(const float* grid, int x, int y) {
    return grid[x * H + y];
}

static inline uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline float clampf_local(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static CRGB mix_rgb(const CRGB& a, const CRGB& b, uint8_t amount) {
    CRGB out;
    out.r = (uint8_t)(((uint16_t)a.r * (255 - amount) + (uint16_t)b.r * amount) / 255);
    out.g = (uint8_t)(((uint16_t)a.g * (255 - amount) + (uint16_t)b.g * amount) / 255);
    out.b = (uint8_t)(((uint16_t)a.b * (255 - amount) + (uint16_t)b.b * amount) / 255);
    return out;
}

static float surface_factor(const float* grid, int sim_x, int sim_y, float gx, float gy) {
    float mag = sqrtf(gx * gx + gy * gy);
    if (mag < 0.05f || !isfinite(mag)) {
        gx = 0.0f;
        gy = 1.0f;
        mag = 1.0f;
    }

    gx /= mag;
    gy /= mag;

    int air_dx = (gx > 0.35f) ? -1 : ((gx < -0.35f) ? 1 : 0);
    int air_dy = (gy > 0.35f) ? -1 : ((gy < -0.35f) ? 1 : 0);
    int water_dx = -air_dx;
    int water_dy = -air_dy;

    int air_x = sim_x + air_dx;
    int air_y = sim_y + air_dy;
    int water_x = sim_x + water_dx;
    int water_y = sim_y + water_dy;

    float here = grid_get(grid, sim_x, sim_y) / LED_VAL_MAX_F;
    float air = 0.0f;
    float water = here;

    if (air_x >= 0 && air_x < W && air_y >= 0 && air_y < H) {
        air = grid_get(grid, air_x, air_y) / LED_VAL_MAX_F;
    }
    if (water_x >= 0 && water_x < W && water_y >= 0 && water_y < H) {
        water = grid_get(grid, water_x, water_y) / LED_VAL_MAX_F;
    }

    float edge = here - air;
    float backing = water - air;
    float surface = edge * 1.9f + backing * 0.45f;
    surface *= clampf_local((0.72f - here) / 0.50f, 0.0f, 1.0f);
    return clampf_local(surface, 0.0f, 1.0f);
}

static CRGB ocean_color(float amount, int sim_x, int sim_y, uint32_t now_ms, const CRGB& base_color, float surface) {
    float fill = amount / LED_VAL_MAX_F;
    if (fill <= 0.02f) {
        return CRGB::Black;
    }
    if (fill > 1.0f) {
        fill = 1.0f;
    }

    float surface_f = clampf_local(surface, 0.0f, 1.0f);
    uint8_t shimmer = sin8((uint8_t)(now_ms / 18 + sim_x * 29 + sim_y * 11));

    // 基于填充度调整基础颜色的亮度
    float brightness_factor = 0.46f + fill * 0.34f; // 0.46 到 0.80
    CRGB c;
    c.r = clamp_u8((int)(base_color.r * brightness_factor));
    c.g = clamp_u8((int)(base_color.g * brightness_factor));
    c.b = clamp_u8((int)(base_color.b * brightness_factor));

    // 表面高光颜色：基础颜色降低饱和度并提高亮度
    CRGB surface_color;
    surface_color.r = clamp_u8((int)(base_color.r * 0.5f + 128 + shimmer / 12));
    surface_color.g = clamp_u8((int)(base_color.g * 0.5f + 128 + shimmer / 12));
    surface_color.b = clamp_u8((int)(base_color.b * 0.5f + 128 + shimmer / 12));

    // 闪光颜色：接近白色
    CRGB sparkle_color;
    sparkle_color.r = clamp_u8((int)(base_color.r * 0.2f + 184));
    sparkle_color.g = clamp_u8((int)(base_color.g * 0.2f + 184));
    sparkle_color.b = clamp_u8((int)(base_color.b * 0.2f + 184));

    if (surface > 0.0f) {
        float shimmer_level = 0.68f + ((float)shimmer / 255.0f) * 0.32f;
        uint8_t highlight = (uint8_t)lrintf(surface_f * shimmer_level * 105.0f);
        c = mix_rgb(c, surface_color, highlight);
    }

    if (surface > 0.45f && shimmer > 205) {
        uint8_t sparkle = (uint8_t)lrintf(surface_f * ((float)shimmer - 205.0f) * 0.26f);
        c = mix_rgb(c, sparkle_color, clamp_u8(sparkle));
    }

    return c;
}

static void sim_task(void* arg) {
    (void)arg;

    FlipFluid* f = NULL;
    // 模拟网格与面板分辨率一致：W × H
    float grid[LED_COUNT];

    // rgb_init();
    brightness_max=10;
    FastLED.setBrightness(10);

    if (mem_subindex2<0)
        mem_subindex2=load_config_ns("water_sim", "sim_index");
    subpage_index=mem_subindex2;

    // Load custom color settings
    int custom_color_enabled = load_config_ns("water", "color_custom");
    String custom_color_hex = load_config_string_ns("water", "color_hue");
    Serial.println(custom_color_hex);

    // Parse custom RGB color
    CRGB custom_color(0, 160, 200); // default color (cyan-ish)
    if (custom_color_hex.length() > 0 && custom_color_hex[0] == '#') {
        // Parse hex color #RRGGBB
        long rgb = strtol(custom_color_hex.c_str() + 1, NULL, 16);
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;
        custom_color = CRGB(r, g, b);
        Serial.printf("Custom color RGB: (%d, %d, %d)\n", r, g, b);
    }

    // Load water level settings
    int dynamic_tide_enabled = load_config_ns("water", "dynamic_tide");
    int fixed_level_percent = load_config_ns("water", "fixed_level");
    if (fixed_level_percent < 1) fixed_level_percent = 40; // default 70%
    if (fixed_level_percent > 100) fixed_level_percent = 100;
    int tide_peak_hour = load_config_ns("water", "tide_peak");
    if (tide_peak_hour < 0) tide_peak_hour = 12; // default 12:00
    if (tide_peak_hour > 23) tide_peak_hour = 12;
    Serial.printf("Water level config: dynamic=%d, fixed=%d%%, tide_peak=%d:00\n",
                  dynamic_tide_enabled, fixed_level_percent, tide_peak_hour);

    const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / SIM_FPS);
    const float dt = 1.0f / (float)SIM_FPS;
    TickType_t last_wake = xTaskGetTickCount();

    // 让内部网格就是 W×H（无拉伸、无缩放），并保持方格 spacing
    const float sim_w = 1.0f;
    const float sim_h = sim_w * ((float)(H + 1) / (float)(W + 1));

    // 根据配置决定初始水位高度
    float initial_fill_ratio;
    if (dynamic_tide_enabled) {
        // 动态模式：创建满水位，后续通过潮汐因子调整到 30%-90%
        initial_fill_ratio = 1.0f;
    } else {
        // 固定模式：用户百分比直接对应初始水位高度
        initial_fill_ratio = (float)fixed_level_percent / 100.0f;
    }

    f = flip_create(sim_w, sim_h, W, H, initial_fill_ratio);
    if (f) {
        flip_set_gravity_scale(f, 9.81f);
        flip_set_solver_quality(f, 1, 10, 0.9f);

        while (s_running) {
            vTaskDelayUntil(&last_wake, frame_ticks);

            // 长按显示图标时暂停渲染
            extern uint8_t touch_hold_hint;
            if (touch_hold_hint > 0 || s_ble_enabled || is_low_bat) {
                continue;
            }

            gravity_xy_t g = gravity_get();
            float gx = g.valid ? g.gx : 0.0f;
            float gy = g.valid ? g.gy : 0.0f;

            // 根据配置决定使用动态潮汐还是固定水位
            float current_tide = 0.5f;
            if (dynamic_tide_enabled) {
                // 动态潮汐：根据当前时间和峰值时间计算潮汐因子
                current_tide = calculate_tide_level(tide_peak_hour);
                // 潮汐水位范围：25%-80% (低潮到高潮)
                flip_set_tide_level(f, current_tide, 0.25f, 0.8f);
            }
            // 固定水位模式：初始创建时已设置正确水位，无需每帧调整

            // Use custom color if enabled, otherwise use tide/palette color
            CRGB base_color;
            if (custom_color_enabled) {
                base_color = custom_color;
            } else if (dynamic_tide_enabled) {
                // 动态潮汐模式：颜色随潮汐变化
                base_color = get_tide_color(current_tide);
            } else {
                // 固定水位模式：使用调色板颜色
                uint8_t hue = (uint8_t)((subpage_index * 20) % 256);
                base_color = CHSV(hue, 255, 200);
            }

            flip_step(f, dt, gx, gy);
            flip_get_led_grid(f, grid, W, H);

            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    float v = grid_get(grid, x, y);
                    if (!isfinite(v)) v = 0.0f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > LED_VAL_MAX_F) v = LED_VAL_MAX_F;

                    float surface = surface_factor(grid, x, y, gx, gy);
                    CRGB c = ocean_color(v, x, y, millis(), base_color, surface);

                    rgb_set(y, x, c.r, c.g, c.b);

                }
            }
            rgb_show();
    
        }
    }

    if (f) {
        flip_destroy(f);
    }
    rgb_clear();
    rgb_show();

    s_running = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

int water_sim_start(int core_id, uint32_t stack_size, int priority) {
    if (s_task) {
        return -1;
    }

    s_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(sim_task, "water_sim", stack_size, NULL, priority, &s_task, core_id);
    if (ok != pdPASS) {
        s_running = false;
        s_task = NULL;
        return -1;
    }
    return 0;
}

int water_sim_stop(uint32_t timeout_ms) {
    mem_subindex2=subpage_index;
    save_config_ns("water_sim", "sim_index", subpage_index);

    if (!s_task) {
        return 0;
    }

    s_running = false;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (s_task) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            TaskHandle_t task = s_task;
            if (task) {
                vTaskDelete(task);
            }
            s_task = NULL;
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}

bool water_sim_is_running(void) {
    return s_task != NULL && s_running;
}

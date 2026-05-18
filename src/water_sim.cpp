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



// ------------------ 潮汐控制相关常量（你可以修改这两个值）------------------
// 最高潮时对应的水量倍率（相对于创建 FlipFluid 时的“基础水量”）：
// 1.0 表示“基础水量”，>1.0 表示比现在更多的水（上限受内部 max_particles 限制）
#define TIDE_MAX_FILL_RATIO 1.4f

// 最低潮时对应的水量倍率：
// 0.3f 表示最低潮时只保留约 30% 的基础水量，你可以根据喜好改成 0.1f~0.9f 等。
#define TIDE_MIN_FILL_RATIO 0.5f

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

static CRGB ocean_color(float amount, int sim_x, int sim_y, uint32_t now_ms, uint8_t hue, float surface) {
    float fill = amount / LED_VAL_MAX_F;
    if (fill <= 0.02f) {
        return CRGB::Black;
    }
    if (fill > 1.0f) {
        fill = 1.0f;
    }

    float surface_f = clampf_local(surface, 0.0f, 1.0f);
    uint8_t shimmer = sin8((uint8_t)(now_ms / 18 + sim_x * 29 + sim_y * 11));
    uint8_t val = clamp_u8(118 + (int)(fill * 88.0f));

    CRGB c = CHSV(hue, 255, val);
    CRGB surface_color = CHSV((uint8_t)(hue - 8), 96, clamp_u8(190 + shimmer / 12));
    CRGB sparkle_color = CHSV((uint8_t)(hue - 6), 36, 230);

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

    // Convert hex color to HSV hue (0-255)
    uint8_t custom_color_hue = 160; // default hue
    if (custom_color_hex.length() > 0 && custom_color_hex[0] == '#') {
        // Parse hex color #RRGGBB
        long rgb = strtol(custom_color_hex.c_str() + 1, NULL, 16);
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;

        // Convert RGB to HSV hue
        CRGB color(r, g, b);
        CHSV hsv = rgb2hsv_approximate(color);
        custom_color_hue = hsv.hue;
    }

    const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / SIM_FPS);
    const float dt = 1.0f / (float)SIM_FPS;
    TickType_t last_wake = xTaskGetTickCount();

    // 让内部网格就是 W×H（无拉伸、无缩放），并保持方格 spacing
    const float sim_w = 1.0f;
    const float sim_h = sim_w * ((float)(H + 1) / (float)(W + 1));
    f = flip_create(sim_w, sim_h, W, H, 0.6f);
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

            // Use custom color if enabled, otherwise use palette-based color
            uint8_t hue;
            if (custom_color_enabled) {
                hue = (uint8_t)custom_color_hue;
            } else {
                hue = (uint8_t)((subpage_index * 20) % 256);
            }

            // 根据当前时间计算潮汐因子（0.0 = 最低潮, 1.0 = 最高潮），
            // 并在每一帧更新内部粒子数量，实现真实“水量”随潮汐变化。
            // float tide = time_sync_get_tide_level();
            float tide = 0.7;
            flip_set_tide_level(f, tide, TIDE_MIN_FILL_RATIO, TIDE_MAX_FILL_RATIO);

            flip_step(f, dt, gx, gy);
            flip_get_led_grid(f, grid, W, H);

            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    float v = grid_get(grid, x, y);
                    if (!isfinite(v)) v = 0.0f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > LED_VAL_MAX_F) v = LED_VAL_MAX_F;

                    float surface = surface_factor(grid, x, y, gx, gy);
                    CRGB c = ocean_color(v, x, y, millis(), hue, surface);

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

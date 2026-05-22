#include "sandglass.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "gravity.h"
#include "sleep_manager.h"

#define W 8
#define H 17

static uint8_t grid[H][W];
static bool mask[H][W];
static int idle_frames = 0;
static int neck_cooldown = 0;
static int flow_speed = 5; // 1-10, default 5
static CRGB fg_color(200, 140, 40); // Sand color
static CRGB bg_color(30, 20, 0);    // Background color

#define NECK_Y 8

static bool crosses_neck(int x0, int y0, int x1, int y1) {
    (void)x0;
    if ((y0 < NECK_Y && y1 >= NECK_Y) || (y0 > NECK_Y && y1 <= NECK_Y)) {
        return true;
    }
    if (y0 == NECK_Y || y1 == NECK_Y) {
        return true;
    }
    return false;
}

static void init_mask() {
    const int profile[H] = {8, 8, 8, 7, 6, 5, 4, 2, 1, 2, 4, 5, 6, 7, 8, 8, 8};

    for (int y = 0; y < H; y++) {
        int w = profile[y];
        int x_start = (W - w) / 2;
        int x_end = x_start + w;

        for (int x = 0; x < W; x++) {
            mask[y][x] = (x >= x_start && x < x_end);
        }
    }
}

static void get_gravity_vec(float* gx_out, float* gy_out) {
    gravity_xy_t g = gravity_get();
    float gx = g.valid ? g.gx : 0.0f;
    float gy = g.valid ? g.gy : 1.0f;

    if (!isfinite(gx)) gx = 0.0f;
    if (!isfinite(gy)) gy = 1.0f;

    *gx_out = gx;
    *gy_out = gy;
}

static bool sand_can_move(int x, int y) {
    return x >= 0 && x < W && y >= 0 && y < H && mask[y][x] && grid[y][x] == 0;
}

static int vertical_to_neck(int y) {
    if (y < NECK_Y) return 1;
    if (y > NECK_Y) return -1;
    return 0;
}

static void update_sand() {
    bool any_moved = false;
    bool neck_used = false;
    float gx = 0.0f;
    float gy = 1.0f;
    get_gravity_vec(&gx, &gy);

    int sx = (gx > 0.12f) ? 1 : ((gx < -0.12f) ? -1 : 0);
    int sy = (gy > 0.12f) ? 1 : ((gy < -0.12f) ? -1 : 0);
    if (sx == 0 && sy == 0) {
        sy = 1;
    }

    bool vertical_major = fabsf(gy) >= fabsf(gx);
    int major_dx = vertical_major ? 0 : sx;
    int major_dy = vertical_major ? sy : 0;
    int minor_dx = vertical_major ? sx : 0;
    int minor_dy = vertical_major ? 0 : sy;

    if (neck_cooldown > 0) {
        neck_cooldown--;
    }

    int y0 = (sy > 0) ? (H - 1) : ((sy < 0) ? 0 : 0);
    int y1 = (sy > 0) ? -1 : ((sy < 0) ? H : H);
    int ys = (sy > 0) ? -1 : 1;

    int x0 = (sx > 0) ? (W - 1) : ((sx < 0) ? 0 : 0);
    int x1 = (sx > 0) ? -1 : ((sx < 0) ? W : W);
    int xs = (sx > 0) ? -1 : 1;

    for (int y = y0; y != y1; y += ys) {
        for (int x = x0; x != x1; x += xs) {
            if (grid[y][x] == 0) {
                continue;
            }

            bool moved = false;

            if (sx == 0 && sy > 0 && y == NECK_Y - 1) {
                if (neck_cooldown > 0) {
                    continue;
                }

                bool blocked = false;
                for (int i = 1; i <= 2; i++) {
                    if (y + i < H && grid[y + i][x]) {
                        blocked = true;
                        break;
                    }
                }
                if (blocked) {
                    continue;
                }
            }

            int moves[6][2];
            int count = 0;

            if (!vertical_major && sx != 0) {
                int neck_dy = vertical_to_neck(y);
                if (neck_dy != 0) {
                    moves[count][0] = 0;
                    moves[count++][1] = neck_dy;
                    moves[count][0] = sx;
                    moves[count++][1] = neck_dy;
                }
                moves[count][0] = sx;
                moves[count++][1] = 0;

                int drift = random(2) ? 1 : -1;
                moves[count][0] = sx;
                moves[count++][1] = drift;
            } else {
                moves[count][0] = sx;
                moves[count++][1] = sy;
                moves[count][0] = major_dx;
                moves[count++][1] = major_dy;
                if (minor_dx != 0 || minor_dy != 0) {
                    moves[count][0] = major_dx + minor_dx;
                    moves[count++][1] = major_dy + minor_dy;
                    moves[count][0] = minor_dx;
                    moves[count++][1] = minor_dy;
                }
                int side = random(2) ? 1 : -1;
                moves[count][0] = vertical_major ? side : major_dx;
                moves[count++][1] = vertical_major ? major_dy : side;
            }

            for (int i = 0; i < count; i++) {
                int nx = x + moves[i][0];
                int ny = y + moves[i][1];
                if (!sand_can_move(nx, ny)) continue;
                bool through_neck = crosses_neck(x, y, nx, ny);
                if (through_neck && (neck_used || neck_cooldown > 0)) continue;

                grid[y][x] = 0;
                grid[ny][nx] = 1;
                if (through_neck) {
                    neck_used = true;
                    neck_cooldown = 2 + random(3);
                }
                moved = true;
                any_moved = true;
                break;
            }

            if (moved && sx == 0 && sy > 0 && y == NECK_Y - 1 && !neck_used) {
                neck_cooldown = 2 + random(3);
            }
        }
    }

    if (any_moved) {
        idle_frames = 0;
    } else {
        idle_frames++;
    }
}

static void render() {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!mask[y][x]) {
                leds(y, x) = CRGB::Black;
            } else if (grid[y][x]) {
                leds(y, x) = fg_color;
            } else {
                leds(y, x) = bg_color;
            }
        }
    }
}

static void init_sand() {
    memset(grid, 0, sizeof(grid));

    // Fill bottom half with sand (inverted hourglass)
    for (int y = H / 2; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (mask[y][x]) {
                grid[y][x] = 1;
            }
        }
    }
}

static bool sand_finished() {
    return idle_frames > 3;
}

static void restart_sand() {
    init_sand();
    idle_frames = 0;
    neck_cooldown = 0;
}

int sand_loop() {
    update_sand();
    render();
    FastLED.show();

    // Calculate delay based on flow speed (1=slowest, 10=fastest)
    // Speed 1: 410ms, Speed 5: 250ms, Speed 10: 50ms
    int frame_delay = 450 - flow_speed * 40;
    delay(frame_delay);
    return 0;
}

int setup_sand() {
    brightness_max = 10;
    FastLED.setBrightness(10);

    // Load flow speed configuration (1-10, default 5)
    flow_speed = load_config_ns("sandglass", "sand_speed");
    if (flow_speed < 1) flow_speed = 5;
    if (flow_speed > 10) flow_speed = 10;
    Serial.printf("Sandglass flow speed: %d\n", flow_speed);

    // Load color configuration
    String fg_hex = load_config_string_ns("sandglass", "sand_fg");
    if (fg_hex.length() > 0 && fg_hex[0] == '#') {
        long rgb = strtol(fg_hex.c_str() + 1, NULL, 16);
        fg_color = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        Serial.printf("Sandglass foreground color: %s -> RGB(%d,%d,%d)\n",
                      fg_hex.c_str(), fg_color.r, fg_color.g, fg_color.b);
    }

    String bg_hex = load_config_string_ns("sandglass", "sand_bg");
    if (bg_hex.length() > 0 && bg_hex[0] == '#') {
        long rgb = strtol(bg_hex.c_str() + 1, NULL, 16);
        bg_color = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        Serial.printf("Sandglass background color: %s -> RGB(%d,%d,%d)\n",
                      bg_hex.c_str(), bg_color.r, bg_color.g, bg_color.b);
    }

    int err = gravity_sensor_start();
    if (err != 0) {
        Serial.println("mpu start failed");
    }

    FastLED.clear();
    init_mask();
    init_sand();

    return 0;
}

int unload_sand() {
    if (s_idle_timeout_ms==0)
        gravity_sensor_sleep();
    return 0;
}

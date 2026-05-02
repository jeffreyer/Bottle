#include "sandglass.h"
#include <Arduino.h>
#include <stdio.h>
#include "common.h"
#include "esp_partition.h"

#define W 8
#define H 17

uint8_t grid[H][W];   // 沙子
bool mask[H][W];      // 沙漏形状
int idle_frames = 0;

void init_mask() {
    int profile[17] = {
        8,8,7,7,6,5,4,2,1,2,4,5,6,7,7,8,8
    };

    for (int y = 0; y < H; y++) {

        int w = profile[y];             // 当前行宽度
        int x_start = (W - w) / 2;      // 居中
        int x_end   = x_start + w;

        for (int x = 0; x < W; x++) {

            if (x >= x_start && x < x_end)
                mask[y][x] = true;
            else
                mask[y][x] = false;
        }
    }
}

#define NECK_Y 8   // 瓶口位置（根据你的mask来）

int neck_cooldown = 0;

void update_sand() {
    bool any_moved = false;

    // 冷却计数
    if (neck_cooldown > 0) {
        neck_cooldown--;
    }

    // 从下往上扫描（防止连跳）
    for (int y = H - 2; y >= 0; y--) {
        for (int x = 0; x < W; x++) {

            if (grid[y][x] != 1) continue;

            bool moved = false;

            // =========================
            // ⭐ 瓶口限制（核心）
            // =========================
            if (y == NECK_Y - 1) {

                // 冷却中 → 不允许通过
                if (neck_cooldown > 0) continue;

                // 防止下面形成连续柱
                bool blocked = false;
                for (int i = 1; i <= 2; i++) {
                    if (y + i < H && grid[y + i][x]) {
                        blocked = true;
                        break;
                    }
                }
                if (blocked) continue;
            }

            // =========================
            // ⭐ 随机左右优先顺序（避免固定路径）
            // =========================
            int dir = random(2); // 0 or 1

            // =========================
            // ⭐ 掉落距离（1~2格，拉开间距）
            // =========================
            int fall = 1 + random(2);

            // =========================
            // ↓ 优先：正下
            // =========================
            int ny = y;
            while (fall-- && ny < H - 1 && mask[ny + 1][x] && grid[ny + 1][x] == 0) {
                ny++;
            }

            if (ny != y) {
                grid[y][x] = 0;
                grid[ny][x] = 1;
                moved = true;
                any_moved = true;
            }
            else {
                // =========================
                // ↙ ↘ 斜向（带随机）
                // =========================
                for (int i = 0; i < 2; i++) {

                    int dx = (dir == 0) ? -1 : 1;
                    dir = 1 - dir; // 下次反方向

                    int nx = x + dx;
                    if (nx < 0 || nx >= W) continue;

                    if (mask[y+1][nx] && grid[y+1][nx] == 0) {

                        grid[y][x] = 0;
                        grid[y+1][nx] = 1;
                        moved = true;
                        any_moved = true;
                        break;
                    }
                }
            }

            // =========================
            // ⭐ 如果通过瓶口 → 触发冷却
            // =========================
            if (moved && y == NECK_Y - 1) {
                neck_cooldown = 2 + random(3);  // 2~4帧间隔
            }
        }
    }

    if (any_moved) {
        idle_frames = 0;
    } else {
        idle_frames++;
    }
}

void render() {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {

            if (!mask[y][x]) {
                leds(y,x) = CRGB::Black;
            } 
            else if (grid[y][x]) {
                leds(y,x) = CRGB(255, 180, 50); // 沙子色
            } 
            else {
                leds(y,x) = CRGB(30, 20, 0); // 背景微亮
            }
        }
    }
}

void init_sand() {
    memset(grid, 0, sizeof(grid));

    for (int y = 0; y < H/2; y++) {
        for (int x = 0; x < W; x++) {
            if (mask[y][x]) {
                grid[y][x] = 1;
            }
        }
    }
}

bool sand_finished() {
    return idle_frames > 3;
}

void restart_sand() {
    init_sand();

    idle_frames = 0;
    neck_cooldown = 0;
}

int sand_loop()
{
    update_sand();
    render();
    FastLED.show();

    if (sand_finished()) {
        delay(500);      // 停顿一下更像真实沙漏
        restart_sand();  // 重新开始
    }

    delay(200);
    return 0;

}

int setup_sand(){
    brightness_max=10;
    FastLED.setBrightness(10);
    
    FastLED.clear();
    init_mask();
    init_sand();
    
    return 0;

}

int unload_sand(){
    return 0;
}
#include "breakout.h"
#include <Arduino.h>
#include <FastLED.h>
#include <math.h>
#include "common.h"
#include "gravity.h"

namespace {

constexpr uint8_t kCols = MATRIX_WIDTH;
constexpr uint8_t kRows = MATRIX_HEIGHT;
constexpr uint8_t kBrickRows = 3;
constexpr uint8_t kPaddleY = kRows - 1;
constexpr uint8_t kPaddleWidth = 4;
constexpr uint16_t kFrameMs = 70;
constexpr uint16_t kResetPauseMs = 650;
constexpr float kTiltDeadZone = 0.08f;
constexpr float kPaddleAccel = 0.6f;
constexpr float kPaddleDamping = 0.86f;
constexpr float kPaddleMaxSpeed = 0.72f;
constexpr float kBallSpeed = 0.38f;

bool bricks[kBrickRows][kCols];
float paddle_x = (kCols - kPaddleWidth) * 0.5f;
float paddle_vx = 0.0f;
float ball_x = kCols * 0.5f;
float ball_y = kPaddleY - 1.0f;
float ball_vx = 0.26f;
float ball_vy = -kBallSpeed;
uint32_t last_frame_ms = 0;
uint32_t reset_at_ms = 0;
bool paused_for_reset = false;

float clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void reset_game(bool serve_right) {
    for (uint8_t y = 0; y < kBrickRows; y++) {
        for (uint8_t x = 0; x < kCols; x++) {
            bricks[y][x] = true;
        }
    }

    paddle_x = (kCols - kPaddleWidth) * 0.5f;
    paddle_vx = 0.0f;
    ball_x = paddle_x + (kPaddleWidth - 1) * 0.5f;
    ball_y = kPaddleY - 1.0f;
    ball_vx = serve_right ? 0.26f : -0.26f;
    ball_vy = -kBallSpeed;
    last_frame_ms = millis();
    paused_for_reset = false;
}

bool any_bricks_left() {
    for (uint8_t y = 0; y < kBrickRows; y++) {
        for (uint8_t x = 0; x < kCols; x++) {
            if (bricks[y][x]) {
                return true;
            }
        }
    }
    return false;
}

void schedule_reset() {
    paused_for_reset = true;
    reset_at_ms = millis() + kResetPauseMs;
}

void update_paddle() {
    gravity_xy_t g = gravity_get();
    if (!g.valid) {
        return;
    }

    float tilt = g.gy;
    if (fabsf(tilt) < kTiltDeadZone) {
        tilt = 0.0f;
    }

    paddle_vx += tilt * kPaddleAccel;
    paddle_vx *= kPaddleDamping;
    paddle_vx = clampf(paddle_vx, -kPaddleMaxSpeed, kPaddleMaxSpeed);

    paddle_x += paddle_vx;
    paddle_x = clampf(paddle_x, 0.0f, (float)(kCols - kPaddleWidth));
    if (paddle_x <= 0.0f || paddle_x >= (float)(kCols - kPaddleWidth)) {
        paddle_vx = 0.0f;
    }
}

void hit_brick(uint8_t x, uint8_t y) {
    bricks[y][x] = false;
    ball_vy = -ball_vy;

    if (!any_bricks_left()) {
        schedule_reset();
    }
}

void update_ball() {
    float next_x = ball_x + ball_vx;
    float next_y = ball_y + ball_vy;

    if (next_x < 0.0f) {
        next_x = 0.0f;
        ball_vx = fabsf(ball_vx);
    } else if (next_x > (float)(kCols - 1)) {
        next_x = (float)(kCols - 1);
        ball_vx = -fabsf(ball_vx);
    }

    if (next_y < 0.0f) {
        next_y = 0.0f;
        ball_vy = fabsf(ball_vy);
    }

    int bx = clampi((int)roundf(next_x), 0, kCols - 1);
    int by = clampi((int)roundf(next_y), 0, kRows - 1);

    if (by >= 0 && by < kBrickRows && bricks[by][bx]) {
        hit_brick((uint8_t)bx, (uint8_t)by);
        next_y = ball_y + ball_vy;
    }

    if (ball_vy > 0.0f && next_y >= (float)(kPaddleY - 1)) {
        float paddle_left = paddle_x - 0.35f;
        float paddle_right = paddle_x + kPaddleWidth - 0.65f;

        if (next_x >= paddle_left && next_x <= paddle_right) {
            float center = paddle_x + (kPaddleWidth - 1) * 0.5f;
            float offset = (next_x - center) / ((float)kPaddleWidth * 0.5f);
            ball_vx = clampf(offset * 0.48f, -0.50f, 0.50f);
            ball_vy = -kBallSpeed;
            next_y = kPaddleY - 1.0f;
        }
    }

    if (next_y > (float)kPaddleY + 0.25f) {
        schedule_reset();
        return;
    }

    ball_x = clampf(next_x, 0.0f, (float)(kCols - 1));
    ball_y = clampf(next_y, 0.0f, (float)kPaddleY);
}

void draw_pixel_safe(int x, int y, const CRGB& color) {
    if (x < 0 || x >= kCols || y < 0 || y >= kRows) {
        return;
    }
    leds(x, y) = color;
}

void render_game() {
    FastLED.clear();

    for (uint8_t y = 0; y < kBrickRows; y++) {
        CHSV brick_color(18 + y * 32, 220, 80);
        for (uint8_t x = 0; x < kCols; x++) {
            if (bricks[y][x]) {
                leds(x, y) = brick_color;
            }
        }
    }

    uint8_t paddle_left = (uint8_t)roundf(paddle_x);
    for (uint8_t i = 0; i < kPaddleWidth; i++) {
        draw_pixel_safe(paddle_left + i, kPaddleY, CRGB(0, 34, 55));
    }

    draw_pixel_safe((int)roundf(ball_x), (int)roundf(ball_y), CRGB(55, 55, 55));
    FastLED.show();
}

}  // namespace

int setup_breakout() {
    brightness_max = 18;
    FastLED.setBrightness(18);
    FastLED.clear();

    int err = gravity_sensor_start();
    if (err != 0) {
        Serial.println("mpu start failed");
    }

    reset_game(true);
    return 0;
}

int unload_breakout() {
    FastLED.clear();
    FastLED.show();
    return 0;
}

int breakout_loop() {
    uint32_t now = millis();

    if (paused_for_reset) {
        if ((int32_t)(now - reset_at_ms) >= 0) {
            reset_game(ball_vx < 0.0f);
        } else {
            render_game();
            return 0;
        }
    }

    if (now - last_frame_ms < kFrameMs) {
        return 0;
    }
    last_frame_ms = now;

    update_paddle();
    update_ball();
    render_game();
    return 0;
}

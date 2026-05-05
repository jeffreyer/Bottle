#include "rhythm_spectrum_user_module.h"

#include <string.h>

namespace {

struct SpectrumState {
  uint8_t direction;
  uint8_t band_count;
  uint8_t value_count;
  uint8_t color_phase;
  uint8_t bars[MATRIX_WIDTH];
  uint8_t peaks[MATRIX_WIDTH];
  uint8_t smooth[MATRIX_WIDTH];
};

SpectrumState g_state;

module_rgb_t green_peak_color(uint8_t y, uint8_t height) {
  uint8_t t = height <= 1 ? 0 : (uint8_t)(y * 255 / (height - 1));
  if (t < 128) {
    return module_blend({120, 255, 32}, {255, 220, 0}, t * 2);
  }
  return module_blend({255, 220, 0}, {255, 24, 0}, (t - 128) * 2);
}

void map_xy(const SpectrumState& st, uint8_t x, uint8_t y, uint8_t* out_x, uint8_t* out_y) {
  switch (st.direction) {
    case 1:
      *out_x = MATRIX_WIDTH - y - 1;
      *out_y = x;
      break;
    case 2:
      *out_x = MATRIX_WIDTH - x - 1;
      *out_y = MATRIX_HEIGHT - y - 1;
      break;
    case 3:
      *out_x = y;
      *out_y = MATRIX_HEIGHT - x - 1;
      break;
    default:
      *out_x = x;
      *out_y = y;
      break;
  }
}

void draw_oriented(module_context_t* ctx, uint8_t x, uint8_t y, module_rgb_t color) {
  uint8_t px = 0;
  uint8_t py = 0;
  map_xy(g_state, x, y, &px, &py);
  ctx->led.set(px, py, color);
}

void update_orientation(module_context_t* ctx) {
  float gx = ctx->sensor.gravity.valid ? ctx->sensor.gravity.x : 0.0f;
  float gy = ctx->sensor.gravity.valid ? ctx->sensor.gravity.y : 0.0f;

  if (gy > 0.7f) {
    g_state.band_count = MATRIX_HEIGHT;
    g_state.value_count = MATRIX_WIDTH;
    g_state.direction = 1;
  } else if (gy < -0.7f) {
    g_state.band_count = MATRIX_HEIGHT;
    g_state.value_count = MATRIX_WIDTH;
    g_state.direction = 3;
  } else if (gx > 0.7f) {
    g_state.band_count = MATRIX_WIDTH;
    g_state.value_count = MATRIX_HEIGHT;
    g_state.direction = 2;
  } else {
    g_state.band_count = MATRIX_WIDTH;
    g_state.value_count = MATRIX_HEIGHT;
    g_state.direction = 0;
  }
}

void update_bars(module_context_t* ctx) {
  update_orientation(ctx);
  for (uint8_t i = 0; i < g_state.band_count; i++) {
    uint8_t raw = (g_state.direction & 1)
        ? (uint8_t)((ctx->sensor.spectrum[i * 2] + ctx->sensor.spectrum[i * 2 + 1]) / 2)
        : ctx->sensor.spectrum[i];
    raw = (uint8_t)((g_state.smooth[i] * 3 + raw) / 4);
    g_state.smooth[i] = raw;
    g_state.bars[i] = raw / max<uint8_t>(1, 255 / max<uint8_t>(1, g_state.value_count - 1));
    if (g_state.bars[i] > g_state.peaks[i]) {
      g_state.peaks[i] = min<uint8_t>(g_state.value_count - 1, g_state.bars[i]);
    }
  }
}

void draw_spectrum(module_context_t* ctx) {
  uint8_t style = (uint8_t)(ctx->config.style() % 4);
  ctx->led.clear();

  for (uint8_t band = 0; band < g_state.band_count; band++) {
    uint8_t bar = g_state.bars[band];
    for (uint8_t y = 0; y < bar; y++) {
      module_rgb_t color;
      if (style == 0) {
        color = green_peak_color(y, g_state.value_count);
      } else if (style == 1) {
        color = module_hsv(band * 255 / max<uint8_t>(1, g_state.band_count), 255, 180);
      } else if (style == 2) {
        color = module_hsv((band * 255 / max<uint8_t>(1, g_state.band_count)) + 32, 255, 180);
      } else {
        color = module_hsv((y * 255 / max<uint8_t>(1, g_state.value_count)) + g_state.color_phase, 255, 180);
      }

      uint8_t draw_y = (style == 2 && (band & 1)) ? (g_state.value_count - 1 - y) : y;
      draw_oriented(ctx, band, draw_y, color);
    }

    uint8_t peak_y = g_state.peaks[band];
    if (style == 2 && (band & 1)) {
      peak_y = g_state.value_count - 1 - peak_y;
    }
    module_rgb_t peak = style == 3
        ? module_hsv((g_state.peaks[band] * 255 / max<uint8_t>(1, g_state.value_count)) + 160, 180, 180)
        : module_rgb_t{180, 180, 180};
    draw_oriented(ctx, band, peak_y, peak);
  }

  if ((ctx->now_ms % 90) < 20) {
    g_state.color_phase++;
  }
  if ((ctx->now_ms % 100) < 20) {
    for (uint8_t i = 0; i < g_state.band_count; i++) {
      if (g_state.peaks[i] > 0) g_state.peaks[i]--;
    }
  }

  ctx->led.show();
}

}  // namespace

void rhythm_spectrum_setup(module_context_t* ctx) {
  (void)ctx;
  memset(&g_state, 0, sizeof(g_state));
  g_state.band_count = MATRIX_WIDTH;
  g_state.value_count = MATRIX_HEIGHT;
}

void rhythm_spectrum_loop(module_context_t* ctx) {
  update_bars(ctx);
  draw_spectrum(ctx);
}

void rhythm_spectrum_unload(module_context_t* ctx) {
  (void)ctx;
}

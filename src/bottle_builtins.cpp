#include "bottle_types.h"
#include "module_runtime.h"
#include <Arduino.h>
#include <math.h>

// Math functions
bottle_value_t builtin_max(bottle_value_t a, bottle_value_t b) {
  float fa = bottle_to_float(a);
  float fb = bottle_to_float(b);
  return bottle_float(max(fa, fb));
}

bottle_value_t builtin_min(bottle_value_t a, bottle_value_t b) {
  float fa = bottle_to_float(a);
  float fb = bottle_to_float(b);
  return bottle_float(min(fa, fb));
}

bottle_value_t builtin_clamp(bottle_value_t v, bottle_value_t lo, bottle_value_t hi) {
  float fv = bottle_to_float(v);
  float flo = bottle_to_float(lo);
  float fhi = bottle_to_float(hi);
  return bottle_float(constrain(fv, flo, fhi));
}

bottle_value_t builtin_abs(bottle_value_t x) {
  if (x.type == BOTTLE_TYPE_INT) {
    return bottle_int(abs(x.as.i));
  }
  return bottle_float(fabs(bottle_to_float(x)));
}

bottle_value_t builtin_sqrt(bottle_value_t x) {
  float fx = bottle_to_float(x);
  if (fx < 0.0f) fx = 0.0f;
  return bottle_float(sqrtf(fx));
}

bottle_value_t builtin_sin(bottle_value_t x) {
  float fx = bottle_to_float(x);
  return bottle_float(sinf(fx));
}

bottle_value_t builtin_cos(bottle_value_t x) {
  float fx = bottle_to_float(x);
  return bottle_float(cosf(fx));
}

bottle_value_t builtin_random(bottle_value_t min_val, bottle_value_t max_val) {
  int32_t min_i = bottle_to_int(min_val);
  int32_t max_i = bottle_to_int(max_val);
  if (min_i > max_i) {
    int32_t temp = min_i;
    min_i = max_i;
    max_i = temp;
  }
  return bottle_int(random(min_i, max_i + 1));
}

bottle_value_t builtin_millis(uint32_t now_ms) {
  return bottle_int((int32_t)now_ms);
}

// Color functions
bottle_value_t builtin_hsv(bottle_value_t h, bottle_value_t s, bottle_value_t v) {
  uint8_t h_byte = bottle_to_uint8(h);
  uint8_t s_byte = bottle_to_uint8(s);
  uint8_t v_byte = bottle_to_uint8(v);
  module_rgb_t color = module_hsv(h_byte, s_byte, v_byte);
  return bottle_color(color);
}

bottle_value_t builtin_rgb(bottle_value_t r, bottle_value_t g, bottle_value_t b) {
  module_rgb_t color;
  color.r = bottle_to_uint8(r);
  color.g = bottle_to_uint8(g);
  color.b = bottle_to_uint8(b);
  return bottle_color(color);
}

bottle_value_t builtin_blend(bottle_value_t a, bottle_value_t b, bottle_value_t amount) {
  if (a.type != BOTTLE_TYPE_COLOR || b.type != BOTTLE_TYPE_COLOR) {
    return a; // Error: not colors
  }
  uint8_t amt = bottle_to_uint8(amount);
  module_rgb_t result = module_blend(a.as.color, b.as.color, amt);
  return bottle_color(result);
}

// Debug
void builtin_print(bottle_value_t value) {
  switch (value.type) {
    case BOTTLE_TYPE_INT:
      Serial.printf("[Bottle] %d\n", value.as.i);
      break;
    case BOTTLE_TYPE_FLOAT:
      Serial.printf("[Bottle] %.3f\n", value.as.f);
      break;
    case BOTTLE_TYPE_BOOL:
      Serial.printf("[Bottle] %s\n", value.as.b ? "true" : "false");
      break;
    case BOTTLE_TYPE_COLOR:
      Serial.printf("[Bottle] rgb(%d, %d, %d)\n",
                    value.as.color.r, value.as.color.g, value.as.color.b);
      break;
    default:
      Serial.println("[Bottle] <unknown>");
      break;
  }
}

#include "bottle_types.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Mock builtin functions for testing

bottle_value_t builtin_max(bottle_value_t a, bottle_value_t b) {
    float af = (a.type == BOTTLE_TYPE_FLOAT) ? a.as.f : (float)a.as.i;
    float bf = (b.type == BOTTLE_TYPE_FLOAT) ? b.as.f : (float)b.as.i;
    return (af > bf) ? a : b;
}

bottle_value_t builtin_min(bottle_value_t a, bottle_value_t b) {
    float af = (a.type == BOTTLE_TYPE_FLOAT) ? a.as.f : (float)a.as.i;
    float bf = (b.type == BOTTLE_TYPE_FLOAT) ? b.as.f : (float)b.as.i;
    return (af < bf) ? a : b;
}

bottle_value_t builtin_clamp(bottle_value_t v, bottle_value_t lo, bottle_value_t hi) {
    float vf = (v.type == BOTTLE_TYPE_FLOAT) ? v.as.f : (float)v.as.i;
    float lof = (lo.type == BOTTLE_TYPE_FLOAT) ? lo.as.f : (float)lo.as.i;
    float hif = (hi.type == BOTTLE_TYPE_FLOAT) ? hi.as.f : (float)hi.as.i;

    if (vf < lof) return lo;
    if (vf > hif) return hi;
    return v;
}

bottle_value_t builtin_abs(bottle_value_t x) {
    float xf = (x.type == BOTTLE_TYPE_FLOAT) ? x.as.f : (float)x.as.i;
    return bottle_float(fabs(xf));
}

bottle_value_t builtin_sqrt(bottle_value_t x) {
    float xf = (x.type == BOTTLE_TYPE_FLOAT) ? x.as.f : (float)x.as.i;
    return bottle_float(sqrt(xf));
}

bottle_value_t builtin_sin(bottle_value_t x) {
    float xf = (x.type == BOTTLE_TYPE_FLOAT) ? x.as.f : (float)x.as.i;
    return bottle_float(sin(xf));
}

bottle_value_t builtin_cos(bottle_value_t x) {
    float xf = (x.type == BOTTLE_TYPE_FLOAT) ? x.as.f : (float)x.as.i;
    return bottle_float(cos(xf));
}

bottle_value_t builtin_random(bottle_value_t min_val, bottle_value_t max_val) {
    float minf = (min_val.type == BOTTLE_TYPE_FLOAT) ? min_val.as.f : (float)min_val.as.i;
    float maxf = (max_val.type == BOTTLE_TYPE_FLOAT) ? max_val.as.f : (float)max_val.as.i;
    float range = maxf - minf;
    return bottle_float(minf + (rand() / (float)RAND_MAX) * range);
}

bottle_value_t builtin_millis(uint32_t now_ms) {
    return bottle_int((int32_t)now_ms);
}

bottle_value_t builtin_hsv(bottle_value_t h, bottle_value_t s, bottle_value_t v) {
    // Mock HSV to RGB conversion
    module_rgb_t color;
    color.r = (uint8_t)((h.type == BOTTLE_TYPE_FLOAT) ? h.as.f : h.as.i);
    color.g = (uint8_t)((s.type == BOTTLE_TYPE_FLOAT) ? s.as.f : s.as.i);
    color.b = (uint8_t)((v.type == BOTTLE_TYPE_FLOAT) ? v.as.f : v.as.i);
    return bottle_color(color);
}

bottle_value_t builtin_rgb(bottle_value_t r, bottle_value_t g, bottle_value_t b) {
    module_rgb_t color;
    color.r = (uint8_t)((r.type == BOTTLE_TYPE_FLOAT) ? r.as.f : r.as.i);
    color.g = (uint8_t)((g.type == BOTTLE_TYPE_FLOAT) ? g.as.f : g.as.i);
    color.b = (uint8_t)((b.type == BOTTLE_TYPE_FLOAT) ? b.as.f : b.as.i);
    return bottle_color(color);
}

bottle_value_t builtin_blend(bottle_value_t a, bottle_value_t b, bottle_value_t amount) {
    // Simple mock blend
    return a;
}

void builtin_print(bottle_value_t value) {
    if (value.type == BOTTLE_TYPE_FLOAT) {
        printf("PRINT: %f\n", value.as.f);
    } else if (value.type == BOTTLE_TYPE_INT) {
        printf("PRINT: %d\n", value.as.i);
    }
}

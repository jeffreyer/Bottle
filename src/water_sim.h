#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r, g, b;
} rgb8_t;

int water_sim_start(int core_id, uint32_t stack_size, int priority);
int water_sim_stop(uint32_t timeout_ms);
bool water_sim_is_running(void);
static uint8_t s_palette_idx = 0;//可在此选择不同调色板（0~2）
#define PAL_N 6
#define PALETTE_COUNT 3
extern rgb8_t PALETTES[PALETTE_COUNT][PAL_N];

#ifdef __cplusplus
}
#endif


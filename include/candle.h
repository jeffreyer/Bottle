#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

uint8_t candle_flicker(uint8_t u, uint8_t v, uint32_t sequence, uint8_t turbulence, uint8_t size);

int setup_candle();
int unload_candle();
int candle_loop();

#ifdef __cplusplus
}
#endif

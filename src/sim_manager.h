#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIM_MODE_NONE = -1,
    SIM_MODE_WATER = 0,
} sim_mode_t;

typedef struct {
    int core_id;
    uint32_t stack_size;
    int priority;
    uint32_t stop_timeout_ms;
} sim_runtime_config_t;

int sim_manager_init(const sim_runtime_config_t* cfg);
int sim_manager_start(sim_mode_t mode);
int sim_manager_stop(void);
sim_mode_t sim_manager_current(void);
int fluid_loop();
int setup_fluid();
int unload_fluid();

#ifdef __cplusplus
}
#endif


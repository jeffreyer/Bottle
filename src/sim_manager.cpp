#include <Arduino.h>
#include "sim_manager.h"
#include "water_sim.h"
#include "gravity.h"
#include "sleep_manager.h"

static sim_runtime_config_t s_cfg = {
    .core_id = 1,
    .stack_size = 8192,
    .priority = 5,
    .stop_timeout_ms = 1000,
};

static sim_mode_t s_current = SIM_MODE_NONE;
static bool s_inited = false;
static sim_mode_t g_boot_mode = SIM_MODE_WATER;

static int start_mode(sim_mode_t mode) {
    if (mode == SIM_MODE_WATER) {
        return water_sim_start(s_cfg.core_id, s_cfg.stack_size, s_cfg.priority);
    }
    return -1;
}

static int stop_mode(sim_mode_t mode) {
    if (mode == SIM_MODE_WATER) {
        return water_sim_stop(s_cfg.stop_timeout_ms);
    }
    return 0;
}

int sim_manager_init(const sim_runtime_config_t* cfg) {
    if (cfg) {
        s_cfg = *cfg;
    }
    s_current = SIM_MODE_NONE;
    s_inited = true;
    return 0;
}

int sim_manager_start(sim_mode_t mode) {
    if (!s_inited) {
        return -1;
    }
    if (s_current != SIM_MODE_NONE) {
        return -1;
    }

    int err = start_mode(mode);
    if (err == 0) {
        s_current = mode;
    }
    return err;
}

int sim_manager_stop(void) {
    if (!s_inited) {
        return -1;
    }

    int err = stop_mode(s_current);
    s_current = SIM_MODE_NONE;
    return err;
}

sim_mode_t sim_manager_current(void) {
    return s_current;
}

int fluid_loop(){
    return 0;
}

int setup_fluid(){
    // gravity_init();

    int err = gravity_sensor_start();
    if (err != 0) {
      Serial.println("mpu start failed");
    }

    // 初始化仿真管理器
    sim_runtime_config_t cfg = {
      .core_id = 1,
      .stack_size = 8192,
      .priority = 5,
      .stop_timeout_ms = 1000,
    };

    err = sim_manager_init(&cfg);
    if (err != 0) {
      return -2;
    }

    // 启动水流体仿真模式
    err = sim_manager_start(g_boot_mode);
    if (err != 0) {
        // Serial.printf("sim_manager_start failed: %d\n", err);
        return -3;
    }

    return 0;
}

int unload_fluid(){
    if (s_idle_timeout_ms==0)
        gravity_sensor_sleep();
    sim_manager_stop();

    return 0;
}
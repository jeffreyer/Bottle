#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDLE_TIMEOUT_DEFAULT 15
#define BRIGHTNESS_FADE_STEP 1

extern uint32_t s_idle_timeout_ms;

typedef void (*sleep_manager_key_cb_t)(void);

// 初始化睡眠管理器
// key_pin: 按键引脚（GPIO4）
// idle_timeout_ms: 无操作超时时间（毫秒），默认15000
int sleep_manager_init(uint8_t key_pin);

// 启动睡眠管理器（需要在主循环中定期调用）
int sleep_manager_start(void);

// 处理睡眠管理逻辑（在主循环中调用）
void sleep_manager_update();

void enter_deep_sleep(void);

#ifdef __cplusplus
}
#endif


#ifndef GRAVITY_H
#define GRAVITY_H

#include <stdbool.h>

typedef struct {
    float gx;
    float gy;
    float gz;
    bool valid;
} gravity_xy_t;

void gravity_init(void);
void gravity_set(float gx, float gy, float gz);
gravity_xy_t gravity_get(void);
bool gravity_is_valid(void);

// Start gravity sensor producer task (current backend: MPU6050).
int gravity_sensor_start(void);  // 返回0表示成功，非0表示失败

// 让重力传感器（MPU6050）进入睡眠模式以降低功耗
void gravity_sensor_sleep(void);

#endif


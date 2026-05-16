#include "gravity.h"
#include "common.h"
#include "mpu6050.h"
#include "lis3dh.h"
#include <math.h>
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// I2C配置 - 修改为GPIO17/GPIO18
#define I2C_MASTER_SCL_IO      18
#define I2C_MASTER_SDA_IO      17
#define I2C_MASTER_FREQ_HZ     400000

#define SENSOR_TASK_HZ 80
#define SENSOR_LPF_ALPHA 0.40f
#define G_CLAMP 1.5f

#define GX_FROM_AX 1
#define GY_FROM_AY 1
#define GX_SIGN (-1.0f)
#define GY_SIGN (-1.0f)

static float s_gx = 0.0f;
static float s_gy = 0.0f;
static float s_gz = 0.0f;
static bool s_valid = false;

static mpu6050_handle_t s_mpu = NULL;
static bool s_sensor_running = false;

static inline float clampf_fast(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline void normalize_to_g(float* ax, float* ay, float* az) {
    float mag = sqrtf((*ax) * (*ax) + (*ay) * (*ay) + (*az) * (*az));
    if (mag > 5.0f) {
        const float inv_g = 1.0f / 9.80665f;
        *ax *= inv_g;
        *ay *= inv_g;
        *az *= inv_g;
    }
}

static void gravity_sensor_task(void* arg) {
    (void)arg;
    
    float gx_f = 0.0f;
    float gy_f = 0.0f;
    float gz_f = 0.0f;
    
    while (s_sensor_running) {
        if (!s_mpu) {
            delay(200);
            continue;
        }
        
        mpu6050_acce_value_t acce;
        #ifdef MPU6050
        if (mpu6050_get_acce(s_mpu, &acce)) {
        #endif
        #ifdef LIS3DH
        if (lis3dh_read_accel(s_mpu, &acce)) {
        #endif
            float ax = acce.acce_x;
            float ay = acce.acce_y;
            float az = acce.acce_z;

            normalize_to_g(&ax, &ay, &az);
            
            float gx = 0.0f;
            float gy = 0.0f;
            float gz = 0.0f;
            
#if GX_FROM_AX
            gx = ax;
#else
            gx = ay;
#endif
            
#if GY_FROM_AY
            gy = ay;
#else
            gy = ax;
#endif
            
            gx *= -1;
            gy *= GY_SIGN;
            gz = az;
            
            gx = clampf_fast(gx, -G_CLAMP, G_CLAMP);
            gy = clampf_fast(gy, -G_CLAMP, G_CLAMP);
            gz = clampf_fast(gz, -G_CLAMP, G_CLAMP);
            
            gx_f = (1.0f - SENSOR_LPF_ALPHA) * gx_f + SENSOR_LPF_ALPHA * gx;
            gy_f = (1.0f - SENSOR_LPF_ALPHA) * gy_f + SENSOR_LPF_ALPHA * gy;
            gz_f = (1.0f - SENSOR_LPF_ALPHA) * gz_f + SENSOR_LPF_ALPHA * gz;
            
            gravity_set(gx_f, gy_f, gz_f);
        }
        
        delay(1000 / SENSOR_TASK_HZ);
    }
}

void gravity_init(void) {
    s_gx = 0.0f;
    s_gy = 0.0f;
    s_gz = 0.0f;
    s_valid = false;
}

void gravity_set(float gx, float gy, float gz) {
    s_gx = gx;
    s_gy = gy;
    s_gz = gz;
    s_valid = true;
}

gravity_xy_t gravity_get(void) {
    gravity_xy_t out;
    out.gx = s_gx;
    out.gy = s_gy;
    out.gz = s_gz;
    out.valid = s_valid;
    return out;
}

bool gravity_is_valid(void) {
    return s_valid;
}

int gravity_sensor_start(void) {
    if (s_mpu) {
        #ifdef MPU6050
        mpu6050_wake_up(s_mpu);
        return 0;  // 已经启动
        #endif
        #ifdef LIS3DH
        lis_wake_up(s_mpu);
        return 0;  // 已经启动
        #endif
    }
    
    // 初始化I2C
    Wire.begin(I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    Wire.setClock(I2C_MASTER_FREQ_HZ);
    
    #ifdef MPU6050
    s_mpu = mpu6050_create(MPU6050_I2C_ADDRESS);
    if (!s_mpu) {
        return -1;
    }
    
    if (!mpu6050_config(s_mpu, ACCE_FS_4G, GYRO_FS_500DPS)) {
        mpu6050_delete(s_mpu);
        s_mpu = NULL;
        return -2;
    }
    
    if (!mpu6050_wake_up(s_mpu)) {
        mpu6050_delete(s_mpu);
        s_mpu = NULL;
        return -3;
    }
    #endif
    #ifdef LIS3DH
    s_mpu = lis_create(LIS3DH_ADDR);
    #endif
    
    s_sensor_running = true;
    xTaskCreatePinnedToCore(gravity_sensor_task, "gravity_task", 4096, NULL, 6, NULL, 0);
    
    return 0;
}

void gravity_sensor_sleep(void) {
    // 在系统进入深度睡眠前，将 MPU6050 配置为睡眠模式以降低功耗
    if (s_mpu) {
        #ifdef MPU6050
        mpu6050_enter_sleep(s_mpu);
        #endif
        #ifdef LIS3DH
        lis_enter_sleep(s_mpu);
        #endif
    }
}


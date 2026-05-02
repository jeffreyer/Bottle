#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>

#define MPU6050_I2C_ADDRESS         0x68u
#define MPU6050_I2C_ADDRESS_1       0x69u
#define MPU6050_WHO_AM_I_VAL        0x68u

typedef enum {
    ACCE_FS_2G  = 0,
    ACCE_FS_4G  = 1,
    ACCE_FS_8G  = 2,
    ACCE_FS_16G = 3,
} mpu6050_acce_fs_t;

typedef enum {
    GYRO_FS_250DPS  = 0,
    GYRO_FS_500DPS  = 1,
    GYRO_FS_1000DPS = 2,
    GYRO_FS_2000DPS = 3,
} mpu6050_gyro_fs_t;

typedef struct {
    float acce_x;
    float acce_y;
    float acce_z;
} mpu6050_acce_value_t;

typedef struct {
    float gyro_x;
    float gyro_y;
    float gyro_z;
} mpu6050_gyro_value_t;

typedef void* mpu6050_handle_t;

mpu6050_handle_t mpu6050_create(uint8_t dev_addr);
void mpu6050_delete(mpu6050_handle_t sensor);
bool mpu6050_config(mpu6050_handle_t sensor, mpu6050_acce_fs_t acce_fs, mpu6050_gyro_fs_t gyro_fs);
bool mpu6050_wake_up(mpu6050_handle_t sensor);
bool mpu6050_enter_sleep(mpu6050_handle_t sensor);
bool mpu6050_get_acce(mpu6050_handle_t sensor, mpu6050_acce_value_t* acce_value);

#endif


#include "mpu6050.h"
#include <Wire.h>
#include <math.h>

// MPU6050寄存器地址
#define MPU6050_REG_SMPLRT_DIV     0x19
#define MPU6050_REG_CONFIG         0x1A
#define MPU6050_REG_GYRO_CONFIG    0x1B
#define MPU6050_REG_ACCEL_CONFIG   0x1C
#define MPU6050_REG_WHO_AM_I       0x75
#define MPU6050_REG_PWR_MGMT_1     0x6B
#define MPU6050_REG_ACCEL_XOUT_H   0x3B
#define MPU6050_INT_PIN_CFG        0x37
#define MPU6050_INT_ENABLE         0x38
#define MPU6050_MOT_THR            0x1F
#define MPU6050_MOT_DUR            0x20
#define MPU6050_MOT_DETECT_CTRL    0x69

struct mpu6050_sensor {
    uint8_t dev_addr;
    float acce_sensitivity;
    float gyro_sensitivity;
};

static float get_acce_sensitivity(mpu6050_acce_fs_t fs) {
    switch (fs) {
        case ACCE_FS_2G:  return 16384.0f;
        case ACCE_FS_4G:  return 8192.0f;
        case ACCE_FS_8G:  return 4096.0f;
        case ACCE_FS_16G: return 2048.0f;
        default: return 8192.0f;
    }
}

static float get_gyro_sensitivity(mpu6050_gyro_fs_t fs) {
    switch (fs) {
        case GYRO_FS_250DPS:  return 131.0f;
        case GYRO_FS_500DPS:  return 65.5f;
        case GYRO_FS_1000DPS: return 32.8f;
        case GYRO_FS_2000DPS: return 16.4f;
        default: return 65.5f;
    }
}

static bool mpu6050_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    Wire.beginTransmission(dev_addr);
    Wire.write(reg);
    Wire.write(data);
    return Wire.endTransmission() == 0;
}

static bool mpu6050_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t* data, uint8_t len) {
    Wire.beginTransmission(dev_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    
    uint8_t received = Wire.requestFrom(dev_addr, len);
    if (received != len) {
        return false;
    }
    
    for (uint8_t i = 0; i < len; i++) {
        data[i] = Wire.read();
    }
    return true;
}

mpu6050_handle_t mpu6050_create(uint8_t dev_addr) {
    mpu6050_sensor* sensor = new mpu6050_sensor();
    if (!sensor) {
        return NULL;
    }
    
    sensor->dev_addr = dev_addr;
    sensor->acce_sensitivity = 8192.0f;
    sensor->gyro_sensitivity = 65.5f;
    
    // 检查设备ID
    uint8_t who_am_i;
    if (!mpu6050_read_reg(dev_addr, MPU6050_REG_WHO_AM_I, &who_am_i, 1)) {
        delete sensor;
        return NULL;
    }
    
    if (who_am_i != MPU6050_WHO_AM_I_VAL) {
        delete sensor;
        return NULL;
    }
    
    return (mpu6050_handle_t)sensor;
}

void mpu6050_delete(mpu6050_handle_t sensor) {
    if (sensor) {
        delete (mpu6050_sensor*)sensor;
    }
}

bool mpu6050_config(mpu6050_handle_t sensor, mpu6050_acce_fs_t acce_fs, mpu6050_gyro_fs_t gyro_fs) {
    if (!sensor) {
        return false;
    }
    
    mpu6050_sensor* s = (mpu6050_sensor*)sensor;
    
    // 配置加速度计
    uint8_t accel_config = (acce_fs << 3);
    if (!mpu6050_write_reg(s->dev_addr, MPU6050_REG_ACCEL_CONFIG, accel_config)) {
        return false;
    }
    s->acce_sensitivity = get_acce_sensitivity(acce_fs);
    
    // 配置陀螺仪
    uint8_t gyro_config = (gyro_fs << 3);
    if (!mpu6050_write_reg(s->dev_addr, MPU6050_REG_GYRO_CONFIG, gyro_config)) {
        return false;
    }
    s->gyro_sensitivity = get_gyro_sensitivity(gyro_fs);

    // //运动中断
    // if (!mpu6050_write_reg(s->dev_addr, MPU6050_MOT_THR, 5)) {
    //     return false;
    // }
    // if (!mpu6050_write_reg(s->dev_addr, MPU6050_MOT_DUR, 5)) {
    //     return false;
    // }
    // if (!mpu6050_write_reg(s->dev_addr, MPU6050_MOT_DETECT_CTRL, 0x15)) {
    //     return false;
    // }
    // if (!mpu6050_write_reg(s->dev_addr, MPU6050_INT_ENABLE, 0x40)) {
    //     return false;
    // }
    // // if (!mpu6050_write_reg(s->dev_addr, MPU6050_INT_PIN_CFG, 160)) {
    // if (!mpu6050_write_reg(s->dev_addr, MPU6050_INT_PIN_CFG, 0x80)) {
    //     return false;
    // }
    
    return true;
}

bool mpu6050_wake_up(mpu6050_handle_t sensor) {
    if (!sensor) {
        return false;
    }
    
    mpu6050_sensor* s = (mpu6050_sensor*)sensor;
    
    // 唤醒设备（清除睡眠位）
    uint8_t pwr_mgmt;
    if (!mpu6050_read_reg(s->dev_addr, MPU6050_REG_PWR_MGMT_1, &pwr_mgmt, 1)) {
        return false;
    }
    
    pwr_mgmt &= ~0x40; // 清除睡眠位
    return mpu6050_write_reg(s->dev_addr, MPU6050_REG_PWR_MGMT_1, pwr_mgmt);
}

bool mpu6050_enter_sleep(mpu6050_handle_t sensor) {
    if (!sensor) {
        return false;
    }

    mpu6050_sensor* s = (mpu6050_sensor*)sensor;

    // 设置睡眠位，使 MPU6050 进入低功耗睡眠模式
    uint8_t pwr_mgmt;
    if (!mpu6050_read_reg(s->dev_addr, MPU6050_REG_PWR_MGMT_1, &pwr_mgmt, 1)) {
        return false;
    }

    pwr_mgmt |= 0x40; // 置位睡眠位
    return mpu6050_write_reg(s->dev_addr, MPU6050_REG_PWR_MGMT_1, pwr_mgmt);
}

bool mpu6050_get_acce(mpu6050_handle_t sensor, mpu6050_acce_value_t* acce_value) {
    if (!sensor || !acce_value) {
        return false;
    }
    
    mpu6050_sensor* s = (mpu6050_sensor*)sensor;
    
    uint8_t data[6];
    if (!mpu6050_read_reg(s->dev_addr, MPU6050_REG_ACCEL_XOUT_H, data, 6)) {
        return false;
    }
    
    int16_t raw_x = (int16_t)((data[0] << 8) | data[1]);
    int16_t raw_y = (int16_t)((data[2] << 8) | data[3]);
    int16_t raw_z = (int16_t)((data[4] << 8) | data[5]);
    
    acce_value->acce_x = (float)raw_x / s->acce_sensitivity;
    acce_value->acce_y = (float)raw_y / s->acce_sensitivity;
    acce_value->acce_z = (float)raw_z / s->acce_sensitivity;
    
    return true;
}


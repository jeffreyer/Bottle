#ifndef LIS3DH_H
#define LIS3DH_H

#include <stdint.h>
#include <stdbool.h>
#include <Wire.h>

#define LIS3DH_ADDR 0x09

int sensor=1;

void write_reg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(LIS3DH_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t read_reg(uint8_t reg)
{
    Wire.beginTransmission(LIS3DH_ADDR);
    Wire.write(reg);

    Wire.endTransmission(false);

    Wire.requestFrom(LIS3DH_ADDR, 1);

    return Wire.read();
}

mpu6050_handle_t lis_create(uint8_t dev_addr) {
    write_reg(0x20, 0x37);
    write_reg(0x23, 0x80);
    
    return (mpu6050_handle_t)sensor;
}

bool lis_wake_up(mpu6050_handle_t sensor) {
    if (!sensor) {
        return false;
    }
    
    write_reg(0x20, 0x37);
    write_reg(0x23, 0x80);
    return true;
}

bool lis_enter_sleep(mpu6050_handle_t sensor) {
    if (!sensor) {
        return false;
    }

    write_reg(0x20, 0x07);
    
    return true;
}

bool lis3dh_read_accel(mpu6050_handle_t sensor, mpu6050_acce_value_t* acce_value) {
    if (!sensor || !acce_value) {
        return false;
    }
    
    uint8_t reg = 0x28 | 0x80;

    Wire.beginTransmission(LIS3DH_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(LIS3DH_ADDR, 6);

    uint8_t data[6];

    for(int i=0;i<6;i++)
    {
        data[i] = Wire.read();
    }

    int16_t raw_x =
        (int16_t)((data[1] << 8) | data[0]);

    int16_t raw_y =
        (int16_t)((data[3] << 8) | data[2]);

    int16_t raw_z =
        (int16_t)((data[5] << 8) | data[4]);
    
    // Serial.printf("Raw accel: ax=%.3f, ay=%.3f, az=%.3f\n", (float)raw_x, (float)raw_y, (float)raw_z);
    acce_value->acce_x = -1.0f * (float)raw_y / 16384.0f;
    acce_value->acce_y = (float)raw_x / 16384.0f;
    acce_value->acce_z = (float)raw_z / 16384.0f;
    
    return true;
}

#endif


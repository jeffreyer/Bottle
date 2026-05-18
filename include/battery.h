#pragma once

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "lua_hardware_api.h"

#define ADC_CHANNEL ADC_CHANNEL_6   // GPIO17
#define ADC_UNIT    ADC_UNIT_2

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool calibrated=false;
static bool is_low_bat=false;

static bool adc_calibration_init(void)
{
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    return adc_cali_create_scheme_curve_fitting(
               &cali_config,
               &adc_cali_handle
           ) == ESP_OK;
}

static void check_bat(){
    int raw = 0;
    int voltage = 0;

    // 多次平均
    uint32_t sum = 0;

    for (int i = 0; i < 5; i++)
    {
        int val;
        adc_oneshot_read(
            adc_handle,
            ADC_CHANNEL,
            &val
        );

        sum += val;

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    raw = sum / 5;

    if (calibrated)
    {
        adc_cali_raw_to_voltage(
            adc_cali_handle,
            raw,
            &voltage
        );
    }

    // printf("RAW=%d  Voltage=%d mV\n", raw, voltage);
    // draw_led_text(String(voltage).c_str(), 1, 1, 130, 0, 0);
    // FastLED.show();
    if (voltage<3150){
        is_low_bat=true;
        
    }
}

static int check_low_battery() {
     // ADC 初始化
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(&init_config, &adc_handle)
    );

    // 配置通道
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            ADC_CHANNEL,
            &config
        )
    );
   
    calibrated = adc_calibration_init();

    return 0;
}
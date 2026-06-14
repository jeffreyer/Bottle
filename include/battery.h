#pragma once

#include <stdio.h>
#include "common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "lua_hardware_api.h"

#ifdef BOTTLE_V4
#define ADC_CHANNEL ADC_CHANNEL_4   // GPIO15
#define ADC_UNIT    ADC_UNIT_2
#else
#define ADC_CHANNEL ADC_CHANNEL_6   // GPIO17
#define ADC_UNIT    ADC_UNIT_2
#endif

static bool is_low_bat=false;

int check_bat();

int check_battery_init();
#include "driver/touch_sens.h"
#include "driver/touch_version_types.h"

static int s_channel_id[1] = {TOUCH_PIN};
// Active threshold to benchmark ratio. (i.e., touch will be activated when data >= benchmark * (1 + ratio))
static float s_thresh2bm_ratio = 0.006;  // 2%
static char *TAG = "touch_sleep";

static void touch_do_initial_scanning(touch_sensor_handle_t sens_handle, touch_channel_handle_t chan_handle)
{
    /* Enable the touch sensor to do the initial scanning, so that to initialize the channel data */
    ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));

    /* Scan the enabled touch channels for several times, to make sure the initial channel data is stable */
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(sens_handle, 2000));
    }

    /* Disable the touch channel to rollback the state */
    ESP_ERROR_CHECK(touch_sensor_disable(sens_handle));

    /* (Optional) Read the initial channel benchmark and reconfig the channel active threshold accordingly */
    // printf("Initial benchmark and new threshold are:\n");
    for (int i = 0; i < 1; i++) {
        /* Read the initial benchmark of the touch channel */
        uint32_t benchmark[1] = {};
        ESP_ERROR_CHECK(touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark));
        /* Calculate the proper active thresholds regarding the initial benchmark */
        // printf("Touch [CH %d]", s_channel_id[i]);
        /* Generate the default channel configuration and then update the active threshold based on the real benchmark */
        touch_channel_config_t chan_cfg = {
            .active_thresh = {2000},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };
        for (int j = 0; j < 1; j++) {
          chan_cfg.active_thresh[j] = (uint32_t)(benchmark[j] * s_thresh2bm_ratio);
          delay(3000);
          printf(" %d:%d  %d\n", j, benchmark[j], chan_cfg.active_thresh[j]);
        }
        // printf("\n");
        /* Update the channel configuration */
        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chan_handle, &chan_cfg));
    }
}

typedef bool (*on_active)(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx);
typedef bool (*on_inactive)(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx);

void touch_sleep_init(on_active act,on_inactive inact){
  touch_sensor_handle_t sens_handle = NULL;
  touch_channel_handle_t chan_handle;

  // 正常运行时的采样配置
  touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
    TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
  };
  touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
  ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens_handle));

  touch_channel_config_t chan_cfg = {
    .active_thresh = {300},
    .charge_speed = TOUCH_CHARGE_SPEED_7,
    .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
  };
  for (int i = 0; i < 1; i++) {
      ESP_ERROR_CHECK(touch_sensor_new_channel(sens_handle, s_channel_id[i], &chan_cfg, &chan_handle));
      touch_chan_info_t chan_info = {};
      ESP_ERROR_CHECK(touch_sensor_get_channel_info(chan_handle, &chan_info));
      printf("Touch [CH %d] enabled on GPIO%d\n", s_channel_id[i], chan_info.chan_gpio);
  }

  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ESP_ERROR_CHECK(touch_sensor_config_filter(sens_handle, &filter_cfg));

  // touch_do_initial_scanning(sens_handle, chan_handle);

  touch_event_callbacks_t callbacks = {
      .on_active = act,
      .on_inactive = inact,
  };
  ESP_ERROR_CHECK(touch_sensor_register_callbacks(sens_handle, &callbacks, NULL));

  // 深度睡眠时的超低功耗配置
  // 目标：实现接近18uA的功耗
  // 策略：极大增加测量间隔，最小化充电次数和电压
  // 正常运行：meas_interval=32us，深度睡眠：meas_interval=16000us（500倍）
  // 正常运行：charge_times=500，深度睡眠：charge_times=50（10倍降低）
  static touch_sensor_sample_config_t deep_sleep_sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
    TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(200, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
  };
  static touch_sensor_config_dslp_t deep_sleep_sens_cfg = {
    .power_on_wait_us = 128,        // 降低上电等待时间（从256降到128）
    .meas_interval_us = 16000.0,    // 测量间隔从3200us增加到16000us（16ms，约0.2%占空比）
    .max_meas_time_us = 0,          // 不限制测量时间
    .sample_cfg_num = 1,
    .sample_cfg = deep_sleep_sample_cfg,
  };

  // 配置深度睡眠唤醒
  touch_sleep_config_t slp_cfg = {
    .slp_wakeup_lvl = TOUCH_DEEP_SLEEP_WAKEUP,
    .deep_slp_chan = chan_handle,  // 指定唤醒通道
    .deep_slp_thresh = {30},     // 深度睡眠阈值
    .deep_slp_sens_cfg = &deep_sleep_sens_cfg,  // 使用超低功耗配置
  };
  ESP_ERROR_CHECK(touch_sensor_config_sleep_wakeup(sens_handle, &slp_cfg));

  ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));
  ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens_handle));

  ESP_LOGI(TAG, "touch wakeup source is ready with ultra-low power deep sleep config");
}
#include "audio_record.h"

#include <Arduino.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "common.h"
#include "audio_fft.h"
#include <stdio.h>

#define I2S_WS_PIN 10
#define I2S_SD_PIN 6  // I2S MIC only
#define I2S_BCK_PIN 7
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 512
#define SAMPLE_FREQ 16000

namespace {

struct AudioRecordContext {
  i2s_chan_handle_t rx_handle = nullptr;
  TaskHandle_t record_task = nullptr;
  int16_t* dma_buffer = nullptr;
  volatile bool exit_requested = false;
  volatile bool is_recording = false;
  FILE* output_file = nullptr;
  uint32_t bytes_written = 0;
  char file_path[128];
};

AudioRecordContext g_ctx;

void record_task_entry(void*) {
  while (!g_ctx.exit_requested) {
    if (!g_ctx.is_recording) {
      delay(10);
      continue;
    }

    size_t bytes_read = 0;
    if (i2s_channel_read(g_ctx.rx_handle, g_ctx.dma_buffer, DMA_BUF_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
      continue;
    }

    if (g_ctx.output_file && bytes_read > 0) {
      size_t written = fwrite(g_ctx.dma_buffer, 1, bytes_read, g_ctx.output_file);
      g_ctx.bytes_written += written;

      // 每写入一定量数据后 flush 一次
      if (g_ctx.bytes_written % (DMA_BUF_LEN * sizeof(int16_t) * 10) == 0) {
        fflush(g_ctx.output_file);
      }
    }
  }

  vTaskDelete(nullptr);
}

}  // namespace

int audio_record_init() {
  memset(&g_ctx, 0, sizeof(g_ctx));
  return 0;
}

int audio_record_start(const char* file_path) {
  if (g_ctx.is_recording) {
    Serial.println("[audio_record] Already recording");
    return -1;
  }

  if (!file_path || strlen(file_path) == 0) {
    Serial.println("[audio_record] Invalid file path");
    return -2;
  }

  strncpy(g_ctx.file_path, file_path, sizeof(g_ctx.file_path) - 1);
  g_ctx.file_path[sizeof(g_ctx.file_path) - 1] = '\0';

  // 打开文件用于写入（使用标准POSIX API访问VFS挂载点）
  g_ctx.output_file = fopen(g_ctx.file_path, "wb");
  if (g_ctx.output_file == NULL) {
    Serial.printf("[audio_record] Failed to open file: %s\n", g_ctx.file_path);
    return -3;
  }

  g_ctx.exit_requested = false;
  g_ctx.bytes_written = 0;

  if (!g_ctx.dma_buffer) {
    g_ctx.dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
    if (!g_ctx.dma_buffer) {
      Serial.println("[audio_record] Failed to allocate DMA buffer");
      fclose(g_ctx.output_file);
      g_ctx.output_file = nullptr;
      return -4;
    }
  }

  // 配置 I2S 通道
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.id = I2S_NUM_0;
  chan_cfg.dma_desc_num = DMA_BUF_COUNT;
  chan_cfg.dma_frame_num = DMA_BUF_LEN;
  if (i2s_new_channel(&chan_cfg, nullptr, &g_ctx.rx_handle) != ESP_OK) {
    Serial.println("[audio_record] Failed to create I2S channel");
    fclose(g_ctx.output_file);
    g_ctx.output_file = nullptr;
    return -5;
  }

  // 根据 is_i2s_mic 配置 PDM 或 I2S 标准模式
  if (!is_i2s_mic) {
    i2s_pdm_rx_config_t pdm_cfg = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),
      .slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .slot_mask = I2S_PDM_SLOT_LEFT,
      },
      .gpio_cfg = {
        .clk = (gpio_num_t)I2S_BCK_PIN,
        .din = (gpio_num_t)I2S_WS_PIN,
      },
    };

    if (i2s_channel_init_pdm_rx_mode(g_ctx.rx_handle, &pdm_cfg) != ESP_OK) {
      Serial.println("[audio_record] Failed to init PDM RX mode");
      i2s_del_channel(g_ctx.rx_handle);
      g_ctx.rx_handle = nullptr;
      fclose(g_ctx.output_file);
      g_ctx.output_file = nullptr;
      return -6;
    }
  } else {
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)I2S_BCK_PIN,
        .ws = (gpio_num_t)I2S_WS_PIN,
        .dout = I2S_GPIO_UNUSED,
        .din = (gpio_num_t)I2S_SD_PIN,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false,
        },
      },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;

    if (i2s_channel_init_std_mode(g_ctx.rx_handle, &std_cfg) != ESP_OK) {
      Serial.println("[audio_record] Failed to initialize I2S channel");
      i2s_del_channel(g_ctx.rx_handle);
      g_ctx.rx_handle = nullptr;
      fclose(g_ctx.output_file);
      g_ctx.output_file = nullptr;
      return -7;
    }
  }

  if (i2s_channel_enable(g_ctx.rx_handle) != ESP_OK) {
    Serial.println("[audio_record] Failed to enable I2S channel");
    i2s_del_channel(g_ctx.rx_handle);
    g_ctx.rx_handle = nullptr;
    fclose(g_ctx.output_file);
    g_ctx.output_file = nullptr;
    return -8;
  }

  // 启动录制任务
  xTaskCreatePinnedToCore(record_task_entry, "AudioRecord", 10000, nullptr, 1, &g_ctx.record_task, 0);
  g_ctx.is_recording = true;

  Serial.printf("[audio_record] Started recording to: %s\n", g_ctx.file_path);
  return 0;
}

void audio_record_stop() {
  if (!g_ctx.is_recording) {
    return;
  }

  g_ctx.is_recording = false;
  g_ctx.exit_requested = true;
  delay(50);

  if (g_ctx.rx_handle) {
    i2s_channel_disable(g_ctx.rx_handle);
    i2s_del_channel(g_ctx.rx_handle);
    g_ctx.rx_handle = nullptr;
  }

  if (g_ctx.output_file) {
    fflush(g_ctx.output_file);
    fclose(g_ctx.output_file);
    g_ctx.output_file = nullptr;
    Serial.printf("[audio_record] Stopped recording. Wrote %u bytes to %s\n", g_ctx.bytes_written, g_ctx.file_path);
  }

  if (g_ctx.dma_buffer) {
    free(g_ctx.dma_buffer);
    g_ctx.dma_buffer = nullptr;
  }

  Serial.println("[audio_record] Stopped");
}

bool audio_record_is_recording() {
  return g_ctx.is_recording;
}

uint32_t audio_record_get_bytes_written() {
  return g_ctx.bytes_written;
}

void audio_record_cleanup() {
  audio_record_stop();
}

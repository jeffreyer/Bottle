#include "audio_fft.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "common.h"

#define I2S_WS_PIN 10
#define I2S_SD_PIN    6 //I2S MIC only
#define I2S_BCK_PIN 7
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 512
#define SAMPLE_FREQ 16000

bool is_i2s_mic=false;

namespace {

struct AudioFFTContext {
  i2s_chan_handle_t rx_handle = nullptr;
  TaskHandle_t fft_task = nullptr;
  int16_t* dma_buffer = nullptr;
  volatile bool exit_requested = false;
};

AudioFFTContext g_ctx;

double g_real[DMA_BUF_LEN];
double g_imag[DMA_BUF_LEN];
double g_fft_mag[DMA_BUF_LEN];
ArduinoFFT<double> g_fft(g_real, g_imag, DMA_BUF_LEN, SAMPLE_FREQ);

void fft_task_entry(void*) {
  while (!g_ctx.exit_requested) {
    delay(10);
    
    size_t bytes_read = 0;
    if (i2s_channel_read(g_ctx.rx_handle, g_ctx.dma_buffer, DMA_BUF_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
      continue;
    }

    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_real[i] = g_ctx.dma_buffer[i];
      g_imag[i] = 0.0;
    }

    g_fft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    g_fft.compute(FFT_FORWARD);
    g_fft.complexToMagnitude();
    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_fft_mag[i] = abs(g_real[i]);
    }
  }

  vTaskDelete(nullptr);
}

}  // namespace

int audio_fft_init() {
  memset(&g_ctx, 0, sizeof(g_ctx));
  return 0;
}

int audio_fft_start() {
  g_ctx.exit_requested = false;

  if (!g_ctx.dma_buffer) {
    g_ctx.dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
    if (!g_ctx.dma_buffer) {
      Serial.println("[audio_fft] Failed to allocate DMA buffer");
      return -1;
    }
  }

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.id = I2S_NUM_0;
  chan_cfg.dma_desc_num = DMA_BUF_COUNT;
  chan_cfg.dma_frame_num = DMA_BUF_LEN;
  if (i2s_new_channel(&chan_cfg, nullptr, &g_ctx.rx_handle) != ESP_OK) {
    Serial.println("[audio_fft] Failed to create I2S channel");
    return -2;
  }

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
      Serial.println("[audio_fft] Failed to init PDM RX mode");
      return -3;
    }

  } else {
    // 配置 I2S 标准模式参数
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),    // 时钟配置
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO), // 时隙配置 (16位，单声道)
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,       // 不使用主时钟
            .bclk = (gpio_num_t)I2S_BCK_PIN,
            .ws   = (gpio_num_t)I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,       // 不使用数据输出
            .din  = (gpio_num_t)I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;

    // 初始化 I2S 通道
    if (i2s_channel_init_std_mode(g_ctx.rx_handle, &std_cfg) != ESP_OK) {
        Serial.println("Error: Failed to initialize I2S channel");
        return -3;
    }
  }

  if (i2s_channel_enable(g_ctx.rx_handle) != ESP_OK) {
    Serial.println("[audio_fft] Failed to enable I2S channel");
    return -4;
  }

  xTaskCreatePinnedToCore(fft_task_entry, "AudioFFT", 10000, nullptr, 1, &g_ctx.fft_task, 0);
  Serial.println("[audio_fft] Started successfully");
  return 0;
}

void audio_fft_stop() {
  g_ctx.exit_requested = true;
  delay(50);

  if (g_ctx.rx_handle) {
    i2s_channel_disable(g_ctx.rx_handle);
    i2s_del_channel(g_ctx.rx_handle);
    g_ctx.rx_handle = nullptr;
  }

  if (g_ctx.dma_buffer) {
    free(g_ctx.dma_buffer);
    g_ctx.dma_buffer = nullptr;
  }

  Serial.println("[audio_fft] Stopped");
}

const double* audio_fft_get_magnitude() {
  return g_fft_mag;
}

int audio_fft_get_magnitude_length() {
  return DMA_BUF_LEN;
}

void audio_fft_cleanup() {
  audio_fft_stop();
}

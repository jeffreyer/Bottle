#include "audio_fft.h"

#include <Arduino.h>
#include <arduinoFFT.h>
#include "driver/i2s_pdm.h"

#define I2S_WS_PIN 10
#define I2S_BCK_PIN 7
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 512
#define SAMPLE_FREQ 16000

namespace {

struct AudioFFTContext {
  i2s_chan_handle_t rx_handle = nullptr;
  TaskHandle_t fft_task = nullptr;
  int16_t* dma_buffer = nullptr;
  volatile bool exit_requested = false;
  float spectrum[17];
};

AudioFFTContext g_ctx;

double g_real[DMA_BUF_LEN];
double g_imag[DMA_BUF_LEN];
double g_fft_mag[DMA_BUF_LEN];
ArduinoFFT<double> g_fft(g_real, g_imag, DMA_BUF_LEN, SAMPLE_FREQ);

double fft_add(int from, int to) {
  double result = 0.0;
  for (int i = from; i <= to; i++) {
    result += g_fft_mag[i];
  }
  return result;
}

void publish_spectrum(const double* fft_data) {
  static const float boost[17] = {
    0.4f, 0.5f, 0.5f, 0.5f, 0.6f, 0.8f, 1.1f, 1.1f, 1.5f,
    1.7f, 3.0f, 3.4f, 3.6f, 3.6f, 3.8f, 3.8f, 1.0f
  };

  for (int i = 0; i < 17; i++) {
    float v = (float)fft_data[i] * boost[i] * 12.0f / 50.0f;
    g_ctx.spectrum[i] = constrain((int)v, 0, 255);
  }
}

void fft_task_entry(void*) {
  while (!g_ctx.exit_requested) {
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

    double fft_data[17];
    fft_data[0]  = fft_add(6, 7) / 2;
    fft_data[1]  = fft_add(8, 10) / 3;
    fft_data[2]  = fft_add(11, 15) / 5;
    fft_data[3]  = fft_add(16, 20) / 5;
    fft_data[4]  = fft_add(21, 25) / 5;
    fft_data[5]  = fft_add(26, 31) / 6;
    fft_data[6]  = fft_add(32, 37) / 6;
    fft_data[7]  = fft_add(38, 43) / 6;
    fft_data[8]  = fft_add(44, 49) / 6;
    fft_data[9]  = fft_add(50, 55) / 6;
    fft_data[10] = fft_add(56, 61) / 6;
    fft_data[11] = fft_add(62, 67) / 6;
    fft_data[12] = fft_add(68, 73) / 6;
    fft_data[13] = fft_add(74, 79) / 6;
    fft_data[14] = fft_add(80, 85) / 6;
    fft_data[15] = fft_add(86, 91) / 6;

    double high = fft_add(92, 97);
    high = max(high, fft_add(98, 103));
    high = max(high, fft_add(104, 109));
    high = max(high, fft_add(110, 115));
    high = max(high, fft_add(116, 121));
    high = max(high, fft_add(122, 127));
    high = max(high, fft_add(128, 133));
    fft_data[16] = high / 6;

    publish_spectrum(fft_data);
  }

  g_ctx.fft_task = nullptr;
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

const float* audio_fft_get_spectrum() {
  return g_ctx.spectrum;
}

void audio_fft_cleanup() {
  audio_fft_stop();
}

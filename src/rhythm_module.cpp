#include "rhythm_module.h"

#include <Arduino.h>
#include <FastLED.h>
#include <arduinoFFT.h>
#include "app_control.h"
#include "common.h"
#include "driver/i2s_pdm.h"
#include "bottle_vm.h"
#include "gravity.h"
#include "module_runtime.h"
#include "module_storage.h"

#define I2S_WS_PIN 10
#define I2S_BCK_PIN 7
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 512
#define SAMPLE_FREQ 16000

namespace {

const char* kDefaultConfigKey = "style";

struct RhythmHost {
  module_context_t module = {};
  i2s_chan_handle_t rx_handle = nullptr;
  TaskHandle_t fft_task = nullptr;
  int16_t* dma_buffer = nullptr;
  volatile bool exit_requested = false;
  int saved_subpage = -1;
  bottle_program_t program = {};
  bottle_vm_t vm = {};
  bool script_load_ok = false;
};

RhythmHost g_host;

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
  static const float boost[MATRIX_WIDTH] = {
    0.4f, 0.5f, 0.5f, 0.5f, 0.6f, 0.8f, 1.1f, 1.1f, 1.5f,
    1.7f, 3.0f, 3.4f, 3.6f, 3.6f, 3.8f, 3.8f, 1.0f
  };

  for (int i = 0; i < MATRIX_WIDTH; i++) {
    float v = (float)fft_data[i] * boost[i] * 12.0f / 50.0f;
    g_host.module.sensor.spectrum[i] = (uint8_t)constrain((int)v, 0, 255);
  }
}

void fft_task_entry(void*) {
  while (!g_host.exit_requested) {
    size_t bytes_read = 0;
    if (i2s_channel_read(g_host.rx_handle, g_host.dma_buffer, DMA_BUF_LEN * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
      continue;
    }

    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_real[i] = g_host.dma_buffer[i];
      g_imag[i] = 0.0;
    }

    g_fft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    g_fft.compute(FFT_FORWARD);
    g_fft.complexToMagnitude();
    for (int i = 0; i < DMA_BUF_LEN; i++) {
      g_fft_mag[i] = abs(g_real[i]);
    }

    double fft_data[MATRIX_WIDTH];
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

  g_host.fft_task = nullptr;
  vTaskDelete(nullptr);
}

int start_audio_capture() {
  g_host.exit_requested = false;
  if (!g_host.dma_buffer) {
    g_host.dma_buffer = (int16_t*)malloc(DMA_BUF_LEN * sizeof(int16_t));
    if (!g_host.dma_buffer) return -1;
  }

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.id = I2S_NUM_0;
  chan_cfg.dma_desc_num = DMA_BUF_COUNT;
  chan_cfg.dma_frame_num = DMA_BUF_LEN;
  if (i2s_new_channel(&chan_cfg, nullptr, &g_host.rx_handle) != ESP_OK) return -2;

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
  if (i2s_channel_init_pdm_rx_mode(g_host.rx_handle, &pdm_cfg) != ESP_OK) return -3;
  if (i2s_channel_enable(g_host.rx_handle) != ESP_OK) return -4;

  xTaskCreatePinnedToCore(fft_task_entry, "RhythmFFT", 10000, nullptr, 1, &g_host.fft_task, 0);
  return 0;
}

void stop_audio_capture() {
  g_host.exit_requested = true;
  delay(50);

  if (g_host.rx_handle) {
    i2s_channel_disable(g_host.rx_handle);
    i2s_del_channel(g_host.rx_handle);
    g_host.rx_handle = nullptr;
  }

  if (g_host.dma_buffer) {
    free(g_host.dma_buffer);
    g_host.dma_buffer = nullptr;
  }
}

void update_gravity_snapshot() {
  gravity_xy_t g = gravity_get();
  g_host.module.sensor.gravity.valid = g.valid;
  g_host.module.sensor.gravity.x = g.gx;
  g_host.module.sensor.gravity.y = g.gy;
  g_host.module.sensor.gravity.z = g.gz;
}

}  // namespace

int setup_rhythm_module(void) {
  int err = gravity_sensor_start();
  if (err != 0) {
    Serial.println("mpu start failed");
  }

  brightness_max = 10;
  FastLED.setBrightness(10);
  Serial.printf("[rhythm_module] Set brightness to 10, current FastLED brightness: %d\n", FastLED.getBrightness());
  if (g_host.saved_subpage < 0) {
    g_host.saved_subpage = load_config(kDefaultConfigKey);
  }
  subpage_index = g_host.saved_subpage;

  g_host.module.now_ms = millis();

  // Load and compile script
  Serial.println("[rhythm_module] Loading script from SPIFFS...");
  String script = module_storage_read_text("/rhythm_spectrum/main.bottle");
  Serial.printf("[rhythm_module] Script length: %d bytes\n", script.length());
  if (script.length() == 0) {
    Serial.println("[rhythm_module] ERROR: Failed to read rhythm script from SPIFFS");
    return -5;
  }

  Serial.println("[rhythm_module] Compiling script...");
  g_host.script_load_ok = bottle_compile(script.c_str(), &g_host.program);
  if (!g_host.script_load_ok) {
    Serial.println("[rhythm_module] ERROR: Failed to compile rhythm script");
    if (g_host.program.error.has_error) {
      bottle_error_print(&g_host.program.error, "Setup");
    }
    return -5;
  }
  Serial.println("[rhythm_module] Script compiled successfully");

  // Initialize VM
  Serial.println("[rhythm_module] Initializing VM...");
  bottle_vm_init(&g_host.vm, &g_host.program);

  // Run setup section if present
  if (g_host.program.has_setup) {
    Serial.println("[rhythm_module] Running setup block...");
    bottle_vm_execute(&g_host.vm, &g_host.program, g_host.program.setup_offset, &g_host.module);
    if (g_host.vm.error.has_error) {
      Serial.println("[rhythm_module] ERROR in setup block:");
      bottle_error_print(&g_host.vm.error, "Setup");
    } else {
      Serial.println("[rhythm_module] Setup block completed successfully");
    }
  }

  return start_audio_capture();
}

int unload_rhythm_module(void) {
  g_host.saved_subpage = subpage_index;
  save_config(kDefaultConfigKey, subpage_index);

  // Run unload section if present
  if (g_host.script_load_ok && g_host.program.has_unload) {
    bottle_vm_execute(&g_host.vm, &g_host.program, g_host.program.unload_offset, &g_host.module);
  }

  gravity_sensor_sleep();
  stop_audio_capture();
  return 0;
}

int loop_rhythm_module(void) {
  static int loop_count = 0;
  update_gravity_snapshot();
  g_host.module.now_ms = millis();

  // Debug: print spectrum values on first few frames
  if (loop_count < 3) {
    Serial.printf("[rhythm_module] Frame %d spectrum values: ", loop_count);
    for (int i = 0; i < 5; i++) {
      Serial.printf("%d ", g_host.module.sensor.spectrum[i]);
    }
    Serial.println();
  }

  // Load config values into VM scalars (for config access in scripts)
  if (g_host.script_load_ok) {
    for (uint8_t i = 0; i < g_host.program.config_count; i++) {
      const char* key = g_host.program.configs[i].key;
      int32_t value = g_host.module.config.get_int(key, g_host.program.configs[i].default_value);
      // Store in scalar slot after regular scalars
      if (g_host.program.scalar_count + i < BOTTLE_MAX_SCALARS) {
        g_host.vm.scalars[g_host.program.scalar_count + i] = (float)value;
      }
    }

    bottle_vm_run_loop(&g_host.vm, &g_host.program, &g_host.module);

    if (g_host.vm.error.has_error) {
      Serial.printf("[rhythm_module] ERROR in loop (frame %d):\n", loop_count);
      bottle_error_print(&g_host.vm.error, "Loop");
      g_host.script_load_ok = false; // Stop executing after error
    }

    loop_count++;
    if (loop_count == 1 || loop_count == 10 || loop_count % 100 == 0) {
      Serial.printf("[rhythm_module] Loop frame %d completed\n", loop_count);
    }
  } else {
    if (loop_count == 0) {
      Serial.println("[rhythm_module] WARNING: Script not loaded, skipping loop execution");
    }
  }

  return 0;
}

String rhythm_module_runtime_status_json(void) {
  String s = "{";
  s += "\"script_loaded\":" + String(g_host.script_load_ok ? "true" : "false");
  s += ",\"script_path\":\"/rhythm_spectrum/main.bottle\"";
  s += ",\"frame_ms\":" + String(g_host.program.frame_ms);
  s += ",\"bytecode_size\":" + String(g_host.program.bytecode_size);
  s += ",\"constant_count\":" + String(g_host.program.constant_count);
  s += ",\"array_count\":" + String(g_host.program.array_count);
  s += ",\"scalar_count\":" + String(g_host.program.scalar_count);
  s += ",\"style\":" + String(subpage_index);
  s += ",\"vm_status\":" + String(bottle_vm_status_json(&g_host.vm));
  s += ",\"has_setup\":" + String(g_host.program.has_setup ? "true" : "false");
  s += ",\"has_loop\":" + String(g_host.program.has_loop ? "true" : "false");
  s += ",\"has_unload\":" + String(g_host.program.has_unload ? "true" : "false");
  s += "}";
  return s;
}

String rhythm_module_configs_json(void) {
  if (g_host.script_load_ok) {
    String s = "[";
    for (uint8_t i = 0; i < g_host.program.config_count; i++) {
      const bottle_config_def_t& cfg = g_host.program.configs[i];
      if (i > 0) s += ",";
      s += "{";
      s += "\"key\":\"" + String(cfg.key) + "\"";
      s += ",\"label\":\"" + String(cfg.label) + "\"";
      s += ",\"type\":\"select\"";
      s += ",\"default\":" + String(cfg.default_value);
      if (cfg.options[0] != '\0') {
        s += ",\"options\":\"" + String(cfg.options) + "\"";
      }
      s += "}";
    }
    s += "]";
    return s;
  }
  return "[]";
}

#include "module_storage.h"

#include <FS.h>
#include <SPIFFS.h>

namespace {

const char* kRhythmManifestPath = "/rhythm_spectrum/manifest.json";
const char* kRhythmScriptPath = "/rhythm_spectrum/main.bottle";

const char kRhythmManifest[] =
R"json({
  "id": "rhythm.spectrum",
  "name": "Rhythm Spectrum",
  "version": "2.0.0",
  "author": "Bottle",
  "description": "Gravity-aware microphone spectrum visualizer.",
  "runtime": "bottle-vm@0.1",
  "entry": "main.bottle",
  "permissions": ["led.draw", "sensor.spectrum", "sensor.accel", "storage.kv"],
  "resources": {"memory_bytes": 512, "max_instructions_per_frame": 1800, "target_fps": 30},
  "configs": [
    {
      "key": "style",
      "label": "Style",
      "type": "select",
      "default": 0,
      "options": [
        {"label": "Green Peak", "value": 0},
        {"label": "Rainbow", "value": 1},
        {"label": "Split Rainbow", "value": 2},
        {"label": "Color Flow", "value": 3}
      ]
    }
  ]
})json";

const char kRhythmScript[] =
R"bottle(runtime bottle-vm@0.2
module rhythm.spectrum

state spectrum[WIDTH] = 0
state history[WIDTH] = 0
state peaks[WIDTH] = 0
state peak_y[WIDTH] = 0
state phase = 0.0
state style_id = 0
state last_decay_time = 0
state current_time = 0
state time_diff = 0

config style {
  type select
  label "Style"
  default 0
  options "Green Peak|Rainbow|Split Rainbow|Color Flow"
}

frame_ms 33

setup {
  clear(LEDS)
}

loop {
  read(ACCEL)
  style_id = style

  spectrum = read(SPECTRUM)
  for x in spectrum {
    spectrum[x] = spectrum[x] * 82 / 100
    spectrum[x] = spectrum[x] + max(spectrum[x] - history[x], 0) * 35 / 100
    spectrum[x] = (history[x] + spectrum[x] * 2) / 3
    spectrum[x] = clamp(spectrum[x] * max_height() / 80, 0, max_height())
    history[x] = spectrum[x]
    peaks[x] = max(peaks[x], spectrum[x])
  }

  current_time = millis()
  time_diff = current_time - last_decay_time

  if time_diff >= 90 {
    last_decay_time = current_time
    for x in spectrum {
      peaks[x] = max(peaks[x] - 1, spectrum[x])
    }
  }

  for x in spectrum {
    peak_y[x] = max(peaks[x] - 1, 0)
  }

  phase = phase + 0.1 every 33ms

  clear(LEDS)
  for x in spectrum {
    for y in range(0, max_height()) {
      if style_id == 0 {
        if y < spectrum[x] {
          LEDS[x,y] = hsv(y * 80 / max_height() + 70, 255, 120 + y * 120 / max_height())
        }
        if y == peak_y[x] {
          LEDS[x,y] = rgb(220, 220, 220)
        }
      } else if style_id == 1 {
        if y < spectrum[x] {
          LEDS[x,y] = hsv(x * 255 / WIDTH, 255, 180)
        }
        if y == peak_y[x] {
          LEDS[x,y] = rgb(220, 220, 220)
        }
      } else if style_id == 2 {
        if x % 2 == 0 {
          if y < spectrum[x] {
            LEDS[x,y] = hsv(x * 255 / WIDTH + 32, 255, 180)
          }
          if y == peak_y[x] {
            LEDS[x,y] = rgb(220, 220, 220)
          }
        } else {
          if y >= max_height() - spectrum[x] {
            LEDS[x,y] = hsv(x * 255 / WIDTH + 32, 255, 180)
          }
          if y == max_height() - peak_y[x] - 1 {
            LEDS[x,y] = rgb(220, 220, 220)
          }
        }
      } else {
        if y < spectrum[x] {
          LEDS[x,y] = hsv(y * 255 / max_height() + phase, 255, 180)
        }
        if y == peak_y[x] {
          LEDS[x,y] = hsv(peak_y[x] * 255 / max_height() + 160, 180, 180)
        }
      }
    }
  }
  show(LEDS)
}

unload {
  clear(LEDS)
  show(LEDS)
}
)bottle";

}  // namespace

bool module_storage_init(void) {
  Serial.println("[module_storage_init] Starting SPIFFS...");
  bool result = SPIFFS.begin(true, "/modules", 10, "modules");
  Serial.printf("[module_storage_init] SPIFFS.begin() returned: %d\n", result);
  return result;
}

String module_storage_read_text(const char* path) {
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) return String();
  String text = f.readString();
  f.close();
  return text;
}

bool module_storage_write_text(const char* path, const char* content) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) return false;
  size_t written = f.print(content);
  f.close();
  return written > 0;
}

bool module_storage_exists(const char* path) {
  return SPIFFS.exists(path);
}

void module_storage_ensure_defaults(void) {
  Serial.println("[ensure_defaults] Checking /rhythm_spectrum directory...");
  if (!module_storage_exists("/rhythm_spectrum")) {
    Serial.println("[ensure_defaults] Creating /rhythm_spectrum directory...");
    SPIFFS.mkdir("/rhythm_spectrum");
  }

  Serial.println("[ensure_defaults] Checking manifest file...");
  if (!module_storage_exists(kRhythmManifestPath)) {
    Serial.println("[ensure_defaults] Writing manifest file...");
    module_storage_write_text(kRhythmManifestPath, kRhythmManifest);
  }

  // Always write the latest script from code
  Serial.println("[ensure_defaults] Writing script file (forced update)...");
  module_storage_write_text(kRhythmScriptPath, kRhythmScript);
  Serial.println("[ensure_defaults] Script file written.");

  Serial.println("[ensure_defaults] Defaults ensured.");
}

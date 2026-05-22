// ---------------------------------------------------------------------------
// SleepMouse.ino — HijelHID_BLEMouse Example
// ---------------------------------------------------------------------------
// Copyright (c) 2026 Hijel. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, this software
// is provided "AS IS", WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. The author(s) accept no liability for any damages,
// loss, or consequences arising from the use or misuse of this software.
// See the License for the full terms governing permissions and limitations.
// ---------------------------------------------------------------------------
// Demonstrates the base layer move() call, TX power configuration,
// and automatic deep sleep after 60 seconds of no button activity.
//
// Press the BOOT button to send a random mouse movement.
// If the button is not pressed for 60 seconds, the device enters deep sleep.
// Press the BOOT button again to wake it back up.
//
// Hardware:
//   No wiring required — uses the onboard BOOT button (GPIO 0).
//   GPIO 0 is an RTC GPIO on the original ESP32 and most common variants.
//   Check your board's pinout if wake does not work — not all variants
//   support ext0 wakeup on GPIO 0.
// ---------------------------------------------------------------------------

#include <HijelHID_BLEMouse.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

// GPIO 0 is the BOOT button on most ESP32 dev boards.
// It is also an RTC GPIO, which is required for deep sleep wake-up.
// Change this to another RTC-capable GPIO if your board uses a different pin.
#define BOOT_BUTTON_PIN 0

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// How long to wait with no button press before entering deep sleep (milliseconds)
#define IDLE_SLEEP_TIMEOUT 60000

// ---------------------------------------------------------------------------
// Mouse object
// ---------------------------------------------------------------------------

HijelBLEMouse mouse("SleepMouse", "Hijel");

// ---------------------------------------------------------------------------
// Setup
//
// On wake from deep sleep the ESP32 runs setup() again from the beginning —
// exactly like a normal power-on. The BLE bond survives in flash storage
// so the host will reconnect automatically after mouse.begin().
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Set TX power before begin().
    // Lower power saves energy at close range.
    // Scale: 1 = -12 dBm (lowest), 8 = +9 dBm (highest, default).
    mouse.setTxPower(4);  // 0 dBm — a good middle ground for desktop use

    // Start BLE. If this is a wake from deep sleep, the bonded host
    // will reconnect automatically — no need to re-pair.
    mouse.begin();


    Serial.println("Started — press the BOOT button to move the mouse.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {

    // Do nothing until the host has fully paired.
    if (!mouse.isPaired()) {
        delay(500);
        return;
    }

    // Check if the button has been pressed (pin reads LOW when pressed).
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

        // Small debounce delay — ignore very brief contact
        delay(50);

        // Confirm the button is still held before acting
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

            // Pick a random dx and dy in the range -127 to +127.
            // move() sends a single-report delta — ±127 is the maximum per call.
            int dx = random(-127, 128);
            int dy = random(-127, 128);

            mouse.move(dx, dy);

            Serial.print("Button pressed — moved dx=");
            Serial.print(dx);
            Serial.print(" dy=");
            Serial.println(dy);

            // Wait for the button to be released before looping again
            while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                delay(10);
            }
        }
    }

    // Check how long it has been since the last mouse event.
    if (mouse.getIdleTime() >= IDLE_SLEEP_TIMEOUT) {
        goToSleep();
    }

    delay(10);
}

// ---------------------------------------------------------------------------
// goToSleep()
//
// Configures the BOOT button as a deep sleep wake source and enters
// deep sleep. The chip will restart from setup() when the button is pressed.
// ---------------------------------------------------------------------------

void goToSleep() {
    Serial.println("No activity for 60 seconds — entering deep sleep.");
    Serial.println("Press the BOOT button to wake up.");
    Serial.flush();

    // Give Serial time to finish sending before the chip powers down
    delay(100);

    // Configure GPIO 0 (BOOT button) as the wake source.
    // The button pulls the pin LOW when pressed, so we wake on level 0.
    // Note: only RTC GPIOs can be used as ext0 wake sources.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

    // Enter deep sleep. All RAM is lost — setup() will run on wake.
    // The BLE bond is stored in flash and survives deep sleep.
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// BasicMouse.ino — HijelHID_BLEMouse Example
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
//
// When the boot button is pressed, the mouse moves to a random position
// over 2 seconds and then performs a right click.
//
// Hardware:
//   No wiring required — uses the onboard BOOT button (GPIO 0).
//   Works on most standard ESP32 dev boards.
// ---------------------------------------------------------------------------

#include <HijelHID_BLEMouse.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

// GPIO 0 is the BOOT button on most ESP32 dev boards.
// Change this if your board uses a different pin.
#define BOOT_BUTTON_PIN 0

// ---------------------------------------------------------------------------
// Mouse object
// ---------------------------------------------------------------------------

// Create the mouse with a custom name and manufacturer string.
// All other settings stay at their defaults:
//   - 5 buttons
//   - No horizontal scroll
//   - 125 Hz report rate
HijelBLEMouse mouse("BasicMouse", "Hijel");

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    // Configure the boot button pin as an input.
    // INPUT_PULLUP means the pin reads HIGH normally and LOW when pressed.
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Report an initial battery level to the host.
    // This can be updated at any time with setBatteryLevel().
    mouse.setBatteryLevel(100);

    // Start BLE advertising — the device will appear in your
    // host's Bluetooth device list as "BasicMouse".
    mouse.begin();

    Serial.println("Advertising — waiting for host to connect...");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {

    // Always check isPaired() before sending any input.
    // The mouse will not respond until a host has fully connected and paired.
    if (!mouse.isPaired()) {
        delay(500);
        return;
    }

    // Wait for the boot button to be pressed (pin reads LOW when pressed).
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

        // Small debounce delay — ignore very brief contact
        delay(50);

        // Confirm the button is still held before acting
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {

            Serial.println("Button pressed — moving mouse...");

            // Pick a random dx and dy in the range -200 to +200.
            // random() returns a value from the first argument (inclusive)
            // up to but not including the second argument.
            int dx = random(-200, 201);
            int dy = random(-200, 201);

            Serial.print("Moving to dx=");
            Serial.print(dx);
            Serial.print(" dy=");
            Serial.println(dy);

            // moveTo() spreads the movement across multiple HID reports
            // over the requested duration. This feels smooth to the host OS.
            // The second argument is the duration in milliseconds.
            mouse.moveTo(dx, dy, 2000);

            // Right click immediately after the move completes.
            // click() handles the press, hold, and release automatically.
            mouse.click(MouseButton::Right);

            Serial.println("Done.");

            // Wait for the button to be released before looping again.
            while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                delay(10);
            }
        }
    }

    delay(10);
}

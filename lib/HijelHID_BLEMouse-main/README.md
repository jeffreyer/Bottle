# <sub><img width="40" height="40" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/mouse.svg"></sub> HijelHID_BLEMouse

A Bluetooth Low Energy (BLE) HID mouse library for ESP32, built on [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino).

Turn your ESP32 into a BLE HID Mouse. Great for building a physical mouse device with real buttons and a sensor, or just emulating one.

Supports 3, 5, or 7(5+horizontal scroll) buttons, vertical scroll, horizontal scroll, and xy movement. Non-blocking at the base layer — all core input methods are safe to call directly from an interrupt handler.

The library automatically reduces BLE radio activity after 5 seconds of inactivity to save power, and instantly resumes full speed the moment input is sent.

Works with iOS, Android, macOS, Windows, and Linux.

**Testing completed on:**

 <sub><img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/gray/apple.svg"></sub> iOS 26.3 - Fully Tested - Requires Assistive Touch to be On

 <sub><img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/green/android2.svg"></sub> Android 16 - Fully Tested

 <sub><img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/gray_dark/apple.svg"></sub> macOS Ventura 13.7.8 - Fully Tested
 
 <sub><img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/windows.svg"></sub> Windows 11 Pro 25H2 - Fully Tested

<sub> <img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/orange/ubuntu.svg"></sub> Ubuntu 22.04.5 LTS - Fully Tested

---

## Requirements

| Requirement | Version | Tested On |
|---|---|---|
| [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) | 3.x.x | 3.3.7 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | >= 2.3.8 | 2.3.9 |
| Arduino IDE | NA | 2.3.8 |

Install **[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)** via Arduino IDE: `Tools → Manage Libraries → search "NimBLE-Arduino"`

Install **[Espressif arduino-esp32](https://github.com/espressif/arduino-esp32)** core via Arduino IDE: `Tools → Boards → Boards Manager → search "arduino-esp32"`

An ESP32 board/module with BLE [ *All variants except ESP32-S2 and ESP32-P4* ]

---

## Installation

[![Latest Release](https://img.shields.io/github/release/HijelHub/HijelHID_BLEMouse.svg?style=plastic&label=Latest%20Release&color=blue)](https://github.com/HijelHub/HijelHID_BLEMouse/releases/latest)
[![Release Date](https://img.shields.io/github/release-date/HijelHub/HijelHID_BLEMouse.svg?style=plastic&label=Release%20Date&color=brightgreen)](https://github.com/HijelHub/HijelHID_BLEMouse/releases/latest)

Arduino Library Manager: `Sketch → Include Library → Manage Libraries` — search for **"HijelHID"**

--- OR ---

Manual Zip Install:
1. Download the [Latest ZIP](https://github.com/HijelHub/HijelHID_BLEMouse/releases/latest/download/HijelHID_BLEMouse.zip) [Direct Download Link]
2. In Arduino IDE: `Sketch → Include Library → Add .ZIP Library`
3. Select the downloaded zip

---

## Quick Start

```.ino
#include <HijelHID_BLEMouse.h>

HijelBLEMouse mouse;

void setup() {
    mouse.begin();
}

void loop() {
    if (mouse.isPaired()) {
        // Move the cursor right 100 units, then click left button
        mouse.moveTo(100, 0);
        mouse.click(MouseButton::Left);
        delay(2000);
    }
}
```

---

## Which API Layer should I use?

This library has two layers. Pick the one that fits your project:

| | Base Layer | Macro Layer |
|---|---|---|
| **Best for** | Physical mouse devices (buttons wired to GPIO, sensor in an interrupt) | Mouse emulators (scripting movement and clicks in `loop()`) |
| **ISR-safe** | ✅ Yes — safe to call from an interrupt handler | ❌ No — call from `loop()` or a FreeRTOS task only |
| **Blocking** | ✅ Never blocks | ⚠️ Some methods block until complete |
| **Movement** | Single-report delta (±127 max per call) | Multi-report movement spread over time |

You can mix both layers freely — for example, use `setButton()` from an interrupt for buttons, and `moveTo()` from `loop()` for smooth cursor movement.

---

## <a name="api-reference"></a><sub><img width="30" height="30" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/code.svg"></sub> API Reference

<details>
<summary> CLICK FOR API INDEX </summary>

**Setup**
* [Constructor](#constructor)
* [Lifecycle](#lifecycle)
* [Connection](#connection)

**Base Layer** *(ISR-safe — for physical mouse devices)*
* [move](#move)
* [addScroll / addScrollH](#addscroll--addscrollh)
* [setButton / setButtons](#setbutton--setbuttons)

**Macro Layer** *(for emulators and scripted input)*
* [moveTo](#moveto)
* [scroll / scrollH](#scroll--scrollh)
* [press / release / releaseAll()](#press--release--releaseall)
* [click / doubleClick](#click--doubleclick)

**Configuration**
* [Update Rate](#update-rate)
* [Battery Level](#battery-level)
* [Security / Pairing](#security--pairing)
* [Power Saving / Sleep](#power-saving--sleep)
* [Debug Logging](#debug-logging)

</details>

---

<br>

## <a name="constructor"></a>Constructor

Create a mouse object. All parameters are optional.

```.ino
// Default — shows as "BLE Mouse" when pairing, 5 buttons, no horizontal scroll
HijelBLEMouse mouse;

// Custom name and manufacturer
HijelBLEMouse mouse("My Mouse", "My Company");

// Full options — 3-button mouse with horizontal scroll (tilt wheel)
HijelBLEMouse mouse("My Mouse", "My Company", 100, 3, true);
```

| Parameter | Description | Default |
|---|---|---|
| `deviceName` | Name shown to the host when pairing | `"BLE Mouse"` |
| `manufacturer` | Manufacturer string reported to the host | `"Hijel"` |
| `batteryLevel` | Starting battery level (0–100) | `100` |
| `buttonCount` | Number of buttons — `3` or `5`. Use `5` for Back/Forward buttons | `5` |
| `horizontalScroll` | `true` enables horizontal scroll (AC Pan tilt-wheel axis) | `false` |

[[Top]](#api-reference)

---

<br>

## <a name="lifecycle"></a>Lifecycle

Call `begin()` once in `setup()`. The device will start advertising and be discoverable to any host.

```.ino
void setup() {
    mouse.begin();
}
```

[[Top]](#api-reference)

---

<br>

## <a name="connection"></a>Connection

Use `isConnected()` to check if a host has connected (before pairing is complete), and `isPaired()` to check if the link is fully authenticated and ready to receive input. **Input methods have no effect until `isPaired()` returns `true`.**

```.ino
void loop() {
    if (mouse.isPaired()) {
        mouse.moveTo(10, 0);
        delay(100);
    }
}
```

| Method | Returns |
|---|---|
| `isConnected()` | `true` if a host has connected (before or after pairing) |
| `isPaired()` | `true` if the link is fully authenticated — input is active |
| `getIdleTime()` | Milliseconds since the last input call |

[[Top]](#api-reference)

---

<br>

---

# Base Layer
*ISR-safe and non-blocking. Safe to call directly from an interrupt handler. Best suited for physical mouse devices where buttons and sensors trigger interrupts.*

---

<br>

## <a name="move"></a>move

Send a single relative XY movement. Values are clamped to ±127 — anything outside that range is discarded. This sends in the **next report** only; for large movements spread over multiple reports, use [`moveTo()`](#moveto) from the macro layer instead.

```.ino
// Move right 50, down 30
mouse.move(50, 30);

// Move left 20, up 10
mouse.move(-20, -10);
```

| Parameter | Description |
|---|---|
| `dx` | Horizontal delta. Positive = right, negative = left. Range: ±127 |
| `dy` | Vertical delta. Positive = down, negative = up. Range: ±127 |

> [!NOTE]
> **Using a sensor?** `move()` accepts `int16_t` so you can pass raw sensor values directly without pre-clamping. Values outside ±127 are clamped silently.

[[Top]](#api-reference)

---

<br>

## <a name="addscroll--addscrollh"></a>addScroll / addScrollH

Accumulate a scroll delta. The value is added to an internal accumulator and drained one chunk per report, so calling this rapidly from an interrupt will not lose scroll ticks.

```.ino
// Scroll down 3 ticks
mouse.addScroll(-3);

// Scroll up 1 tick
mouse.addScroll(1);

// Horizontal scroll right (only works if horizontalScroll = true in constructor)
mouse.addScrollH(2);
```

| Method | Parameter | Description |
|---|---|---|
| `addScroll(dz)` | `int8_t` | Vertical scroll. Positive = up, negative = down |
| `addScrollH(dz)` | `int8_t` | Horizontal scroll. Positive = right, negative = left. No effect if `horizontalScroll` is `false` |

[[Top]](#api-reference)

---

<br>

## <a name="setbutton--setbuttons"></a>setButton / setButtons

Set or clear individual buttons. Button state **persists** until you explicitly change it — press sets it, release clears it. This is the right approach for physical buttons wired to GPIO pins.

```.ino
// Press and hold left button
mouse.setButton(MouseButton::Left, true);

// Release left button
mouse.setButton(MouseButton::Left, false);

// Set multiple buttons at once using a bitmask
mouse.setButtons((uint8_t)MouseButton::Left | (uint8_t)MouseButton::Right);

// Release all buttons at once
mouse.setButtons(0x00);
```

| Button Constant | Description | Requires |
|---|---|---|
| `MouseButton::Left` | Left button | — |
| `MouseButton::Right` | Right button | — |
| `MouseButton::Middle` | Middle button / scroll wheel click | — |
| `MouseButton::Back` | Browser back / back thumb button | `buttonCount = 5` |
| `MouseButton::Forward` | Browser forward / forward thumb button | `buttonCount = 5` |

> [!NOTE]
> **Typical interrupt pattern:**
> ```ino
> void ARDUINO_ISR_ATTR onLeftButtonChange() {
>     // true if button is pressed (pin LOW), false if released (pin HIGH)
>     bool pressed = (digitalRead(BTN_LEFT_PIN) == LOW);
>     mouse.setButton(MouseButton::Left, pressed);
> }
>
> void setup() {
>     pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
>     attachInterrupt(BTN_LEFT_PIN, onLeftButtonChange, CHANGE);
>     mouse.begin();
> }
> ```

[[Top]](#api-reference)

---

<br>

---

# Macro Layer
*Not ISR-safe. Call from `loop()` or a FreeRTOS task. These methods handle timing and multi-report sequencing automatically. Best suited for mouse emulators and scripted input.*

---

<br>

## <a name="moveto"></a>moveTo

Move the cursor by a total of `dx`, `dy` spread across multiple HID reports. Blocks the calling task until the move is complete.

```.ino
// Move 500 units right as fast as possible
mouse.moveTo(500, 0);

// Move diagonally over 1 second
mouse.moveTo(300, 200, 1000);

// Move up 100 units as fast as possible
mouse.moveTo(0, -100);
```

| Parameter | Description | Default |
|---|---|---|
| `dx` | Total horizontal movement. Per Call Limit: ±32,767 | — |
| `dy` | Total vertical movement. Per Call Limit: ±32,767 | — |
| `durationMs` | Time to complete the move in milliseconds. `0` = as fast as possible | `0` |

**How it works:**

- `durationMs = 0` — The move is broken into as many 127-unit chunks as needed and sent as fast as the report rate allows. A 500-unit move at 125Hz takes about 32ms.
- `durationMs > 0` — The total movement is spread evenly over the requested time. If the distance is very small relative to the duration, the library uses "spacing mode" — injecting ±1 pixel at regular intervals to fill the time.

[[Top]](#api-reference)

---

<br>

## <a name="scroll--scrollh"></a>scroll / scrollH

Queue a large scroll value. The library drains it one chunk per report so the scroll feels smooth rather than jumping all at once.

```.ino
// Scroll down 20 ticks smoothly
mouse.scroll(-20);

// Scroll up 10 ticks
mouse.scroll(10);

// Horizontal scroll right 5 ticks (requires horizontalScroll = true)
mouse.scrollH(5);
```

| Method | Parameter | Description |
|---|---|---|
| `scroll(dz)` | `int16_t` | Vertical scroll. Positive = up, negative = down |
| `scrollH(dz)` | `int16_t` | Horizontal scroll. Positive = right, negative = left. No effect if `horizontalScroll` is `false` |

[[Top]](#api-reference)

---

<br>

## <a name="press--release--releaseall"></a>press / release / releaseAll

Hold a button down, release it, or release everything at once. You are responsible for adding delays between steps.

```.ino
// Hold left button down, wait 500ms, then release
mouse.press(MouseButton::Left);
delay(500);
mouse.release(MouseButton::Left);

// Release everything — useful as a safety call
mouse.releaseAll();
```
> [!TIP]
> For a simple click, use [`click()`](#click--doubleclick) instead — it handles press, delay, and release automatically.

[[Top]](#api-reference)

---

<br>

## <a name="click--doubleclick"></a>click / doubleClick

Press and release a button automatically, with timing handled for you. Both methods block for their duration.

```.ino
// Single left click
mouse.click(MouseButton::Left);

// Right click
mouse.click(MouseButton::Right);

// Left click with a longer hold time (50ms)
mouse.click(MouseButton::Left, 50);

// Double click with default timing
mouse.doubleClick(MouseButton::Left);

// Double click — 30ms hold, 80ms between clicks
mouse.doubleClick(MouseButton::Left, 30, 80);
```

| Parameter | Description | Default |
|---|---|---|
| `button` | The button to click | — |
| `releaseDelay_ms` | Time in ms between press and release. `0` = two report intervals | `0` |
| `betweenDelay_ms` | *(doubleClick only)* Time in ms between the two clicks. `0` = two report intervals | `0` |

> [!NOTE]
> **Why two report intervals?** A single BLE report interval (~8ms at 125Hz) is too short to guarantee the host sees the press and release as separate events. The two-interval default (~15ms) gives the host's BLE stack time to process both notifications. If clicks are missed on a particular platform, try increasing `releaseDelay_ms` to `20` or higher.

[[Top]](#api-reference)

---

<br>

---

# Configuration

---

<br>

## <a name="update-rate"></a>Update Rate

Controls how often the device sends reports to the host. The default of 125Hz (7.5ms interval) is the fastest BLE allows. Lower rates reduce CPU load and power draw.

```.ino
// Set before or after begin()
mouse.setUpdateRate(HIDRate::Hz125);  // 125 reports/sec — default
mouse.setUpdateRate(HIDRate::Hz100);  // 100 reports/sec
mouse.setUpdateRate(HIDRate::Hz50);   // 50 reports/sec
mouse.setUpdateRate(HIDRate::Hz25);   // 25 reports/sec
```

| Constant | Rate | BLE Interval |
|---|---|---|
| `HIDRate::Hz125` | 125 reports/sec | 7.5 ms (BLE minimum) — **Default** |
| `HIDRate::Hz100` | 100 reports/sec | 10 ms |
| `HIDRate::Hz50` | 50 reports/sec | 20 ms |
| `HIDRate::Hz25` | 25 reports/sec | 40 ms |

[[Top]](#api-reference)

---

<br>

## <a name="battery-level"></a>Battery Level

Report a battery percentage to the host at any time. The host displays this in its Bluetooth device list.

```.ino
mouse.setBatteryLevel(85);  // Report 85% battery
```

[[Top]](#api-reference)

---

<br>

## <a name="security--pairing"></a>Security / Pairing

By default the mouse pairs automatically with no passkey (Just Works). To require a numeric passkey challenge, call `setSecurityMode()` before `begin()`.

```.ino
void setup() {
    Serial.begin(115200);
    mouse.setSecurityMode(HIDSecurity::Passkey);  // Must be before begin()
    mouse.begin();
}
```

| Mode | Behaviour |
|---|---|
| `HIDSecurity::JustWorks` | Auto-pair with no passcode — encrypted but no verification (default) |
| `HIDSecurity::Passkey` | Numeric comparison — a 6-digit code must be confirmed on both sides |

When Passkey mode is active, register a callback to display the code to the user. The device confirms automatically — the user only needs to confirm on the host side.

```.ino
#include <HijelHID_BLEMouse.h>

HijelBLEMouse mouse;

// Called when a passkey needs to be shown to the user.
// Display it however suits your project — Serial, a screen, LEDs, etc.
// The user checks that this code matches what their host device is showing,
// then confirms on the host. No action is required in your sketch.
void onPassKey(uint32_t passkey) {
    Serial.print("Check that this code matches your host device: ");
    Serial.println(passkey);
}

void setup() {
    Serial.begin(115200);
    mouse.setSecurityMode(HIDSecurity::Passkey);
    mouse.setPasskeyCallback(onPassKey);
    mouse.begin();
}
```

### Bond Management

Use `isBonded()` to check whether bond data from a previous pairing is stored in flash. Use `clearBonds()` to erase it and force a fresh pair on the next connection.

```.ino
// Check if a bond already exists
if (mouse.isBonded()) {
    Serial.println("A bond is stored — host will reconnect automatically.");
} else {
    Serial.println("No bond stored — waiting to pair...");
}

// Erase all stored bonds (forces a full re-pair on next connection)
mouse.clearBonds();
```

> [!NOTE]
> **When to use `clearBonds()`:** If you change `buttonCount` or `horizontalScroll` and reflash, the host may refuse to reconnect because its cached HID descriptor no longer matches. Call `clearBonds()` on the device and remove it from the host's Bluetooth settings, then re-pair from scratch.

[[Top]](#api-reference)

---

<br>

## <a name="power-saving--sleep"></a>Power Saving / Sleep

### Idle Power Saving

The library handles BLE peripheral latency automatically. After 5 seconds with no input the connection interval relaxes to reduce radio wake-ups to roughly 1.6 times per second. The moment any input is sent the connection snaps back to the full report rate. No code is needed on your part.

### TX Power

Set the BLE radio transmit power, reduce to save energy when operating at close range. Valid levels are 1–8. Default is 8.

```.ino
mouse.setTxPower(1);  // -12 dBm — lowest power, shortest range
mouse.setTxPower(8);  //  +9 dBm — maximum power, longest range (default)
```

### Light Sleep

Call `beforeSleep()` immediately before entering light sleep and `afterWake()` immediately after waking. The BLE stack is shut down cleanly and restarted on wake — the bonded host will reconnect automatically.

```.ino
mouse.beforeSleep();
esp_light_sleep_start();
mouse.afterWake();  // restarts BLE — bonded host will auto-reconnect
```

### Deep Sleep

Deep sleep wipes RAM entirely, so no special library calls are needed before sleeping or after waking. Just call `begin()` in `setup()` as normal — the bonding data survives in NVS and the host will reconnect automatically.

```.ino
// --- Before sleeping ---
esp_deep_sleep_start();

// --- On wake, setup() runs as normal ---
void setup() {
    mouse.begin();  // bonded host will auto-reconnect
}
```

[[Top]](#api-reference)

---

<br>

## <a name="debug-logging"></a>Debug Logging

Enable Serial logging to help diagnose connection or pairing issues. Call before `begin()`.

```.ino
void setup() {
    Serial.begin(115200);
    mouse.setLogLevel(HIDLogLevel::Normal);  // connection and pairing events
    mouse.begin();
}
```

| Level | Output |
|---|---|
| `HIDLogLevel::Off` | No output (default) |
| `HIDLogLevel::Normal` | Connection, pairing, and sleep/wake events |
| `HIDLogLevel::Verbose` | All of the above, plus every HID report sent and every connection parameter update |

[[Top]](#api-reference)

---

---

## <sub><img width="30" height="30" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/card-list.svg"></sub> Platform Notes

| Platform | Pairing | Notes |
|---|---|---|
| **iOS** | Auto or passkey | Requires activation of AssistiveTouch  [Settings >> Accessibility >> Touch >> AssistiveTouch] |
| **Android** | Auto or passkey | Vendor quirks vary; Just Works works on most devices, Passkey on newer Android devices |
| **macOS** | Auto or passkey | Scroll wheel **may** need tuning due to OS quirks. |
| **Windows 10/11** | Auto or passkey | Caches the HID descriptor. If you change `buttonCount` or `horizontalScroll` and reflash, fully remove and re-pair the device in Bluetooth settings |
| **Linux (BlueZ)** | Auto or passkey | Works via generic `usbhid` / BlueZ HID profile |


---

## <sub><img width="30" height="30" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/emoji-angry-fill.svg"></sub> Troubleshooting

**ESP32 stuck in a reboot loop**
- Ensure you have NimBLE-Arduino 2.3.8 or later installed

**Device not appearing in Bluetooth scan**
- Make sure `mouse.begin()` has been called in `setup()`
- Check no previous bond is stored on the host — remove the device from Bluetooth settings and re-pair

**Cursor not moving / buttons not responding**
- Confirm `mouse.isPaired()` returns `true` before sending input — input methods have no effect until the link is fully authenticated
- If you are calling base layer methods from an interrupt, make sure the function is marked with `ARDUINO_ISR_ATTR`

**Click not registering on some platforms**
- Increase `releaseDelay_ms` in `click()` / `doubleClick()` to `20` or higher
- At slower report rates (25Hz / 50Hz) the default two-interval hold is already longer, so this is mainly a concern at 125Hz

**Windows shows a ghost / duplicate device after reflashing**
- Completely remove the device in Windows Bluetooth settings before flashing a new descriptor (any change to `buttonCount` or `horizontalScroll` changes the HID descriptor)

**Horizontal scroll has no effect**
- Confirm `horizontalScroll = true` was passed to the constructor — it defaults to `false`
- Use `scrollH()` (macro layer) or `addScrollH()` (base layer) — `scroll()` and `addScroll()` are vertical only
- Not all OS's / Apps accept or handle horizontal scroll in a manner you would expect.

---

## <sub><img width="30" height="30" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/trophy.svg"></sub> Acknowledgements

- The hundreds of contributors and maintainers of the [Espressif arduino-esp32](https://github.com/espressif/arduino-esp32) library
- My fellow Canadian Ryan Powell AKA [h2zero](https://github.com/h2zero) for his continued work on [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) and all the contributors to the project.
- My good friend Claude over at [Anthropic](https://www.anthropic.com/) for working tirelessly and for always telling me how smart, and right, and great I am. Even when I'm being an absolute moron.

---

## <sub><img width="30" height="30" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/blue/piggy-bank.svg"></sub> Support This Project

If you found this library useful, your support would mean a lot!

<sub><img width="20" height="20" src="https://raw.githubusercontent.com/HijelHub/GitStrap_SVG_Icons/b674246b8f46d8bc2c75f3cf5cf395a370b86ae2/icons/purple/stripe.svg"></sub>  [Securely Donate with Stripe](https://buy.stripe.com/fZu8wQdt01Oi5wWdKycMM01)

If you are intending to use this library in a commercial product, your support is **expected**.

<br>

### My other projects you might like:

[HijelHub/HijelHID_BLEKeyboard](https://github.com/HijelHub/HijelHID_BLEKeyboard)

A Bluetooth Low Energy HID Keyboard library for ESP32

[HijelHub/HijelHub_Dashboard](https://github.com/HijelHub/HijelHub_Dashboard)

A Github Analytics dashboard that automatically Gets, Stores, and Displays traffic data from multiple repos on a single page. Built completely on Githubs Free Tier.


##

Feel free to post your known working hardware/OS versions and combos in the Discussions section.

Please take the time to **properly** report any bugs you come across.

---

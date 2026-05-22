/**
 * HijelHID_BLEMouse.h
 *
 * Header and public API for the HijelHID_BLEMouse library.
 * Built on NimBLE-Arduino 2.3.8+
 *
 * Copyright (c) 2026 Hijel. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, this software
 * is provided "AS IS", WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. The author(s) accept no liability for any damages,
 * loss, or consequences arising from the use or misuse of this software.
 * See the License for the full terms governing permissions and limitations.
 */
#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
// Button identifiers
// ---------------------------------------------------------------------------

/**
  * Mouse button identifiers for use with `setButton()`, `press()`, `release()`, and `click()`.
  *
  * `MouseButton::Back` and `MouseButton::Forward` are only active in 5-button mode.
  */
enum class MouseButton : uint8_t {
    Left    = 0x01,
    Right   = 0x02,
    Middle  = 0x04,
    Back    = 0x08,   // 5-button mode only
    Forward = 0x10,   // 5-button mode only
};

// ---------------------------------------------------------------------------
// HID Report IDs
// Report ID used when horizontalScroll = true (single collection, 5-byte report).
// ---------------------------------------------------------------------------
#define MOUSE_REPORT_ID_MOUSE    0x01

// ---------------------------------------------------------------------------
// moveTo() FIFO buffer capacity in entries. Each entry is 2 bytes (int8_t x, y).
// So 128 entries = 256 bytes of RAM.
// Override before including this header: #define MOUSE_MOVE_BUFFER_SIZE 256
// ---------------------------------------------------------------------------
#ifndef MOUSE_MOVE_BUFFER_SIZE
#define MOUSE_MOVE_BUFFER_SIZE 128
#endif
#if MOUSE_MOVE_BUFFER_SIZE < 1
#error "MOUSE_MOVE_BUFFER_SIZE must be at least 1"
#endif

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/**
  * BLE report rate / connection interval.
  *
  * Controls how often the device sends HID reports to the host.
  * Values are BLE connection intervals in 1.25 ms units.
  *
  * `HIDRate::Hz25`  — 40 ms interval (25 reports/sec)
  * `HIDRate::Hz50`  — 20 ms interval (50 reports/sec)
  * `HIDRate::Hz100` — 10 ms interval (100 reports/sec)
  * `HIDRate::Hz125` — 7.5 ms interval (125 reports/sec) [Default]
  */
enum class HIDRate : uint16_t {
    Hz25  = 32,
    Hz50  = 16,
    Hz100 = 8,
    Hz125 = 6,
};

/**
  * BLE pairing security mode.
  *
  * `HIDSecurity::JustWorks` — Encrypted, no PIN required. [Default]
  * `HIDSecurity::Passkey`   — Numeric comparison. Requires `setPasskeyCallback()`.
  */
enum class HIDSecurity {
    JustWorks,
    Passkey,
};

/**
  * Debug log verbosity.
  *
  * `HIDLogLevel::Off`     — No output. [Default]
  * `HIDLogLevel::Normal`  — Connection, pairing, and sleep events.
  * `HIDLogLevel::Verbose` — All of the above, plus every report send and conn param update.
  */
enum class HIDLogLevel {
    Off     = 0,
    Normal  = 1,
    Verbose = 2,
};

// ---------------------------------------------------------------------------
// HijelBLEMouse
// ---------------------------------------------------------------------------

/**
	* Create a BLE HID Mouse instance.
	*
	* `deviceName`       — Name shown in Bluetooth device list. [Default: "BLE Mouse"]
	* `manufacturer`     — Manufacturer string in Device Information Service. [Default: "Hijel"]
	* `batteryLevel`     — Initial battery percent 0–100. [Default: 100]
	* `buttonCount`      — `3` or `5`. Back/Forward buttons only active in 5-button mode. [Default: 5]
	* `horizontalScroll` — `true` enables horizontal scroll (AC Pan) as a 5th report axis. [Default: false]
	*/
class HijelBLEMouse {

public:

    // -----------------------------------------------------------------------
    // Constructor / lifecycle
    // -----------------------------------------------------------------------

    /**
      * Create a BLE HID Mouse instance.
      *
      * `deviceName`       — Name shown in Bluetooth device list. [Default: "BLE Mouse"]
      * `manufacturer`     — Manufacturer string in Device Information Service. [Default: "Hijel"]
      * `batteryLevel`     — Initial battery percent 0–100. [Default: 100]
      * `buttonCount`      — `3` or `5`. Back/Forward buttons only active in 5-button mode. [Default: 5]
      * `horizontalScroll` — `true` enables horizontal scroll (AC Pan) as a 5th report axis. [Default: false]
      */
    HijelBLEMouse(
        const char* deviceName       = "BLE Mouse",
        const char* manufacturer     = "Hijel",
        uint8_t     batteryLevel     = 100,
        uint8_t     buttonCount      = 5,
        bool        horizontalScroll = false
    );

    /**
      * Initialise BLE stack and start advertising.
      *
      * Call once in `setup()`. Required before any input functions will have effect.
      */
    void begin();

    // -----------------------------------------------------------------------
    // Configuration (call before or after begin())
    // -----------------------------------------------------------------------

    /**
      * Set the HID report rate and BLE connection interval.
      *
      * `HIDRate::Hz25`  — 25 reports/sec
      * `HIDRate::Hz50`  — 50 reports/sec
      * `HIDRate::Hz100` — 100 reports/sec
      * `HIDRate::Hz125` — 125 reports/sec (BLE minimum interval) [Default]
      */
    void setUpdateRate(HIDRate rate);

    /**
      * Set BLE TX transmit power.
      *
      * Scale 1–8 maps to -12 dBm (lowest) through +9 dBm (highest).
      * Values outside 1–8 are clamped. [Default: 8]
      */
    void setTxPower(uint8_t level);

    /**
      * Update the BLE Battery Service percentage.
      *
      * Sends a BLE notification to the host. Range 0–100.
      */
    void setBatteryLevel(uint8_t percent);

    /**
      * Set BLE pairing security mode.
      *
      * `HIDSecurity::JustWorks` — No PIN, encrypted. [Default]
      * `HIDSecurity::Passkey`   — Numeric comparison. Set a callback with `setPasskeyCallback()`.
      */
    void setSecurityMode(HIDSecurity mode);

    /**
      * Register a callback for Passkey pairing (optional).
      *
      * Called with the 6-digit code when a host initiates Passkey pairing.
      * Display the code to the user — they must confirm on the host side.
      * If no callback is set, the code is printed directly to `Serial`.
      */
    void setPasskeyCallback(void (*callback)(uint32_t passkey));

    /**
      * Set Serial debug log verbosity.
      *
      * `HIDLogLevel::Off`     — No output. [Default]
      * `HIDLogLevel::Normal`  — Connection and pairing events.
      * `HIDLogLevel::Verbose` — All events plus every report and conn param update.
      */
    void setLogLevel(HIDLogLevel level);

    // -----------------------------------------------------------------------
    // Base layer — state setters
    // ISR-safe and non-blocking. Safe to call from interrupt handlers.
    // -----------------------------------------------------------------------

    /**
      * Send a relative XY movement delta. ISR-safe.
      *
      * Values are clamped to ±127 per report — excess is silently discarded.
      * Calling this more than once per report interval overwrites the previous value.
      * For movements where every delta matters, use `moveTo()`.
      */
    void move(int16_t dx, int16_t dy);

    /**
      * Accumulate a vertical scroll delta. ISR-safe.
      *
      * Negative = scroll down, positive = scroll up. Range: -127 to +127.
      */
    void addScroll(int8_t dz);

    /**
      * Accumulate a horizontal scroll (AC Pan) delta. ISR-safe.
      *
      * No effect if `horizontalScroll` was `false` in the constructor.
      * Negative = scroll left, positive = scroll right. Range: -127 to +127.
      */
    void addScrollH(int8_t dz);

    /**
      * Set or clear a single button. ISR-safe.
      *
      * `button`  — `MouseButton::Left`, `Right`, `Middle`, `Back`, or `Forward`.
      * `pressed` — `true` = pressed, `false` = released.
      */
    void setButton(MouseButton button, bool pressed);

    /**
      * Replace the entire button bitmask. ISR-safe.
      *
      * Sets all button states at once using OR'd `MouseButton` values cast to `uint8_t`.
      * Example: `setButtons((uint8_t)MouseButton::Left | (uint8_t)MouseButton::Right)`
      */
    void setButtons(uint8_t mask);

    // -----------------------------------------------------------------------
    // Base layer — status
    // -----------------------------------------------------------------------

    /**
      * Returns `true` if a host is currently connected (before or after pairing).
      */
    bool isConnected();

    /**
      * Returns `true` if a host is connected and fully authenticated/bonded.
      *
      * Input functions have no effect until this returns `true`.
      */
    bool isPaired();

    /**
      * Returns `true` if bond data is stored in flash from a previous pairing.
      *
      * Survives deep sleep and power cycles. Returns `false` after `clearBonds()`.
      */
    bool isBonded();

    /**
      * Erase all stored bond data from flash.
      *
      * The next connection will require a full re-pair. Useful during development
      * when the HID descriptor has changed, or to force a clean pair on a new host.
      * Takes effect immediately — does not require a restart.
      */
    void clearBonds();

    /**
      * Returns milliseconds since the last input call.
      *
      * Resets on any call to `move()`, `addScroll()`, `addScrollH()`, or `setButton()`.
      */
    uint32_t getIdleTime();

    // -----------------------------------------------------------------------
    // Sleep helpers
    // -----------------------------------------------------------------------

    /**
      * Prepare for deep sleep.
      *
      * Stops the send task and deinitialises NimBLE cleanly. Bonding data is preserved.
      * Call immediately before `esp_deep_sleep_start()`.
      */
    void beforeSleep();

    /**
      * Reinitialise after waking from deep sleep.
      *
      * Restarts the BLE stack and advertising. A bonded host will reconnect automatically.
      * Call once after wake in `setup()` or wherever wake is handled.
      */
    void afterWake();

    // -----------------------------------------------------------------------
    // Macro layer
    // Not ISR-safe. Call from loop() or a FreeRTOS task.
    // -----------------------------------------------------------------------

    /**
      * Move the cursor by dx, dy spread across multiple HID reports. Blocking.
      *
      * `durationMs = 0` (default): moves as fast as possible. Steps = number of
      * 127-unit chunks needed to cover the larger axis, remainder in final step.
      *
      * `durationMs > 0`: movement is spread over the requested duration. If dx/dy
      * is too small to produce a full pixel per report, spacing mode is used —
      * ±1 pixel injected every N reports to fill the requested duration.
      *
      * A new `moveTo()` call discards any in-progress move immediately.
      *
      * `dx`, `dy`      — Total relative movement in HID units. No range limit.
      * `durationMs`    — Time to complete the move in ms. `0` = as fast as possible. [Default: 0]
      */
    void moveTo(int16_t dx, int16_t dy, uint32_t durationMs = 0);

    /**
      * Queue a large vertical scroll, sent across multiple reports.
      *
      * Negative = scroll down, positive = scroll up.
      */
    void scroll(int16_t dz);

    /**
      * Queue a large horizontal scroll (AC Pan), sent across multiple reports.
      *
      * No effect if `horizontalScroll` was `false` in the constructor.
      */
    void scrollH(int16_t dz);

    /**
      * Hold a button down (persists until `release()` or `releaseAll()`).
      *
      * `button` — `MouseButton::Left`, `Right`, `Middle`, `Back`, or `Forward`.
      */
    void press(MouseButton button);

    /**
      * Release a held button.
      *
      * `button` — `MouseButton::Left`, `Right`, `Middle`, `Back`, or `Forward`.
      */
    void release(MouseButton button);

    /**
      * Release all buttons simultaneously.
      */
    void releaseAll();

    /**
      * Press then release a button (single click).
      *
      * `button`          — `MouseButton::Left`, `Right`, `Middle`, `Back`, or `Forward`.
      * `releaseDelay_ms` — Time between press and release in ms. `0` = two report intervals. [Default: 0]
      *
      * Two report intervals (~15 ms at 125 Hz) is the default to ensure the host receives
      * the press and release as distinct BLE notifications. Increase to 20+ ms if clicks
      * are missed on any platform.
      *
      * Blocks the calling task for the duration.
      */
    void click(MouseButton button, uint16_t releaseDelay_ms = 0);

    /**
      * Press and release a button twice (double click).
      *
      * `button`          — `MouseButton::Left`, `Right`, `Middle`, `Back`, or `Forward`.
      * `releaseDelay_ms` — Press-to-release delay for each click in ms. `0` = two report intervals. [Default: 0]
      * `betweenDelay_ms` — Release-to-press delay between the two clicks in ms. `0` = two report intervals. [Default: 0]
      *
      * Two report intervals (~15 ms at 125 Hz) is the default to ensure each press and release
      * arrives as a distinct BLE notification. Increase to 20+ ms if clicks are missed on any platform.
      *
      * Blocks the calling task for the full duration.
      */
    void doubleClick(MouseButton button, uint16_t releaseDelay_ms = 0, uint16_t betweenDelay_ms = 0);

private:

    // -----------------------------------------------------------------------
    // Internal connection state
    // -----------------------------------------------------------------------
    enum class ConnState { Disconnected, Connecting, Active, Idle };

    // -----------------------------------------------------------------------
    // BLE objects
    // -----------------------------------------------------------------------
    NimBLEServer*         _pServer         = nullptr;
    NimBLEHIDDevice*      _pHIDDevice      = nullptr;
    NimBLECharacteristic* _pInputReport    = nullptr;  // Mouse report (Report ID 1 or no-ID)

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------
    const char*  _deviceName;
    const char*  _manufacturer;
    uint8_t      _batteryLevel;
    uint8_t      _buttonCount;
    bool         _horizontalScroll;
    HIDRate      _updateRate   = HIDRate::Hz125;
    uint8_t      _txPowerLevel = 8;
    HIDSecurity  _securityMode = HIDSecurity::JustWorks;
    HIDLogLevel  _logLevel     = HIDLogLevel::Off;

    void (*_passkeyCallback)(uint32_t passkey) = nullptr;

    // -----------------------------------------------------------------------
    // Spinlock for ISR-safe base layer access
    // Must be a named instance — portENTER_CRITICAL_ISR requires a pointer.
    // -----------------------------------------------------------------------
    portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;

    // -----------------------------------------------------------------------
    // Input state (protected by _spinlock)
    // -----------------------------------------------------------------------
    struct InputState {
        uint8_t buttons = 0;
        int8_t  x       = 0;
        int8_t  y       = 0;
        int8_t  scrollV = 0;
        int8_t  scrollH = 0;
    };
    InputState _state;

    // -----------------------------------------------------------------------
    // Scroll remainder accumulators (protected by _spinlock)
    // Accumulated by scroll()/scrollH(), drained one int8 chunk per report.
    // -----------------------------------------------------------------------
    int32_t _remainingScroll  = 0;
    int32_t _remainingScrollH = 0;

    // -----------------------------------------------------------------------
    // moveTo() circular FIFO buffer (protected by _spinlock)
    // Each entry is one report's worth of XY movement.
    // Written by moveTo(), read and cleared by the send task.
    // -----------------------------------------------------------------------
    struct MoveEntry {
        int8_t x;
        int8_t y;
    };
    MoveEntry _moveBuffer[MOUSE_MOVE_BUFFER_SIZE];
    volatile uint16_t _moveBufHead  = 0;  // send task reads from head
    volatile uint16_t _moveBufTail  = 0;  // moveTo() writes to tail
    volatile uint16_t _moveBufCount = 0;  // entries currently in buffer

    // -----------------------------------------------------------------------
    // Connection state
    // -----------------------------------------------------------------------
    volatile bool  _connected  = false;
    volatile bool  _paired     = false;
    volatile uint16_t _connHandle = BLE_HS_CONN_HANDLE_NONE;
    ConnState      _connState  = ConnState::Disconnected;
    volatile uint32_t _lastInputTime = 0;

    static constexpr uint32_t _idleThreshold  = 5000;  // ms before entering idle latency
    static constexpr uint16_t _idleLatency    = 80;    // slave latency in idle state
    static constexpr uint16_t _idleInterval   = 6;     // 7.5 ms in 1.25 ms units
    static constexpr uint16_t _connectInterval = 32;   // 40 ms in 1.25 ms units
    static constexpr uint16_t _connTimeout    = 300;   // 3000 ms in 10 ms units

    // -----------------------------------------------------------------------
    // FreeRTOS send task
    // -----------------------------------------------------------------------
    TaskHandle_t      _sendTaskHandle = nullptr;
    volatile bool     _stopSendTask   = false;  // set true to request clean task exit
    static void       _sendTaskEntry(void* arg);
    void              _sendTask();

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    void    _log(HIDLogLevel level, const char* fmt, ...) const;
    void    _buildReportDescriptor(uint8_t* buf, uint16_t* len);
    void    _writePointerBlock(uint8_t*& p);  // shared button + X/Y/Wheel block
    void    _sendReport();
    void    _drainAccumulators();
    void    _updateConnParams(uint16_t minInterval, uint16_t maxInterval,
                              uint16_t latency, uint16_t timeout);
    void    _applyTxPower();
    uint16_t _reportIntervalMs() const;

    // Push one entry to the move buffer tail. Returns false if full.
    // Must be called with _spinlock held.
    bool    _moveBufPush(int8_t x, int8_t y);

    // Pop one entry from the move buffer head into _state.x / _state.y.
    // Must be called with _spinlock held.
    void    _moveBufPop();

    static int8_t _txLevelToDbm(uint8_t level);
    static int8_t _clampInt8(int16_t value);
    static int8_t _clampDrain(int32_t& acc);

    // -----------------------------------------------------------------------
    // NimBLE server callbacks
    // -----------------------------------------------------------------------
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        explicit ServerCallbacks(HijelBLEMouse* parent) : _parent(parent) {}
        void     onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
        void     onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
        void     onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
        uint32_t onPassKeyDisplay() override;
        void     onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override;
    private:
        HijelBLEMouse* _parent;
    };

    ServerCallbacks* _pServerCallbacks = nullptr;
};

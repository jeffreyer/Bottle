/**
 * HijelHID_BLEMouse.cpp
 *
 * Implementation of the HijelHID_BLEMouse library.
 * Built on NimBLE-Arduino 2.x.
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
#include "HijelHID_BLEMouse.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Arduino.h>

// ---------------------------------------------------------------------------
// TX power mapping: user scale 1–8 → dBm
// ---------------------------------------------------------------------------
static const int8_t kTxPowerTable[8] = { -12, -9, -6, -3, 0, 3, 6, 9 };

// ---------------------------------------------------------------------------
// Internal log helper
// ---------------------------------------------------------------------------

void HijelBLEMouse::_log(HIDLogLevel level, const char* fmt, ...) const {
    if (_logLevel < level) return;
    Serial.print("[BLEMouse] ");
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HijelBLEMouse::HijelBLEMouse(
    const char* deviceName,
    const char* manufacturer,
    uint8_t     batteryLevel,
    uint8_t     buttonCount,
    bool        horizontalScroll)
    : _deviceName(deviceName),
      _manufacturer(manufacturer),
      _batteryLevel(batteryLevel),
      _buttonCount(buttonCount == 5 ? 5 : 3),  // only 3 or 5 supported
      _horizontalScroll(horizontalScroll)
{
}

// ---------------------------------------------------------------------------
// begin() — initialise BLE stack, services, and start advertising
// ---------------------------------------------------------------------------

void HijelBLEMouse::begin() {

    NimBLEDevice::init(_deviceName);

    _applyTxPower();

    // Security
    if (_securityMode == HIDSecurity::Passkey) {
        NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND |
                                      BLE_SM_PAIR_AUTHREQ_MITM |
                                      BLE_SM_PAIR_AUTHREQ_SC);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    } else {
        NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND |
                                      BLE_SM_PAIR_AUTHREQ_SC);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    }

    _pServer = NimBLEDevice::createServer();
    _pServerCallbacks = new ServerCallbacks(this);
    _pServer->setCallbacks(_pServerCallbacks);
    _pServer->advertiseOnDisconnect(false); // advertising managed manually

    // HID device creates HID Service, Battery Service, and Device Information Service
    _pHIDDevice = new NimBLEHIDDevice(_pServer);
    _pHIDDevice->setManufacturer(_manufacturer);
    _pHIDDevice->setPnp(0x02, 0x05AC, 0x0000, 0x0001);
    _pHIDDevice->setHidInfo(0x00, 0x01); // country=0, normally connectable

    // Build and load report descriptor.
    // Worst-case size: hscroll 5-button = 69 bytes, standard 5-button = 52 bytes.
    // 128 bytes provides comfortable headroom for future additions.
    uint8_t  reportDesc[128];
    uint16_t reportDescLen = 0;
    _buildReportDescriptor(reportDesc, &reportDescLen);
    _pHIDDevice->setReportMap(reportDesc, reportDescLen);

    // Single input report characteristic for all modes.
    // hscroll mode uses one collection with Report ID 1;
    // standard mode uses one collection with no Report ID (pass 0).
    _pInputReport = _pHIDDevice->getInputReport(_horizontalScroll ? MOUSE_REPORT_ID_MOUSE : 0);
    _pHIDDevice->setBatteryLevel(_batteryLevel);
    _pHIDDevice->startServices();

    // Advertising
    // setName() puts the device name in the scan response packet so Hosts
    // can display it immediately during discovery.
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->setAppearance(0x03C2); // HID Mouse
    pAdv->setName(_deviceName);
    pAdv->addServiceUUID(_pHIDDevice->getHidService()->getUUID());
    pAdv->addServiceUUID(_pHIDDevice->getBatteryService()->getUUID());
    pAdv->enableScanResponse(true);
    pAdv->start();

    _log(HIDLogLevel::Normal, "Advertising started as \"%s\"", _deviceName);

    // Send task — not pinned to core (single and dual-core compatible)
    xTaskCreate(
        _sendTaskEntry,
        "BLEMouseSend",
        4096,
        this,
        5,
        &_sendTaskHandle
    );
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void HijelBLEMouse::setUpdateRate(HIDRate rate) {
    _updateRate = rate;
}

void HijelBLEMouse::setTxPower(uint8_t level) {
    if (level == 0) level = 1;
    if (level > 8)  level = 8;
    _txPowerLevel = level;
    if (NimBLEDevice::isInitialized()) {
        _applyTxPower();
    }
}

void HijelBLEMouse::setBatteryLevel(uint8_t percent) {
    _batteryLevel = percent;
    if (_pHIDDevice) {
        _pHIDDevice->setBatteryLevel(percent, true); // true = notify host
    }
}

void HijelBLEMouse::setSecurityMode(HIDSecurity mode) {
    _securityMode = mode;
}

void HijelBLEMouse::setPasskeyCallback(void (*callback)(uint32_t passkey)) {
    _passkeyCallback = callback;
}

void HijelBLEMouse::setLogLevel(HIDLogLevel level) {
    _logLevel = level;
}

// ---------------------------------------------------------------------------
// Base layer — state setters (ISR-safe)
// move(): int16_t input so sensors can pass raw values without pre-clamping.
// Values beyond ±127 are clamped silently.
// setButton() / press() / release() / click() / doubleClick() accept MouseButton enum values.
// ---------------------------------------------------------------------------
void HijelBLEMouse::move(int16_t dx, int16_t dy) {
    int8_t cx = _clampInt8(dx);
    int8_t cy = _clampInt8(dy);
    uint32_t now = millis();

    portENTER_CRITICAL_ISR(&_spinlock);
    _state.x = cx;
    _state.y = cy;
    _lastInputTime = now;
    portEXIT_CRITICAL_ISR(&_spinlock);
}

void HijelBLEMouse::addScroll(int8_t dz) {
    uint32_t now = millis();
    portENTER_CRITICAL_ISR(&_spinlock);
    _remainingScroll += dz;
    _lastInputTime    = now;
    portEXIT_CRITICAL_ISR(&_spinlock);
}

void HijelBLEMouse::addScrollH(int8_t dz) {
    if (!_horizontalScroll) return;
    uint32_t now = millis();
    portENTER_CRITICAL_ISR(&_spinlock);
    _remainingScrollH += dz;
    _lastInputTime     = now;
    portEXIT_CRITICAL_ISR(&_spinlock);
}

void HijelBLEMouse::setButton(MouseButton button, bool pressed) {
    uint32_t now = millis();
    portENTER_CRITICAL_ISR(&_spinlock);
    if (pressed) {
        _state.buttons |= (uint8_t)button;
    } else {
        _state.buttons &= ~(uint8_t)button;
    }
    _lastInputTime = now;
    portEXIT_CRITICAL_ISR(&_spinlock);
}

void HijelBLEMouse::setButtons(uint8_t mask) {
    uint32_t now = millis();
    portENTER_CRITICAL_ISR(&_spinlock);
    _state.buttons = mask;
    _lastInputTime = now;
    portEXIT_CRITICAL_ISR(&_spinlock);
}

// ---------------------------------------------------------------------------
// Base layer — status
// ---------------------------------------------------------------------------

bool HijelBLEMouse::isConnected() {
    return _connected;
}

bool HijelBLEMouse::isPaired() {
    return _paired;
}

bool HijelBLEMouse::isBonded() {
    return NimBLEDevice::getNumBonds() > 0;
}

void HijelBLEMouse::clearBonds() {
    // Iterate in reverse through all bonded peers and delete each one
    // individually via deleteBond(), which routes through ble_gap_unpair()
    // rather than ble_store_clear(). ble_gap_unpair() flushes the SM's
    // in-memory state for each peer — deleteAllBonds() does not, leaving
    // stale SM context that causes re-pairing to fail within the same boot
    // cycle on iOS and other hosts.
    // Iterating in reverse avoids index-shift issues when deleting forward.
    int numBonds = NimBLEDevice::getNumBonds();
    _log(HIDLogLevel::Normal, "Clearing %d bond(s)", numBonds);
    for (int i = numBonds - 1; i >= 0; i--) {
        NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
        NimBLEDevice::deleteBond(addr);
    }
}

uint32_t HijelBLEMouse::getIdleTime() {
    return millis() - _lastInputTime;
}

// ---------------------------------------------------------------------------
// Sleep helpers
// ---------------------------------------------------------------------------

void HijelBLEMouse::beforeSleep() {
    _log(HIDLogLevel::Normal, "beforeSleep: stopping send task");

    if (_sendTaskHandle) {
        // Signal the task to exit at its next loop iteration and wait for
        // self-delete. This avoids deleting the task while it may hold the
        // spinlock, which would leave the lock permanently acquired.
        _stopSendTask = true;
        while (_sendTaskHandle != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        _stopSendTask = false;
    }

    if (_connected && _connHandle != BLE_HS_CONN_HANDLE_NONE) {
        _pServer->updateConnParams(_connHandle,
            _idleInterval, _idleInterval, _idleLatency, _connTimeout);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    NimBLEDevice::deinit(false); // false = keep bonding data
    _log(HIDLogLevel::Normal, "beforeSleep: NimBLE stopped");
}

void HijelBLEMouse::afterWake() {
    _log(HIDLogLevel::Normal, "afterWake: reinitialising");
    _connected  = false;
    _paired     = false;
    _connHandle = BLE_HS_CONN_HANDLE_NONE;
    _connState  = HijelBLEMouse::ConnState::Disconnected;

    // Free the previous ServerCallbacks allocation. The NimBLE objects
    // (_pServer, _pHIDDevice, _pInputReport) are owned by the NimBLE stack
    // and are invalidated by deinit() — null them before begin() reallocates.
    delete _pServerCallbacks;
    _pServerCallbacks = nullptr;
    _pServer          = nullptr;
    _pHIDDevice       = nullptr;
    _pInputReport     = nullptr;

    begin();
}

// ---------------------------------------------------------------------------
// Macro layer
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// moveTo() — move dx/dy spread across multiple HID reports.
//
// durationMs = 0: moves as fast as possible — total steps = number of
// 127-unit chunks needed to cover the larger axis, remainder in final step.
// durationMs > 0: total steps = durationMs / reportInterval. If the
// per-step chunk is sub-pixel, spacing mode injects ±1 every N steps.
//
// Fills the FIFO buffer and blocks, refilling as the send task drains it,
// until all steps are queued and the buffer empties.
// A new moveTo() call discards any in-progress buffered steps immediately.
// ---------------------------------------------------------------------------
void HijelBLEMouse::moveTo(int16_t dx, int16_t dy, uint32_t durationMs) {
    if (dx == 0 && dy == 0) return;

    uint16_t intervalMs = _reportIntervalMs();

    int32_t absX = (dx > 0) ? dx : -dx;
    int32_t absY = (dy > 0) ? dy : -dy;

    // ---------------------------------------------------------------------------
    // Calculate total steps.
    // durationMs = 0: drive as fast as possible — steps = number of 127-unit
    // chunks needed to cover the larger axis, minimum 1.
    // durationMs > 0: divide the requested duration by the report interval.
    // ---------------------------------------------------------------------------
    uint32_t totalSteps;
    if (durationMs == 0) {
        uint32_t stepsX = (absX + 126) / 127;  // ceil(absX / 127)
        uint32_t stepsY = (absY + 126) / 127;  // ceil(absY / 127)
        totalSteps = (stepsX > stepsY) ? stepsX : stepsY;
        if (totalSteps == 0) totalSteps = 1;
    } else {
        totalSteps = durationMs / intervalMs;
        if (totalSteps == 0) totalSteps = 1;
    }

    // Discard any in-progress move
    portENTER_CRITICAL(&_spinlock);
    _moveBufHead  = 0;
    _moveBufTail  = 0;
    _moveBufCount = 0;
    portEXIT_CRITICAL(&_spinlock);

    // ---------------------------------------------------------------------------
    // Per-axis step calculation.
    // Normal mode: chunk >= 1 pixel per step, remainder pushed as final entry.
    // Spacing mode: 1 pixel every N steps (tiny move over long duration).
    // ---------------------------------------------------------------------------
    int8_t   stepX = 0,   stepY = 0;
    int8_t   remX  = 0,   remY  = 0;   // remainder for final step
    uint32_t spacingX = 1, spacingY = 1;

    if (dx != 0) {
        if (absX >= (int32_t)totalSteps) {
            // Normal mode
            int32_t chunk = absX / totalSteps;
            if (chunk > 127) chunk = 127;
            stepX = (dx > 0) ? (int8_t)chunk : -(int8_t)chunk;
            int32_t covered = chunk * (totalSteps - 1);
            int32_t left    = absX - covered;
            if (left > 127) left = 127;
            remX = (dx > 0) ? (int8_t)left : -(int8_t)left;
            spacingX = 1;
        } else {
            // Spacing mode: 1 pixel every N steps
            stepX    = (dx > 0) ? 1 : -1;
            remX     = stepX;  // remainder same as step in spacing mode
            spacingX = totalSteps / absX;
            if (spacingX == 0) spacingX = 1;
        }
    }

    if (dy != 0) {
        if (absY >= (int32_t)totalSteps) {
            int32_t chunk = absY / totalSteps;
            if (chunk > 127) chunk = 127;
            stepY = (dy > 0) ? (int8_t)chunk : -(int8_t)chunk;
            int32_t covered = chunk * (totalSteps - 1);
            int32_t left    = absY - covered;
            if (left > 127) left = 127;
            remY = (dy > 0) ? (int8_t)left : -(int8_t)left;
            spacingY = 1;
        } else {
            stepY    = (dy > 0) ? 1 : -1;
            remY     = stepY;
            spacingY = totalSteps / absY;
            if (spacingY == 0) spacingY = 1;
        }
    }

    // ---------------------------------------------------------------------------
    // Fill and refill the buffer until all steps are queued and drained.
    // The final step uses remX/remY to deliver the exact remainder.
    // ---------------------------------------------------------------------------
    uint32_t stepsQueued = 0;
    while (stepsQueued < totalSteps) {

        portENTER_CRITICAL(&_spinlock);
        while (stepsQueued < totalSteps && _moveBufCount < MOUSE_MOVE_BUFFER_SIZE) {
            uint32_t step = stepsQueued + 1; // 1-based for spacing modulo
            bool isFinal  = (stepsQueued == totalSteps - 1);

            int8_t ex, ey;
            if (isFinal) {
                ex = remX;
                ey = remY;
            } else {
                ex = ((spacingX == 1) || (step % spacingX == 0)) ? stepX : 0;
                ey = ((spacingY == 1) || (step % spacingY == 0)) ? stepY : 0;
            }
            _moveBufPush(ex, ey);
            stepsQueued++;
        }
        portEXIT_CRITICAL(&_spinlock);

        if (stepsQueued < totalSteps) {
            vTaskDelay(pdMS_TO_TICKS(intervalMs));
        }
    }

    // Wait for the send task to drain the remaining buffer entries
    bool bufferEmpty = false;
    while (!bufferEmpty) {
        portENTER_CRITICAL(&_spinlock);
        bufferEmpty = (_moveBufCount == 0);
        portEXIT_CRITICAL(&_spinlock);
        if (!bufferEmpty) {
            vTaskDelay(pdMS_TO_TICKS(intervalMs));
        }
    }
}

void HijelBLEMouse::scroll(int16_t dz) {
    uint32_t now = millis();
    portENTER_CRITICAL(&_spinlock);
    _remainingScroll += dz;
    _lastInputTime    = now;
    portEXIT_CRITICAL(&_spinlock);
}

void HijelBLEMouse::scrollH(int16_t dz) {
    if (!_horizontalScroll) return;
    uint32_t now = millis();
    portENTER_CRITICAL(&_spinlock);
    _remainingScrollH += dz;
    _lastInputTime     = now;
    portEXIT_CRITICAL(&_spinlock);
}

void HijelBLEMouse::press(MouseButton button) {
    setButton(button, true);
}

void HijelBLEMouse::release(MouseButton button) {
    setButton(button, false);
}

void HijelBLEMouse::releaseAll() {
    setButtons(0x00);
}

void HijelBLEMouse::click(MouseButton button, uint16_t releaseDelay_ms) {
    uint16_t d = (releaseDelay_ms == 0) ? 2 * _reportIntervalMs() : releaseDelay_ms;
    press(button);
    vTaskDelay(pdMS_TO_TICKS(d));
    release(button);
}

void HijelBLEMouse::doubleClick(MouseButton button,
                                 uint16_t releaseDelay_ms,
                                 uint16_t betweenDelay_ms) {
    uint16_t rel  = (releaseDelay_ms == 0) ? 2 * _reportIntervalMs() : releaseDelay_ms;
    uint16_t btwn = (betweenDelay_ms  == 0) ? 2 * _reportIntervalMs() : betweenDelay_ms;
    press(button);
    vTaskDelay(pdMS_TO_TICKS(rel));
    release(button);
    vTaskDelay(pdMS_TO_TICKS(btwn));
    press(button);
    vTaskDelay(pdMS_TO_TICKS(rel));
    release(button);
}

// ---------------------------------------------------------------------------
// Send task
// ---------------------------------------------------------------------------

void HijelBLEMouse::_sendTaskEntry(void* arg) {
    static_cast<HijelBLEMouse*>(arg)->_sendTask();
}

void HijelBLEMouse::_sendTask() {
    TickType_t lastWake = xTaskGetTickCount();

    while (!_stopSendTask) {
        uint16_t intervalMs = _reportIntervalMs();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(intervalMs));

        if (_stopSendTask) break;

        if (!_paired) continue;

        _drainAccumulators();
        _sendReport();

        // Peripheral latency / idle management
        uint32_t idle = getIdleTime();

        if (_connState == HijelBLEMouse::ConnState::Active && idle >= _idleThreshold) {
            _connState = HijelBLEMouse::ConnState::Idle;
            _updateConnParams(_idleInterval, _idleInterval, _idleLatency, _connTimeout);
            _log(HIDLogLevel::Verbose, "Idle: latency=%d", _idleLatency);

        } else if (_connState == HijelBLEMouse::ConnState::Idle && idle < _idleThreshold) {
            _connState = HijelBLEMouse::ConnState::Active;
            _updateConnParams((uint16_t)_updateRate, (uint16_t)_updateRate, 0, _connTimeout);
            _log(HIDLogLevel::Verbose, "Active: interval=%d", (uint16_t)_updateRate);
        }
    }

    _sendTaskHandle = nullptr;
    vTaskDelete(nullptr);  // self-delete — called only after loop exit, outside any critical section
}

// ---------------------------------------------------------------------------
// Internal: send one HID report
// ---------------------------------------------------------------------------

void HijelBLEMouse::_sendReport() {
    if (!_pInputReport) return;

    // Snapshot state under spinlock; reset delta axes (buttons persist)
    InputState snap;
    portENTER_CRITICAL(&_spinlock);
    snap = _state;
    _state.x       = 0;
    _state.y       = 0;
    _state.scrollV = 0;
    _state.scrollH = 0;
    portEXIT_CRITICAL(&_spinlock);

    if (_horizontalScroll) {
        // Single collection, Report ID baked into characteristic — payload is
        // 5 bytes: buttons, X, Y, scrollV, scrollH (AC Pan, Consumer page axis).
        uint8_t mouseData[5] = {
            snap.buttons,
            (uint8_t)snap.x,
            (uint8_t)snap.y,
            (uint8_t)snap.scrollV,
            (uint8_t)snap.scrollH
        };
        _pInputReport->notify(mouseData, sizeof(mouseData));
        _log(HIDLogLevel::Verbose, "report B:%02X X:%d Y:%d Vscr:%d Hscr:%d",
             snap.buttons, snap.x, snap.y, snap.scrollV, snap.scrollH);
    } else {
        uint8_t data[4] = {
            snap.buttons,
            (uint8_t)snap.x,
            (uint8_t)snap.y,
            (uint8_t)snap.scrollV
        };
        _pInputReport->notify(data, sizeof(data));
        _log(HIDLogLevel::Verbose, "report B:%02X X:%d Y:%d Vscr:%d",
             snap.buttons, snap.x, snap.y, snap.scrollV);
    }
}

// ---------------------------------------------------------------------------
// Internal: drain scroll accumulators one int8 chunk each into _state,
// and pop one moveTo() buffer entry into _state.x / _state.y if available.
// ---------------------------------------------------------------------------

void HijelBLEMouse::_drainAccumulators() {
    uint32_t now = millis();
    portENTER_CRITICAL(&_spinlock);

    // Pop one moveTo() buffer entry if available
    if (_moveBufCount > 0) {
        _moveBufPop();
        _lastInputTime = now;
    }

    if (_remainingScroll != 0) {
        _state.scrollV += _clampDrain(_remainingScroll);
        _lastInputTime  = now;
    }
    if (_horizontalScroll && _remainingScrollH != 0) {
        _state.scrollH += _clampDrain(_remainingScrollH);
        _lastInputTime  = now;
    }

    portEXIT_CRITICAL(&_spinlock);
}

// ---------------------------------------------------------------------------
// Internal: push one entry to the move buffer tail.
// Caller must hold _spinlock.
// ---------------------------------------------------------------------------

bool HijelBLEMouse::_moveBufPush(int8_t x, int8_t y) {
    if (_moveBufCount >= MOUSE_MOVE_BUFFER_SIZE) return false;
    _moveBuffer[_moveBufTail].x = x;
    _moveBuffer[_moveBufTail].y = y;
    _moveBufTail = (_moveBufTail + 1) % MOUSE_MOVE_BUFFER_SIZE;
    _moveBufCount++;
    return true;
}

// ---------------------------------------------------------------------------
// Internal: pop one entry from the move buffer head into _state.x / _state.y.
// Caller must hold _spinlock.
// ---------------------------------------------------------------------------

void HijelBLEMouse::_moveBufPop() {
    if (_moveBufCount == 0) return;
    _state.x = _moveBuffer[_moveBufHead].x;
    _state.y = _moveBuffer[_moveBufHead].y;
    _moveBufHead = (_moveBufHead + 1) % MOUSE_MOVE_BUFFER_SIZE;
    _moveBufCount--;
}

// ---------------------------------------------------------------------------
// Internal: clamp one int8 drain step from an int32 accumulator
// ---------------------------------------------------------------------------

int8_t HijelBLEMouse::_clampDrain(int32_t& acc) {
    // HID logical range is -127 to +127 per descriptor (not ±128)
    int8_t chunk;
    if      (acc >  127) { chunk =  127; }
    else if (acc < -127) { chunk = -127; }
    else                 { chunk = (int8_t)acc; }
    acc -= chunk;
    return chunk;
}

// ---------------------------------------------------------------------------
// Internal: clamp an int16 value to int8 range
// ---------------------------------------------------------------------------

int8_t HijelBLEMouse::_clampInt8(int16_t value) {
    if      (value >  127) return  127;
    else if (value < -127) return -127;
    else                   return (int8_t)value;
}

// ---------------------------------------------------------------------------
// Internal: update BLE connection parameters (call from send task only)
// ---------------------------------------------------------------------------

void HijelBLEMouse::_updateConnParams(uint16_t minInterval, uint16_t maxInterval,
                                       uint16_t latency, uint16_t timeout) {
    if (!_connected || _connHandle == BLE_HS_CONN_HANDLE_NONE) return;
    _pServer->updateConnParams(_connHandle, minInterval, maxInterval, latency, timeout);
}

// ---------------------------------------------------------------------------
// Internal: apply TX power to radio
// ---------------------------------------------------------------------------

void HijelBLEMouse::_applyTxPower() {
    NimBLEDevice::setPower(_txLevelToDbm(_txPowerLevel));
}

int8_t HijelBLEMouse::_txLevelToDbm(uint8_t level) {
    if (level == 0) level = 1;
    if (level > 8)  level = 8;
    return kTxPowerTable[level - 1];
}

// ---------------------------------------------------------------------------
// Internal: report interval in ms from current HIDRate setting
// BLE interval units are 1.25 ms: (units × 5) / 4
// ---------------------------------------------------------------------------

uint16_t HijelBLEMouse::_reportIntervalMs() const {
    return (uint16_t)(((uint32_t)_updateRate * 5) / 4);
}

// ---------------------------------------------------------------------------
// Internal: build HID report descriptor
// Standard mode (horizontalScroll=false): single collection, no Report ID, 4-byte report.
// Horizontal scroll mode (horizontalScroll=true): single collection, Report ID 1, 5-byte report.
//   Bytes: buttons, X, Y, scrollV, scrollH (AC Pan via Consumer page usage inside physical collection).
//   AC Pan is declared inside the mouse physical collection by switching usage page to Consumer
//   for that axis — this is identical to real tilt-wheel mice and is required for mouhid.sys,
//   IOHIDFamily (macOS), and Linux HID to recognise it as mouse horizontal scroll.
// ---------------------------------------------------------------------------

void HijelBLEMouse::_writePointerBlock(uint8_t*& p) {
    // Pointer physical collection — buttons, X, Y, and vertical wheel.
    // Shared between standard and horizontal scroll descriptor builds.
    *p++ = 0x09; *p++ = 0x01;  // Usage (Pointer)
    *p++ = 0xA1; *p++ = 0x00;  // Collection (Physical)

    // Buttons
    *p++ = 0x05; *p++ = 0x09;  // Usage Page (Button)
    *p++ = 0x19; *p++ = 0x01;  // Usage Minimum (Button 1)
    if (_buttonCount >= 5) {
        *p++ = 0x29; *p++ = 0x05; // Usage Maximum (Button 5)
        *p++ = 0x95; *p++ = 0x05; // Report Count (5)
    } else {
        *p++ = 0x29; *p++ = 0x03; // Usage Maximum (Button 3)
        *p++ = 0x95; *p++ = 0x03; // Report Count (3)
    }
    *p++ = 0x15; *p++ = 0x00;  // Logical Minimum (0)
    *p++ = 0x25; *p++ = 0x01;  // Logical Maximum (1)
    *p++ = 0x75; *p++ = 0x01;  // Report Size (1)
    *p++ = 0x81; *p++ = 0x02;  // Input (Data, Var, Abs)

    // Padding to byte boundary
    uint8_t btnPad = (_buttonCount >= 5) ? 3 : 5;
    *p++ = 0x95; *p++ = btnPad;
    *p++ = 0x75; *p++ = 0x01;
    *p++ = 0x81; *p++ = 0x03;  // Input (Const, Var, Abs)

    // X, Y, Vertical Wheel — Generic Desktop page
    *p++ = 0x05; *p++ = 0x01;  // Usage Page (Generic Desktop)
    *p++ = 0x09; *p++ = 0x30;  // Usage (X)
    *p++ = 0x09; *p++ = 0x31;  // Usage (Y)
    *p++ = 0x09; *p++ = 0x38;  // Usage (Wheel)
    *p++ = 0x15; *p++ = 0x81;  // Logical Minimum (-127)
    *p++ = 0x25; *p++ = 0x7F;  // Logical Maximum (127)
    *p++ = 0x75; *p++ = 0x08;  // Report Size (8)
    *p++ = 0x95; *p++ = 0x03;  // Report Count (3)
    *p++ = 0x81; *p++ = 0x06;  // Input (Data, Var, Rel)
}

void HijelBLEMouse::_buildReportDescriptor(uint8_t* buf, uint16_t* len) {
    uint8_t* p = buf;

    *p++ = 0x05; *p++ = 0x01;  // Usage Page (Generic Desktop)
    *p++ = 0x09; *p++ = 0x02;  // Usage (Mouse)
    *p++ = 0xA1; *p++ = 0x01;  // Collection (Application)

    if (_horizontalScroll) {
        // Single collection with Report ID.
        // AC Pan is declared inside the physical collection by switching to the
        // Consumer usage page for that axis only — identical to real tilt-wheel
        // mice. A separate Consumer Control collection is NOT recognised as
        // horizontal scroll by mouhid.sys / IOHIDFamily / Linux HID.
        *p++ = 0x85; *p++ = MOUSE_REPORT_ID_MOUSE;

        _writePointerBlock(p);

        // AC Pan (horizontal scroll) — Consumer page, same physical collection
        *p++ = 0x05; *p++ = 0x0C;           // Usage Page (Consumer)
        *p++ = 0x0A; *p++ = 0x38; *p++ = 0x02; // Usage (AC Pan, 0x0238)
        *p++ = 0x15; *p++ = 0x81;           // Logical Minimum (-127)
        *p++ = 0x25; *p++ = 0x7F;           // Logical Maximum (127)
        *p++ = 0x75; *p++ = 0x08;           // Report Size (8)
        *p++ = 0x95; *p++ = 0x01;           // Report Count (1)
        *p++ = 0x81; *p++ = 0x06;           // Input (Data, Var, Rel)

        *p++ = 0xC0;                         // End Collection (Physical)

    } else {
        // Standard mode — no Report ID, 4-byte report.
        _writePointerBlock(p);

        *p++ = 0xC0;                         // End Collection (Physical)
    }

    *p++ = 0xC0;                             // End Collection (Application)
    *len = (uint16_t)(p - buf);
}

// ---------------------------------------------------------------------------
// NimBLE server callbacks
// ---------------------------------------------------------------------------

void HijelBLEMouse::ServerCallbacks::onConnect(NimBLEServer* pServer,
                                                NimBLEConnInfo& connInfo) {
    _parent->_connected  = true;
    _parent->_connHandle = connInfo.getConnHandle();
    _parent->_connState  = HijelBLEMouse::ConnState::Connecting;

    // Use a relaxed connecting interval (40 ms) during pairing
    pServer->updateConnParams(
        connInfo.getConnHandle(),
        HijelBLEMouse::_connectInterval,
        HijelBLEMouse::_connectInterval,
        0,
        HijelBLEMouse::_connTimeout
    );

    _parent->_log(HIDLogLevel::Normal, "Connected handle=%d", connInfo.getConnHandle());
}

void HijelBLEMouse::ServerCallbacks::onDisconnect(NimBLEServer* pServer,
                                                    NimBLEConnInfo& connInfo,
                                                    int reason) {
    _parent->_connected  = false;
    _parent->_paired     = false;
    _parent->_connHandle = BLE_HS_CONN_HANDLE_NONE;
    _parent->_connState  = HijelBLEMouse::ConnState::Disconnected;

    _parent->_log(HIDLogLevel::Normal, "Disconnected reason=%d — restarting advertising", reason);
    NimBLEDevice::startAdvertising();
}

void HijelBLEMouse::ServerCallbacks::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
    if (!connInfo.isEncrypted()) {
        _parent->_log(HIDLogLevel::Normal, "Auth failed — disconnecting");
        NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        return;
    }

    _parent->_paired    = true;
    _parent->_connState = HijelBLEMouse::ConnState::Active;

    portENTER_CRITICAL(&_parent->_spinlock);
    _parent->_lastInputTime = millis(); // reset idle clock on connect
    portEXIT_CRITICAL(&_parent->_spinlock);

    // Escalate to the user-configured report rate now that we're paired
    NimBLEDevice::getServer()->updateConnParams(
        connInfo.getConnHandle(),
        (uint16_t)_parent->_updateRate,
        (uint16_t)_parent->_updateRate,
        0,
        HijelBLEMouse::_connTimeout
    );

    // Windows CCCD reconnect fix: send a zero-movement report immediately.
    // This re-triggers the host notification pipeline and prevents the dropped
    // first-packet issue seen on Windows BLE HID reconnects.
    // Report size must match the descriptor: 5 bytes (hscroll) or 4 bytes (standard).
    if (_parent->_pInputReport) {
        if (_parent->_horizontalScroll) {
            uint8_t data[5] = { 0, 0, 0, 0, 0 };
            _parent->_pInputReport->notify(data, sizeof(data));
        } else {
            uint8_t data[4] = { 0, 0, 0, 0 };
            _parent->_pInputReport->notify(data, sizeof(data));
        }
    }

    _parent->_log(HIDLogLevel::Normal, "Paired and authenticated handle=%d", connInfo.getConnHandle());
}

uint32_t HijelBLEMouse::ServerCallbacks::onPassKeyDisplay() {
    _parent->_log(HIDLogLevel::Normal, "onPassKeyDisplay called");
    return 0;
}

void HijelBLEMouse::ServerCallbacks::onConfirmPassKey(NimBLEConnInfo& connInfo,
                                                       uint32_t pin) {
    if (_parent->_passkeyCallback) {
        _parent->_passkeyCallback(pin);
    } else {
        // No callback registered — print directly so the code is always visible
        Serial.printf("[BLEMouse] Passkey: %06lu — confirm on host\n", (unsigned long)pin);
    }
    _parent->_log(HIDLogLevel::Normal, "Passkey: %06lu — confirm on host", (unsigned long)pin);

    // Device always confirms — user verifies the matching code on the host side
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

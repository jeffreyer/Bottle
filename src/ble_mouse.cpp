#include "ble_mouse.h"
#include <Arduino.h>
#include <HijelHID_BLEMouse.h>
#include <NimBLEDevice.h>

static HijelBLEMouse* bleMouse = nullptr;

bool ble_mouse_init() {
    if (bleMouse != nullptr) {
        return true;
    }

    // 如果 NimBLE 已被其他服务（如配置服务）初始化，先完全清理
    if (NimBLEDevice::isInitialized()) {
        Serial.println("BLE Mouse: NimBLE already initialized by another service, deinitializing...");
        NimBLEDevice::deinit(true);  // 完全清理
        delay(100);
    }

    bleMouse = new HijelBLEMouse("Bottle Mouse", "Bottle", 100, 5, false);
    bleMouse->begin();

    return true;
}

void ble_mouse_deinit() {
    if (bleMouse != nullptr) {
        Serial.println("BLE Mouse: Deinitializing...");

        // 先手动清理 BLE 栈，避免析构函数阻塞
        if (NimBLEDevice::isInitialized()) {
            Serial.println("BLE Mouse: Cleaning up NimBLE stack...");
            NimBLEDevice::deinit(true);
            delay(200);  // 等待清理完成
        }

        // 删除对象（此时析构函数应该检测到 BLE 已清理，跳过 deinit）
        delete bleMouse;
        bleMouse = nullptr;

        Serial.println("BLE Mouse: Deinitialized");
    }
}

void ble_mouse_move(int8_t x, int8_t y) {
    if (bleMouse == nullptr || !bleMouse->isConnected()) return;
    bleMouse->move(y, x);  // 交换 x 和 y 轴
}

void ble_mouse_click(uint8_t button) {
    if (bleMouse == nullptr || !bleMouse->isConnected()) return;
    bleMouse->click(static_cast<MouseButton>(button));
}

void ble_mouse_press(uint8_t button) {
    if (bleMouse == nullptr || !bleMouse->isConnected()) return;
    bleMouse->press(static_cast<MouseButton>(button));
}

void ble_mouse_release(uint8_t button) {
    if (bleMouse == nullptr || !bleMouse->isConnected()) return;
    bleMouse->release(static_cast<MouseButton>(button));
}

void ble_mouse_scroll(int8_t amount) {
    if (bleMouse == nullptr || !bleMouse->isConnected()) return;
    bleMouse->scroll(amount);
}

bool ble_mouse_is_connected() {
    return bleMouse != nullptr && bleMouse->isConnected();
}

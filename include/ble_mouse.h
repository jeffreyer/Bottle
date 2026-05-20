#ifndef BLE_MOUSE_H
#define BLE_MOUSE_H

#include <stdint.h>

bool ble_mouse_init();
void ble_mouse_deinit();
void ble_mouse_move(int8_t x, int8_t y);
void ble_mouse_click(uint8_t button);
void ble_mouse_press(uint8_t button);
void ble_mouse_release(uint8_t button);
void ble_mouse_scroll(int8_t amount);
bool ble_mouse_is_connected();

#endif

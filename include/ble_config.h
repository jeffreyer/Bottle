#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ble_config_init(void);
void ble_config_stop(void);
void ble_config_update(void);
void ble_config_publish_status(void);
bool ble_config_is_enabled(void);
bool ble_config_handle_tap(void);
void ble_config_render_mode(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_set_page(int32_t page, int32_t subpage);
void app_set_subpage(int32_t subpage);
int32_t app_get_page_count(void);

#ifdef __cplusplus
}
#endif

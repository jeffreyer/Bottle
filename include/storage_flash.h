#pragma once

#include "esp_vfs_fat.h"

void storage_init();

// 获取 wear leveling 句柄（用于卸载文件系统）
wl_handle_t storage_get_wl_handle();

// 重新挂载FAT文件系统（不重新注册分区）
bool storage_remount_fatfs();
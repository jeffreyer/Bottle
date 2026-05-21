#ifndef USB_MSC_H
#define USB_MSC_H

#include <Arduino.h>

// 初始化 USB MSC（大容量存储设备）
// 将 /extflash 分区暴露为 U盘
bool usb_msc_init();

// 停止 USB MSC
void usb_msc_deinit();

// 检查 USB MSC 是否已初始化
bool usb_msc_is_enabled();

#endif

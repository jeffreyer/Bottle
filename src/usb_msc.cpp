#include "usb_msc.h"
#include <USB.h>
#include <USBMSC.h>
#include <Preferences.h>
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "storage_flash.h"
#include "wear_levelling.h"

// USB MSC 实例
USBMSC msc;

// 标志位
static bool msc_enabled = false;
static uint32_t last_activity_time = 0;  // 最后一次读写活动时间
static bool pending_eject = false;  // 待处理的弹出请求

// Wear leveling 句柄
static wl_handle_t wl_handle = WL_INVALID_HANDLE;

// 扇区数量和大小
static uint32_t sector_count = 0;
static uint32_t sector_size = 512;  // USB MSC 扇区大小

// FAT 文件系统在 Flash 中的偏移量（跳过坏块）
static const size_t FAT_OFFSET = 4096 * 100;  // 409600 字节

// 读取回调 - 通过 wear leveling 层读取
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (wl_handle == WL_INVALID_HANDLE) {
    log_e("onRead: wl_handle is invalid!");
    return -1;
  }

  // 更新活动时间
  last_activity_time = millis();

  log_i("onRead: lba=%u, offset=%u, bufsize=%u", lba, offset, bufsize);

  // 计算虚拟地址（使用实际的扇区大小）
  size_t addr = lba * sector_size + offset;

  // 通过 wear leveling 层读取
  esp_err_t err = wl_read(wl_handle, addr, buffer, bufsize);
  if (err != ESP_OK) {
    log_e("Read error at lba=%u offset=%u size=%u: %d", lba, offset, bufsize, err);
    return -1;
  }

  log_i("onRead: success, read %u bytes from addr 0x%x", bufsize, addr);
  return bufsize;
}

// 写入回调 - 通过 wear leveling 层写入
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (wl_handle == WL_INVALID_HANDLE) {
    return -1;
  }

  // 更新活动时间
  last_activity_time = millis();

  // 计算虚拟地址（使用实际的扇区大小）
  size_t addr = lba * sector_size + offset;

  // 如果是完整扇区写入（offset=0, bufsize=sector_size），必须先擦除
  if (offset == 0 && bufsize == sector_size) {
    esp_err_t err = wl_erase_range(wl_handle, addr, sector_size);
    if (err != ESP_OK) {
      Serial.printf("[USB MSC] Erase error at lba=%u: %d\n", lba, err);
      return -1;
    }
  }

  // 通过 wear leveling 层写入
  esp_err_t err = wl_write(wl_handle, addr, buffer, bufsize);
  if (err != ESP_OK) {
    Serial.printf("[USB MSC] Write error at lba=%u: %d\n", lba, err);
    return -1;
  }

  return bufsize;
}

// 启动/停止回调
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  if (load_eject && !start) {
    // 弹出请求：标记待处理，在主循环中处理
    log_e("[USB MSC] Eject request received");
    pending_eject = true;
    return true;  // 返回 true 表示接受弹出请求
  }

  return true;
}

bool usb_msc_init() {
  if (msc_enabled) {
    log_e("USB MSC already enabled");
    return true;
  }

  log_e("[USB MSC] Initializing...");

  // 查找外部 Flash 存储分区（由 storage_flash.cpp 注册）
  const esp_partition_t* storage_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
  if (!storage_partition) {
    log_e("Storage partition 'storage' not found");
    return false;
  }

  // 卸载文件系统
  wl_handle_t old_handle = storage_get_wl_handle();
  if (old_handle != WL_INVALID_HANDLE) {
    log_e("[USB MSC] Unmounting /extflash...");
    esp_vfs_fat_spiflash_unmount_rw_wl("/extflash", old_handle);
    wl_unmount(old_handle);
  }

  // 重新挂载 wear leveling（不挂载文件系统）
  log_e("[USB MSC] Mounting wear leveling...");
  esp_err_t err = wl_mount(storage_partition, &wl_handle);
  if (err != ESP_OK) {
    log_e("Failed to mount wear leveling: %d", err);
    return false;
  }

  // 读取 FAT 引导扇区以获取实际的文件系统大小
  uint8_t boot_sector[512];
  err = wl_read(wl_handle, 0, boot_sector, 512);
  if (err != ESP_OK) {
    log_e("Failed to read boot sector: %d", err);
    return false;
  }

  // 检查引导扇区签名
  if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
    log_e("Invalid boot sector signature! [510]=0x%02X [511]=0x%02X", boot_sector[510], boot_sector[511]);
    return false;
  }

  // 读取 FAT 文件系统的总扇区数（偏移 19-20，小端序）
  uint16_t total_sectors_16 = boot_sector[19] | (boot_sector[20] << 8);
  uint32_t total_sectors_32 = boot_sector[32] | (boot_sector[33] << 8) |
                               (boot_sector[34] << 16) | (boot_sector[35] << 24);

  // 读取每扇区字节数（偏移 11-12，小端序）
  uint16_t bytes_per_sector = boot_sector[11] | (boot_sector[12] << 8);

  // FAT12/FAT16 使用偏移 19-20，FAT32 使用偏移 32-35
  uint32_t fat_total_sectors;
  if (total_sectors_16 != 0) {
    fat_total_sectors = total_sectors_16;
  } else {
    fat_total_sectors = total_sectors_32;
  }

  // 使用 FAT 文件系统的扇区大小
  sector_size = bytes_per_sector;
  sector_count = fat_total_sectors;

  log_e("[USB MSC] Sector size: %u, count: %u", sector_size, sector_count);

  // 配置 USB MSC
  msc.vendorID("Bottle");
  msc.productID("ExtFlash");
  msc.productRevision("1.0");

  // 注册回调函数
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);

  // 设置媒体状态
  msc.mediaPresent(true);

  // 初始化 MSC（使用 FAT 文件系统的扇区大小）
  log_e("[USB MSC] Calling msc.begin(%u, %u)...", sector_count, sector_size);
  if (!msc.begin(sector_count, sector_size)) {
    log_e("Failed to start MSC!");
    return false;
  }

  msc_enabled = true;
  last_activity_time = millis();  // 初始化活动时间
  log_e("[USB MSC] Enabled successfully");
  return true;
}

void usb_msc_deinit() {
  if (!msc_enabled) {
    log_e("[USB MSC] Already disabled");
    return;
  }

  log_e("[USB MSC] Force disabling...");

  // 设置媒体不存在
  msc.mediaPresent(false);

  // 不调用 msc.end()，因为它会导致无法重新初始化
  // 只卸载 wear leveling 和重新挂载文件系统

  // 卸载 wear leveling
  if (wl_handle != WL_INVALID_HANDLE) {
    log_e("[USB MSC] Unmounting wear leveling...");
    wl_unmount(wl_handle);
    wl_handle = WL_INVALID_HANDLE;
  }

  msc_enabled = false;
  sector_count = 0;

  // 重新挂载 FAT 文件系统供设备使用
  log_e("[USB MSC] Remounting /extflash...");

  // 调用 storage_remount_fatfs 而不是 storage_init
  extern bool storage_remount_fatfs();
  if (storage_remount_fatfs()) {
    log_e("[USB MSC] Storage remounted successfully");
  } else {
    log_e("[USB MSC] Failed to remount storage");
  }

  log_e("[USB MSC] Disabled, /extflash should be available for device use");
}

bool usb_msc_is_enabled() {
  return msc_enabled;
}

bool usb_msc_check_eject() {
  if (pending_eject) {
    log_e("[USB MSC] Processing eject request - saving state and restarting...");

    Preferences prefs;
    if (prefs.begin("usb_msc", false)) {
      prefs.putBool("ejected", true);
      prefs.end();
    }

    delay(100);
    ESP.restart();
    return true;
  }
  return false;
}

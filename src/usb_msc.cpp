#include "usb_msc.h"
#include <USB.h>
#include <USBMSC.h>
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "storage_flash.h"
#include "wear_levelling.h"

// USB MSC 实例
USBMSC msc;

// 标志位
static bool msc_enabled = false;

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
    Serial.println("[USB MSC] Read: Invalid handle");
    return -1;
  }

  // 计算虚拟地址（使用实际的扇区大小）
  size_t addr = lba * sector_size + offset;

  // 通过 wear leveling 层读取
  esp_err_t err = wl_read(wl_handle, addr, buffer, bufsize);
  if (err != ESP_OK) {
    Serial.printf("[USB MSC] Read error at lba=%u offset=%u size=%u: %d\n", lba, offset, bufsize, err);
    return -1;
  }

  return bufsize;
}

// 写入回调 - 通过 wear leveling 层写入
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (wl_handle == WL_INVALID_HANDLE) {
    return -1;
  }

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
  Serial.printf("[USB MSC] Start/Stop: power=%d, start=%d, eject=%d\n",
                power_condition, start, load_eject);

  if (load_eject && !start) {
    Serial.println("[USB MSC] Eject requested - switching to serial mode...");

    // 等待所有操作完成
    delay(100);

    // 结束MSC，这会断开USB大容量存储设备
    msc.end();

    // 卸载 wear leveling
    if (wl_handle != WL_INVALID_HANDLE) {
      wl_unmount(wl_handle);
      wl_handle = WL_INVALID_HANDLE;
    }

    // 重新挂载 FAT 文件系统（不重新注册分区）
    Serial.println("[USB MSC] Remounting FAT filesystem...");
    extern bool storage_remount_fatfs();
    if (!storage_remount_fatfs()) {
      Serial.println("[USB MSC] Failed to remount FAT filesystem");
    }

    // 保存配置：禁用USB MSC
    extern void save_config(String key, int value);
    save_config("usb_msc", 0);

    delay(500);

    // 重新初始化USB为串口模式
    USB.begin();
    Serial.begin(115200);
    delay(500);

    Serial.println("[USB MSC] Switched to serial mode. Use 'usb=1' to re-enable USB MSC.");
  }

  return true;
}

bool usb_msc_init() {
  if (msc_enabled) {
    Serial.println("[USB MSC] Already enabled");
    return true;
  }

  Serial.println("[USB MSC] Initializing...");

  // 查找外部 Flash 存储分区（由 storage_flash.cpp 注册）
  const esp_partition_t* storage_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
  if (!storage_partition) {
    Serial.println("[USB MSC] Storage partition 'storage' not found");
    Serial.println("[USB MSC] Make sure storage_init() has been called");
    return false;
  }

  Serial.printf("[USB MSC] Found storage partition: size=%u bytes (%.2f MB)\n",
                storage_partition->size, storage_partition->size / 1024.0 / 1024.0);

  // 卸载文件系统
  Serial.println("[USB MSC] Unmounting /extflash...");
  wl_handle_t old_handle = storage_get_wl_handle();
  if (old_handle != WL_INVALID_HANDLE) {
    esp_vfs_fat_spiflash_unmount_rw_wl("/extflash", old_handle);
    wl_unmount(old_handle);
  }

  // 重新挂载 wear leveling（不挂载文件系统）
  Serial.println("[USB MSC] Mounting wear leveling...");
  esp_err_t err = wl_mount(storage_partition, &wl_handle);
  if (err != ESP_OK) {
    Serial.printf("[USB MSC] Failed to mount wear leveling: %d\n", err);
    return false;
  }

  // 获取 wear leveling 层的大小
  size_t wl_storage_size = wl_size(wl_handle);

  Serial.printf("[USB MSC] Wear leveling size: %u bytes\n", wl_storage_size);

  // 读取 FAT 引导扇区以获取实际的文件系统大小
  uint8_t boot_sector[512];
  err = wl_read(wl_handle, 0, boot_sector, 512);
  if (err != ESP_OK) {
    Serial.printf("[USB MSC] Failed to read boot sector: %d\n", err);
    return false;
  }

  // 检查引导扇区签名
  if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
    Serial.println("[USB MSC] Invalid boot sector signature!");
    return false;
  }

  // 读取 FAT 文件系统的总扇区数（偏移 19-20，小端序）
  uint16_t total_sectors_16 = boot_sector[19] | (boot_sector[20] << 8);
  uint32_t total_sectors_32 = boot_sector[32] | (boot_sector[33] << 8) |
                               (boot_sector[34] << 16) | (boot_sector[35] << 24);

  // 读取每扇区字节数（偏移 11-12，小端序）
  uint16_t bytes_per_sector = boot_sector[11] | (boot_sector[12] << 8);
  Serial.printf("[USB MSC] FAT bytes per sector: %u\n", bytes_per_sector);

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

  Serial.printf("[USB MSC] FAT total sectors: %u\n", fat_total_sectors);
  Serial.printf("[USB MSC] USB MSC sector size: %u bytes\n", sector_size);
  Serial.printf("[USB MSC] USB MSC sector count: %u\n", sector_count);
  Serial.printf("[USB MSC] Total size: %.2f MB\n", (sector_count * sector_size) / 1024.0 / 1024.0);

  // 配置 USB MSC
  msc.vendorID("Bottle");
  msc.productID("ExtFlash");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);

  // 先初始化 USB 栈
  USB.begin();

  // 然后开始 MSC（使用 FAT 文件系统的扇区大小）
  if (!msc.begin(sector_count, sector_size)) {
    Serial.println("[USB MSC] Failed to start MSC");
    return false;
  }

  msc_enabled = true;
  Serial.println("[USB MSC] ========================================");
  Serial.println("[USB MSC] USB Mass Storage enabled successfully!");
  Serial.println("[USB MSC] Device will appear as USB drive on PC");
  Serial.println("[USB MSC] ========================================");
  Serial.println("[USB MSC] WARNING: /extflash is now unmounted!");
  Serial.println("[USB MSC] Do NOT access files while USB connected!");
  Serial.println("[USB MSC] ========================================");

  return true;
}

void usb_msc_deinit() {
  if (!msc_enabled) {
    return;
  }

  Serial.println("[USB MSC] Disabling...");

  msc.end();
  msc_enabled = false;
  wl_handle = WL_INVALID_HANDLE;
  sector_count = 0;

  // 重新挂载 FAT 文件系统供设备使用
  Serial.println("[USB MSC] Remounting /extflash...");

  // 需要重新调用 storage_init() 来重新挂载
  // 或者直接调用挂载函数
  extern void storage_init();
  storage_init();

  Serial.println("[USB MSC] Disabled, /extflash remounted for device use");
}

bool usb_msc_is_enabled() {
  return msc_enabled;
}

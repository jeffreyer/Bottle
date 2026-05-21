
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "storage_flash.h"

#include "esp_flash.h"
#include "esp_flash_spi_init.h"
#include "esp_partition.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "soc/spi_pins.h"

static const char *TAG = "storage_flash";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

// Mount path for the partition
const char *base_path = "/extflash";

static esp_flash_t* storage_init_ext_flash(void);
static const esp_partition_t* storage_add_partition(esp_flash_t* ext_flash, const char* partition_label);
static bool storage_mount_fatfs(const char* partition_label);

void storage_init()
{
    // Set up SPI bus and initialize the external SPI Flash chip
    esp_flash_t* flash = storage_init_ext_flash();
    if (flash == NULL) {
        return;
    }

    // Add the entire external flash chip as a partition
    const char *partition_label = "storage";
    storage_add_partition(flash, partition_label);

    // Initialize FAT FS in the partition
    if (!storage_mount_fatfs(partition_label)) {
        return;
    }

    // Print FAT FS size information
    uint64_t bytes_total, bytes_free;
    esp_vfs_fat_info(base_path, &bytes_total, &bytes_free);
    ESP_LOGE(TAG, "FAT FS: %" PRIu64 " kB total, %" PRIu64 " kB free", bytes_total / 1024, bytes_free / 1024);

}

static esp_flash_t* storage_init_ext_flash(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = 14,
        .miso_io_num = 13,
        .sclk_io_num = 9,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    const esp_flash_spi_device_config_t device_config = {
        .host_id = SPI2_HOST,
        .cs_io_num = 8,
        .io_mode = SPI_FLASH_DIO,
        .cs_id = 0,
        .freq_mhz = 40,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));

    // Add device to the SPI bus
    esp_flash_t* ext_flash;
    ESP_ERROR_CHECK(spi_bus_add_flash_device(&ext_flash, &device_config));

    // Probe the Flash chip and initialize it
    esp_err_t err = esp_flash_init(ext_flash);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize external Flash: %s (0x%x)", esp_err_to_name(err), err);
        return NULL;
    }

    return ext_flash;
}

static const esp_partition_t* storage_add_partition(esp_flash_t* ext_flash, const char* partition_label)
{
    const esp_partition_t* fat_partition;
    const size_t offset = 4096*100;
    ESP_ERROR_CHECK(esp_partition_register_external(ext_flash, offset, ext_flash->size - offset, partition_label, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, &fat_partition));

    return fat_partition;
}

static bool storage_mount_fatfs(const char* partition_label)
{
    const esp_vfs_fat_mount_config_t mount_config = {
            .format_if_mount_failed = true,
            .max_files = 4,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
            .use_one_fat = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(base_path, partition_label, &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

wl_handle_t storage_get_wl_handle()
{
    return s_wl_handle;
}

bool storage_remount_fatfs()
{
    // 如果已经挂载，先卸载
    if (s_wl_handle != WL_INVALID_HANDLE) {
        esp_vfs_fat_spiflash_unmount_rw_wl(base_path, s_wl_handle);
        s_wl_handle = WL_INVALID_HANDLE;
    }

    // 重新挂载FAT文件系统（分区已经存在，不需要重新注册）
    return storage_mount_fatfs("storage");
}

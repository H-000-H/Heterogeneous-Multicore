#include "app_flash_task.hpp"

#include "w25q64_drv.h"
#include "device.h"
#include "VFS.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

#include <string.h>

static const char* kTag = "FlashTask";

static const uint32_t kTestSectorAddr = 0x00010000U;
static const size_t   kTestPatternLen = 64U;

static bool flash_seek(struct device* dev, uint32_t offset)
{
    return device_ioctl(dev, W25Q64_CMD_SEEK, &offset, sizeof(offset), 5000) == VFS_OK;
}

static void flash_log_jedec(struct device* dev)
{
    struct w25q64_jedec_arg jedec;

    if (device_ioctl(dev, W25Q64_CMD_READ_JEDEC_ID, &jedec, sizeof(jedec), 5000) != VFS_OK)
    {
        ESP_LOGE(kTag, "JEDEC ID read failed");
        return;
    }

    ESP_LOGI(kTag, "JEDEC ID: %02X %02X %02X%s",
             jedec.id[0], jedec.id[1], jedec.id[2],
             w25q64_jedec_match_w25q64jv(jedec.id) ? " (W25Q64JV OK)" : " (unexpected)");
}

static bool flash_sector_is_erased(struct device* dev, uint32_t addr, size_t len)
{
    uint8_t buf[kTestPatternLen];

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag, "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_read(dev, buf, len, 5000) != (int)len)
    {
        ESP_LOGE(kTag, "post-erase read failed");
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] != 0xFFU)
        {
            ESP_LOGE(kTag, "erase verify fail @+%zu: 0x%02X", i, buf[i]);
            return false;
        }
    }

    return true;
}

static bool flash_write_read_verify(struct device* dev, uint32_t addr)
{
    uint8_t tx[kTestPatternLen];
    uint8_t rx[kTestPatternLen];

    for (size_t i = 0; i < kTestPatternLen; i++)
        tx[i] = (uint8_t)(0xA5U ^ (uint8_t)i);

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag, "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_write(dev, tx, kTestPatternLen, 10000) != (int)kTestPatternLen)
    {
        ESP_LOGE(kTag, "write failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag, "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_read(dev, rx, kTestPatternLen, 5000) != (int)kTestPatternLen)
    {
        ESP_LOGE(kTag, "readback failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (memcmp(tx, rx, kTestPatternLen) != 0)
    {
        ESP_LOGE(kTag, "data mismatch @0x%08lX", (unsigned long)addr);
        return false;
    }

    return true;
}

static bool flash_run_smoke_test(struct device* dev)
{
    flash_log_jedec(dev);

    ESP_LOGI(kTag, "sector erase @0x%08lX ...", (unsigned long)kTestSectorAddr);
    uint32_t erase_addr = kTestSectorAddr;
    if (device_ioctl(dev, W25Q64_CMD_SECTOR_ERASE, &erase_addr,
                     sizeof(erase_addr), 30000) != VFS_OK)
    {
        ESP_LOGE(kTag, "sector erase failed");
        return false;
    }

    if (!flash_sector_is_erased(dev, kTestSectorAddr, kTestPatternLen))
        return false;

    ESP_LOGI(kTag, "erase OK, writing %u bytes ...", (unsigned)kTestPatternLen);
    if (!flash_write_read_verify(dev, kTestSectorAddr))
        return false;

    ESP_LOGI(kTag, "W25Q64 smoke test PASSED");
    return true;
}

static void flash_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find_by_label("w25q64_master");
    if (!dev)
    {
        ESP_LOGE(kTag, "w25q64_master not found");
        osal_task_self_delete();
        return;
    }

    if (device_open(dev, NULL) != VFS_OK)
    {
        ESP_LOGE(kTag, "device_open failed");
        osal_task_self_delete();
        return;
    }

    if (!flash_run_smoke_test(dev))
        ESP_LOGE(kTag, "W25Q64 smoke test FAILED");

    for (;;)
    {
        system_wdt_feed();
        osal_delay_ms(5000);
    }
}

void app_flash_task_start(void)
{
    task_manager_create_task("flash_test", 4096, 8, flash_task_entry, NULL, 0);
}

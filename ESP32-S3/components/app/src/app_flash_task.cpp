#include "app_flash_task.hpp"

#include "w25q64_drv.h"
#include "device.h"
#include "VFS.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

#include <etl/array.h>
#include <etl/string.h>

static etl::string<16> kTag = "FlashTask";
static etl::string<16> kTaskName = "flash_test";

static constexpr uint32_t kTestSectorAddr = 0x00010000U;
static constexpr size_t   kTestPatternLen = 64U;

using TestPatternBuf = etl::array<uint8_t, kTestPatternLen>;

static bool flash_seek(struct device* dev, uint32_t offset)
{
    return device_ioctl(dev, W25Q64_CMD_SEEK, &offset, sizeof(offset), 5000) == VFS_OK;
}

static void flash_log_jedec(struct device* dev)
{
    struct w25q64_jedec_arg jedec;

    if (device_ioctl(dev, W25Q64_CMD_READ_JEDEC_ID, &jedec, sizeof(jedec), 5000) != VFS_OK)
    {
        ESP_LOGE(kTag.c_str(), "JEDEC ID read failed");
        return;
    }

    ESP_LOGI(kTag.c_str(), "JEDEC ID: %02X %02X %02X%s",
             jedec.id[0], jedec.id[1], jedec.id[2],
             w25q64_jedec_match_w25q64jv(jedec.id) ? " (W25Q64JV OK)" : " (unexpected)");
}

static bool flash_sector_is_erased(struct device* dev, uint32_t addr)
{
    TestPatternBuf buf{};

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag.c_str(), "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_read(dev, buf.data(), buf.size(), 5000) != (int)buf.size())
    {
        ESP_LOGE(kTag.c_str(), "post-erase read failed");
        return false;
    }

    for (size_t i = 0; i < buf.size(); i++)
    {
        if (buf[i] != 0xFFU)
        {
            ESP_LOGE(kTag.c_str(), "erase verify fail @+%zu: 0x%02X", i, buf[i]);
            return false;
        }
    }

    return true;
}

static bool flash_write_read_verify(struct device* dev, uint32_t addr)
{
    TestPatternBuf tx{};
    TestPatternBuf rx{};

    for (size_t i = 0; i < tx.size(); i++)
        tx[i] = (uint8_t)(0xA5U ^ (uint8_t)i);

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag.c_str(), "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_write(dev, tx.data(), tx.size(), 10000) != (int)tx.size())
    {
        ESP_LOGE(kTag.c_str(), "write failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (!flash_seek(dev, addr))
    {
        ESP_LOGE(kTag.c_str(), "seek failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (device_read(dev, rx.data(), rx.size(), 5000) != (int)rx.size())
    {
        ESP_LOGE(kTag.c_str(), "readback failed @0x%08lX", (unsigned long)addr);
        return false;
    }

    if (__builtin_memcmp(tx.data(), rx.data(), tx.size()) != 0)
    {
        ESP_LOGE(kTag.c_str(), "data mismatch @0x%08lX", (unsigned long)addr);
        return false;
    }

    return true;
}

static bool flash_run_smoke_test(struct device* dev)
{
    flash_log_jedec(dev);

    ESP_LOGI(kTag.c_str(), "sector erase @0x%08lX ...", (unsigned long)kTestSectorAddr);
    uint32_t erase_addr = kTestSectorAddr;
    if (device_ioctl(dev, W25Q64_CMD_SECTOR_ERASE, &erase_addr,
                     sizeof(erase_addr), 30000) != VFS_OK)
    {
        ESP_LOGE(kTag.c_str(), "sector erase failed");
        return false;
    }

    if (!flash_sector_is_erased(dev, kTestSectorAddr))
        return false;

    ESP_LOGI(kTag.c_str(), "erase OK, writing %zu bytes ...", TestPatternBuf{}.size());
    if (!flash_write_read_verify(dev, kTestSectorAddr))
        return false;

    ESP_LOGI(kTag.c_str(), "W25Q64 smoke test PASSED");
    return true;
}

static void flash_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find_by_label("w25q64_master");
    if (IS_ERR(dev))
    {
        ESP_LOGE(kTag.c_str(), "w25q64_master not found (err=%d)", PTR_ERR(dev));
        osal_task_self_delete();
        return;
    }

    enum device_status st = device_get_status(dev);
    if (st == DEVICE_STATUS_DISABLED)
    {
        ESP_LOGW(kTag.c_str(), "w25q64_master disabled in DTS — skipping flash test");
        osal_task_self_delete();
        return;
    }

    if (st != DEVICE_STATUS_RUNNING && st != DEVICE_STATUS_PROBED)
    {
        ESP_LOGE(kTag.c_str(), "w25q64_master not ready (status=%d)", (int)st);
        osal_task_self_delete();
        return;
    }

    if (st == DEVICE_STATUS_PROBED && device_open(dev, NULL) != VFS_OK)
    {
        ESP_LOGE(kTag.c_str(), "device_open failed");
        osal_task_self_delete();
        return;
    }

    if (!flash_run_smoke_test(dev))
        ESP_LOGE(kTag.c_str(), "W25Q64 smoke test FAILED");

    for (;;)
    {
        system_wdt_feed();
        osal_delay_ms(5000);
    }
}

void app_flash_task_start(void)
{
    struct device* dev = device_find_by_label("w25q64_master");
    if (IS_ERR(dev))
    {
        ESP_LOGW(kTag.c_str(), "w25q64_master not in device tree — skipping flash test");
        return;
    }
    if (device_get_status(dev) == DEVICE_STATUS_DISABLED)
    {
        ESP_LOGW(kTag.c_str(), "w25q64_master disabled in DTS — skipping flash test");
        return;
    }

    task_manager_create_task(kTaskName.c_str(), 4096, 8, flash_task_entry, NULL, 0);
}

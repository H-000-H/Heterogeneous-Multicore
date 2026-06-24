/*
 * Flash 领域任务 — 拥有专属消息队列, 异步执行 W25Q64 硬件操作.
 *
 * Sector 擦除可阻塞 30 秒, 但完全在独立任务上下文中执行,
 * 不影响 SPI RX 驱动和其他任务的运行.
 */

#include "app_flash_task.hpp"
#include "app_cmd_handlers.hpp"

#include "device.h"
#include "VFS.h"
#include "w25q64_drv.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"
#include "system_log.h"

static constexpr const char* kTag = "FlashTask";

// Flash 领域任务的专属队列
osal_queue_handle_t g_flash_queue = nullptr;

static void flash_task_entry(void* arg)
{
    struct device* dev = static_cast<struct device*>(arg);
    CmdFlashErase cmd;

    for (;;)
    {
        system_wdt_feed();

        // 阻塞在自己的队列上, 爱等多久等多久
        if (osal_queue_receive(g_flash_queue, &cmd, OSAL_WAIT_FOREVER) != true)
            continue;

        SYS_LOGI(kTag, "erase sector @0x%08lX ...", (unsigned long)cmd.sector_addr);

        uint32_t addr = cmd.sector_addr;
        int ret = device_ioctl(dev, W25Q64_CMD_SECTOR_ERASE, &addr, sizeof(addr), 30000);

        if (ret != VFS_OK)
        {
            SYS_LOGE(kTag, "erase failed: %d", ret);
        }
        else
        {
            SYS_LOGI(kTag, "erase OK");
        }
    }
}

void app_flash_task_start(void)
{
    struct device* dev = device_find_by_label("w25q64_master");
    if (IS_ERR(dev))
    {
        SYS_LOGW(kTag, "w25q64_master not found — disabled");
        return;
    }

    if (device_get_status(dev) == DEVICE_STATUS_DISABLED)
    {
        SYS_LOGW(kTag, "w25q64_master disabled in DTS");
        return;
    }

    if (device_open(dev, nullptr) != VFS_OK)
    {
        SYS_LOGE(kTag, "device_open failed");
        return;
    }

    // 创建队列: 深度 4, 元素大小为 CmdFlashErase
    g_flash_queue = osal_queue_create(2, sizeof(CmdFlashErase));
    if (!g_flash_queue)
    {
        SYS_LOGE(kTag, "queue create failed");
        return;
    }

    task_manager_create_task("flash", 1024, 8, flash_task_entry, dev, 0);
    SYS_LOGI(kTag, "started, device=%s", device_get_name(dev));
}

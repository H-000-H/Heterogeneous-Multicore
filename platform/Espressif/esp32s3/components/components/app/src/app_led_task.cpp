/*
 * LED 领域任务 — 拥有专属消息队列, 异步执行 WS2812 硬件操作.
 *
 * 不阻塞 SPI RX 任务, 不直接接 SystemCmd dispatch 链路.
 */

#include "app_led_task.hpp"
#include "app_cmd_handlers.hpp"

#include "device.h"
#include "VFS.h"
#include "ws2812_drv.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"
#include "system_log.h"

#include <cstring>

static constexpr const char* kTag = "LedTask";

// LED 领域任务的专属队列
osal_queue_handle_t g_led_queue = nullptr;

static void led_task_entry(void* arg)
{
    struct device* dev = static_cast<struct device*>(arg);
    CmdLedSet cmd;

    for (;;)
    {
        system_wdt_feed();

        // 阻塞在自己的队列上, 不影响 SPI RX 收包
        if (osal_queue_receive(g_led_queue, &cmd, OSAL_WAIT_FOREVER) != true)
            continue;

        ws2812_color color = { cmd.r, cmd.g, cmd.b };
        int ret = device_ioctl(dev, WS2812_CMD_SET_COLOR, &color, sizeof(color), 100);
        if (ret != VFS_OK)
        {
            SYS_LOGE(kTag, "set_color failed: %d", ret);
        }
    }
}

void app_led_task_start(void)
{
    struct device* dev = device_find_by_label("ws2812");
    if (IS_ERR(dev))
    {
        SYS_LOGW(kTag, "ws2812 not found — disabled");
        return;
    }

    if (device_open(dev, nullptr) != VFS_OK)
    {
        SYS_LOGE(kTag, "device_open failed");
        return;
    }

    // 创建队列: 深度 4, 元素大小为 CmdLedSet
    g_led_queue = osal_queue_create(4, sizeof(CmdLedSet));
    if (!g_led_queue)
    {
        SYS_LOGE(kTag, "queue create failed");
        return;
    }

    task_manager_create_task("led", 2048, 10, led_task_entry, dev, 0);
    SYS_LOGI(kTag, "started, device=%s", device_get_name(dev));
}

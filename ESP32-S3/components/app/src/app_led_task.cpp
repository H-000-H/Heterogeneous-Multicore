#include "app_led_task.hpp"

#include "device.h"
#include "VFS.h"
#include "ws2812_drv.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

static const char* kTag = "LedTask";
static constexpr uint32_t kLedTimeoutMs = 100;

static const struct ws2812_color kColors[] = {
    {255, 0,   0  },
    {0,   255, 0  },
    {0,   0,   255},
    {255, 255, 255},
};

static void led_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find("ws2812");
    if (!dev)
    {
        ESP_LOGE(kTag, "ws2812 device not found");
        osal_task_self_delete();
        return;
    }

    size_t color_idx = 0;
    for (;;)
    {
        system_wdt_feed();

        struct ws2812_color color = kColors[color_idx];
        if (device_ioctl(dev, WS2812_CMD_SET_COLOR, &color, sizeof(color),
                         kLedTimeoutMs) != VFS_OK)
        {
            ESP_LOGW(kTag, "set color failed");
        }

        color_idx = (color_idx + 1) % (sizeof(kColors) / sizeof(kColors[0]));
        osal_delay_ms(500);
    }
}

void app_led_task_start(void)
{
    task_manager_create_task("led", 2048, 10, led_task_entry, NULL, 0);
}

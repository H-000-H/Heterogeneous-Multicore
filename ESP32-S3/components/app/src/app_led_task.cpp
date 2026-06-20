#include "app_led_task.hpp"

#include "device.h"
#include "VFS.h"
#include "ws2812_drv.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

#include <etl/array.h>
#include <etl/string.h>

static etl::string<16> kTag = "LedTask";
static etl::string<16> kTaskName = "led";
static constexpr uint32_t kLedTimeoutMs = 100;

static constexpr etl::array<ws2812_color, 4> kColors = {{
    ws2812_color{255, 0,   0  },
    ws2812_color{0,   255, 0  },
    ws2812_color{0,   0,   255},
    ws2812_color{255, 255, 255},
}};

static void led_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find_by_label("ws2812");
    if (IS_ERR(dev))
    {
        ESP_LOGE(kTag.c_str(), "ws2812 device not found (err=%d)", PTR_ERR(dev));
        osal_task_self_delete();
        return;
    }

    if (device_open(dev, NULL) != VFS_OK)
    {
        ESP_LOGE(kTag.c_str(), "device_open failed");
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
            ESP_LOGW(kTag.c_str(), "set color failed");
        }

        color_idx = (color_idx + 1) % kColors.size();
        osal_delay_ms(500);
    }
}

void app_led_task_start(void)
{
    task_manager_create_task(kTaskName.c_str(), 2048, 10, led_task_entry, NULL, 0);
}

#include "app_spi_task.hpp"

#include "device.h"
#include "VFS.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

#include <etl/string.h>

static etl::string<16> kTag = "SpiTask";
static etl::string<16> kTaskName = "spi_test";

static void spi_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find_by_label("fft_slave");
    if (IS_ERR(dev))
    {
        ESP_LOGE(kTag.c_str(), "fft_slave not found (err=%d)", PTR_ERR(dev));
        osal_task_self_delete();
        return;
    }

    enum device_status st = device_get_status(dev);
    if (st != DEVICE_STATUS_RUNNING && st != DEVICE_STATUS_PROBED)
    {
        ESP_LOGE(kTag.c_str(), "fft_slave not ready (status=%d)", (int)st);
        osal_task_self_delete();
        return;
    }

    if (st == DEVICE_STATUS_PROBED && device_open(dev, NULL) != VFS_OK)
    {
        ESP_LOGE(kTag.c_str(), "device_open failed");
        osal_task_self_delete();
        return;
    }

    ESP_LOGI(kTag.c_str(), "SPI slave open OK — ready for master on CS=10");

    for (;;)
    {
        system_wdt_feed();
        osal_delay_ms(2000);
    }
}

void app_spi_task_start(void)
{
    task_manager_create_task(kTaskName.c_str(), 3072, 9, spi_task_entry, NULL, 0);
}

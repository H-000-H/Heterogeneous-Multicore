#include "app_spi_task.hpp"

#include "device.h"
#include "VFS.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

#include "esp_log.h"

static const char* kTag = "SpiTask";

static void spi_task_entry(void* arg)
{
    (void)arg;

    struct device* dev = device_find_by_label("fft_slave");
    if (!dev)
    {
        ESP_LOGE(kTag, "fft_slave not found");
        osal_task_self_delete();
        return;
    }

    enum device_status st = device_get_status(dev);
    ESP_LOGI(kTag, "fft_slave status=%d (expect PROBED=%d)", (int)st, (int)DEVICE_STATUS_PROBED);

    if (device_open(dev, NULL) != VFS_OK)
    {
        ESP_LOGE(kTag, "device_open failed");
        osal_task_self_delete();
        return;
    }

    ESP_LOGI(kTag, "SPI slave open OK — ready for master on CS=10");

    for (;;)
    {
        system_wdt_feed();
        osal_delay_ms(2000);
    }
}

void app_spi_task_start(void)
{
    task_manager_create_task("spi_test", 3072, 9, spi_task_entry, NULL, 0);
}

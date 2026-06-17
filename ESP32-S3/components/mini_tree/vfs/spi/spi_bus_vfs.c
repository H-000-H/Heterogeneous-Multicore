#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include <stddef.h>
#include "system_log.h"

static const char* const kTag = "spi_bus_vfs";

static int spi_bus_probe(struct device *pdev)
{
    int host = -1,mosi = -1, miso = -1,sclk =-1, dma_chan=-1;
    int max_trans = -1;
    struct hal_spi_bus_config bus_cfg;
    if (device_get_prop_int(pdev, "host-id", &host) ||
    device_get_prop_int(pdev, "miso-pin", &miso) ||
    device_get_prop_int(pdev, "mosi-pin", &mosi) ||
    device_get_prop_int(pdev, "sclk-pin", &sclk) ||
    device_get_prop_int(pdev, "dma-chan", &dma_chan))
        goto err_prop;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "max-trans-buffer", &max_trans));
    bus_cfg.host_id = host;
    bus_cfg.mosi    = mosi;
    bus_cfg.sclk    = sclk;
    bus_cfg.dma_chan= dma_chan;
    bus_cfg.max_transfer_sz = max_trans > 0 ? max_trans :0;
    if(hal_spi_bus_host_init(host, &bus_cfg))
    {
        SYS_LOGE(kTag, "hal_spi_bus_host_init failed host=%d", host);
        return VFS_ERR_IO;
    }
    SYS_LOGI(kTag, "host probe OK: host=%d mosi=%d miso=%d sclk=%d",host, mosi, miso, sclk);
    return VFS_OK;
err_prop:
    SYS_LOGE(kTag, "host probe property error: %s", device_get_name(pdev));
    return VFS_ERR_INVAL;
}

static int spi_bus_remove(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    int host = -1;
    if(device_get_prop_int(pdev,"host-id",&host)!=0)
        return VFS_ERR_INVAL;
    return hal_spi_bus_host_deinit(host);
}

DRIVER_REGISTER(spi_bus, "esp32,spi-host",spi_bus_probe, spi_bus_remove)

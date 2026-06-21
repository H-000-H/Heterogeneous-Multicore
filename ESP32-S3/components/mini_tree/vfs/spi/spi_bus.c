#include "bus.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "board_devtable.h"
#include "dt_config_gen.h"
#include "compiler_compat.h"
#include "osal.h"
#include <stddef.h>
#include <string.h>
#include "system_log.h"
#include "compiler_compat_poison.h"

static const char* const kTag = "spi_bus";

static const struct bus_type spi_bus_type =
{
    .name = "spi",
};

/* SPI 总线控制器创建的私有数据结构*/
struct spi_controller_priv
{
    int                    host_id;
    struct hal_spi_bus_host* host;
    int                    pool_idx;
};

#define SPI_CONTROLLER_COUNT \
    (DTC_GEN_COUNT_ESP32_SPI + DTC_GEN_COUNT_ESP32_SPI_MASTER)

/*位图池*/
static struct spi_controller_priv s_controller_pool[SPI_CONTROLLER_COUNT] COMPAT_ALIGNED(4);
static uint8_t                    s_controller_used[SPI_CONTROLLER_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t                s_controller_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(160)
static void spi_controller_pool_boot_init(void)
{
    osal_pool_init(&s_controller_pool_ctrl, s_controller_used, SPI_CONTROLLER_COUNT);
}

static int spi_controller_probe_impl(struct device* dev, int bus_role)
{
    struct spi_controller_priv* priv;
    struct hal_spi_bus_config   bus_cfg;
    int                         host = -1;
    int                         mosi = -1;
    int                         miso = -1;
    int                         sclk = -1;
    int                         dma_chan = -1;
    int                         max_trans = -1;
    int                         pool_idx;

    if (device_get_prop_int(dev, "host-id", &host) ||
        device_get_prop_int(dev, "miso-pin", &miso) ||
        device_get_prop_int(dev, "mosi-pin", &mosi) ||
        device_get_prop_int(dev, "sclk-pin", &sclk) ||
        device_get_prop_int(dev, "dma-chan", &dma_chan))
        goto err_prop;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-trans-buffer", &max_trans));

    bus_cfg.host_id         = host;
    bus_cfg.mosi            = mosi;
    bus_cfg.miso            = miso;
    bus_cfg.sclk            = sclk;
    bus_cfg.dma_chan        = dma_chan;
    bus_cfg.max_transfer_sz = max_trans > 0 ? max_trans : 0;
    bus_cfg.bus_role        = bus_role;

    if (hal_spi_bus_host_init(host, &bus_cfg) != VFS_OK)
    {
        SYS_LOGE(kTag, "hal_spi_bus_host_init failed host=%d", host);
        return VFS_ERR_IO;
    }

    pool_idx = osal_pool_claim(&s_controller_pool_ctrl);
    if (pool_idx < 0)
    {
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host));
        return VFS_ERR_NOMEM;
    }

    priv = &s_controller_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->host_id  = host;
    if (hal_spi_bus_host_get(host, &priv->host) != VFS_OK)
    {
        osal_pool_release(&s_controller_pool_ctrl, pool_idx);
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host));
        return VFS_ERR_IO;
    }

    if (bus_controller_bind(dev, &spi_bus_type, priv->host) != VFS_OK)
    {
        osal_pool_release(&s_controller_pool_ctrl, pool_idx);
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host));
        return VFS_ERR_IO;
    }

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        bus_controller_unbind(dev);
        osal_pool_release(&s_controller_pool_ctrl, pool_idx);
        COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host));
        return VFS_ERR_IO;
    }

    SYS_LOGI(kTag, "controller probe OK: role=%s host=%d mosi=%d miso=%d sclk=%d",
             bus_role == HAL_SPI_BUS_ROLE_MASTER ? "master" : "slave",
             host, mosi, miso, sclk);
    return VFS_OK;

err_prop:
    SYS_LOGE(kTag, "controller property error: %s", device_get_name(dev));
    return VFS_ERR_INVAL;
}

static int spi_controller_probe(struct device* dev)
{
    return spi_controller_probe_impl(dev, HAL_SPI_BUS_ROLE_SLAVE);
}

static int spi_master_controller_probe(struct device* dev)
{
    return spi_controller_probe_impl(dev, HAL_SPI_BUS_ROLE_MASTER);
}

static int spi_controller_remove(struct device* dev)
{
    struct spi_controller_priv* priv = (struct spi_controller_priv*)device_get_priv(dev);
    device_id_t                 dev_id;
    int                         child_count = 0;
    const device_id_t*          children;
    int                         i;
    int                         host;
    int                         pool_idx;

    if (IS_ERR(priv))
        return PTR_ERR(priv);

    host     = priv->host_id;
    pool_idx = priv->pool_idx;

    dev_id = board_dev_find(device_get_name(dev));
    if ((int)dev_id >= 0)
    {
        children = board_cascade_get(dev_id, &child_count);
        for (i = 0; i < child_count; i++)
        {
            struct device* child = board_dev_get(children[i]);
            if (!IS_ERR(child) && child)
                bus_client_unbind(child);
        }
    }

    bus_controller_unbind(dev);
    COMPAT_IGNORE_RESULT(device_set_priv(dev, NULL));
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_controller_pool_ctrl, pool_idx);

    return hal_spi_bus_host_deinit(host);
}

DRIVER_REGISTER(spi_bus, "esp32,spi", spi_controller_probe, spi_controller_remove)
DRIVER_REGISTER(spi_master_bus, "esp32,spi-master", spi_master_controller_probe, spi_controller_remove)

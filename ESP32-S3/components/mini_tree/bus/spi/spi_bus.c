/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI Bus Framework — ESP32-S3 master + slave
 */
#include "spi_bus.h"
#include "hal_spi.h"
#include "bus_controller.h"
#include "bus_client.h"
#include "device.h"
#include "driver.h"
#include "board_devtable.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define SPI_BUS_HOST_MAX       4

struct spi_bus_host {
    struct device*               dev;
    struct hal_spi_bus_config    hal_cfg;
    struct hal_spi_bus_host*     hal_host;
    struct osal_mutex*           bus_mutex;
    int                          ref_count;
    uint8_t                      in_use;
    uint8_t                      mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
};

struct spi_bus_client {
    struct device*               dev;
    struct spi_bus_host*         host;
    struct spi_bus_client_config cfg;
    struct hal_spi_dev           hal_dev;
    int                          hw_open;
};

static struct spi_bus_host   s_spi_hosts[SPI_BUS_HOST_MAX];
static struct spi_bus_client s_spi_clients[DEV_ID_COUNT];
static const char* const     kTag = "spi_bus";

static int spi_host_pool_claim(void)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (!s_spi_hosts[i].in_use)
        {
            s_spi_hosts[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void spi_host_pool_release(int idx)
{
    if (idx >= 0 && idx < SPI_BUS_HOST_MAX)
        memset(&s_spi_hosts[idx], 0, sizeof(s_spi_hosts[idx]));
}

static struct spi_bus_host* spi_host_from_device(struct device* dev)
{
    for (int i = 0; i < SPI_BUS_HOST_MAX; i++)
    {
        if (s_spi_hosts[i].in_use && s_spi_hosts[i].dev == dev)
            return &s_spi_hosts[i];
    }
    return NULL;
}

static struct spi_bus_client* spi_client_from_device(struct device* dev)
{
    int id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_spi_clients[id].dev)
        return NULL;
    return &s_spi_clients[id];
}

static int spi_bus_parse_host_dts(struct device* dev, struct hal_spi_bus_config* cfg)
{
    hal_pin_t mosi = {0};
    hal_pin_t miso = {0};
    hal_pin_t sclk = {0};

    if (device_get_prop_int(dev, "host-id", &cfg->host_id) != VFS_OK ||
        hal_pin_probe(dev, "mosi-port", "mosi-pin", &mosi) != VFS_OK ||
        hal_pin_probe(dev, "miso-port", "miso-pin", &miso) != VFS_OK ||
        hal_pin_probe(dev, "sclk-port", "sclk-pin", &sclk) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    cfg->mosi = mosi;
    cfg->miso = miso;
    cfg->sclk = sclk;
    cfg->dma_chan = -1;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "dma-chan", &cfg->dma_chan));
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-transfer-buffer", &cfg->max_transfer_sz));
    if (cfg->max_transfer_sz <= 0)
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-trans-buffer", &cfg->max_transfer_sz));

    if (cfg->max_transfer_sz <= 0)
        cfg->max_transfer_sz = 4096;

    return VFS_OK;
}

static int spi_bus_host_transfer(struct bus_controller* ctlr,
                                  struct bus_client* cli,
                                  const void* tx, void* rx,
                                  size_t len, uint32_t timeout_ms)
{
    struct spi_bus_host*   host = (struct spi_bus_host*)bus_controller_priv(ctlr);
    struct spi_bus_client* client = (struct spi_bus_client*)bus_client_priv(cli);

    if (!host || !client)
        return VFS_ERR_INVAL;

    return spi_bus_transfer(client->dev, (const uint8_t*)tx, (uint8_t*)rx, len, timeout_ms);
}

static const struct bus_controller_ops spi_bus_controller_ops = {
    .transfer = spi_bus_host_transfer,
};

static int spi_bus_host_probe_impl(struct device* dev, int bus_role)
{
    struct spi_bus_host*        host;
    struct hal_spi_bus_config   hal_cfg = {0};
    int                         idx;
    int                         ret;

    if (!dev)
        return VFS_ERR_INVAL;

    hal_cfg.bus_role = bus_role;
    if (spi_bus_parse_host_dts(dev, &hal_cfg) != VFS_OK)
    {
        SYS_LOGE(kTag, "dts parse failed: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    idx = spi_host_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_spi_hosts[idx];
    host->dev = dev;
    host->hal_cfg = hal_cfg;

    if (osal_mutex_create_static(&host->bus_mutex, host->mutex_storage,
                                  sizeof(host->mutex_storage)) != 0)
    {
        spi_host_pool_release(idx);
        return VFS_ERR_NOMEM;
    }

    ret = hal_spi_bus_host_init(hal_cfg.host_id, &hal_cfg);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = hal_spi_bus_host_get(hal_cfg.host_id, &host->hal_host);
    if (ret != VFS_OK)
        goto err_hal;

    ret = bus_controller_register(dev, BUS_CONTROLLER_SPI, &spi_bus_controller_ops, host);
    if (ret != VFS_OK)
        goto err_hal;

    if (device_set_priv(dev, host) != VFS_OK)
    {
        bus_controller_unregister(dev);
        goto err_hal;
    }

    SYS_LOGI(kTag, "host probe OK: %s role=%s host=%d",
             device_get_name(dev),
             bus_role == SPI_BUS_ROLE_SLAVE ? "slave" : "master",
             hal_cfg.host_id);
    return VFS_OK;

err_hal:
    COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(hal_cfg.host_id));
err_mutex:
    osal_mutex_destroy(host->bus_mutex);
    spi_host_pool_release(idx);
    return ret;
}

int spi_bus_host_probe(struct device* dev)
{
    return spi_bus_host_probe_impl(dev, SPI_BUS_ROLE_MASTER);
}

static int spi_bus_host_probe_slave(struct device* dev)
{
    return spi_bus_host_probe_impl(dev, SPI_BUS_ROLE_SLAVE);
}

int spi_bus_host_remove(struct device* dev)
{
    struct spi_bus_host* host;

    if (!dev)
        return VFS_ERR_INVAL;

    host = spi_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    bus_controller_unregister(dev);
    COMPAT_IGNORE_RESULT(hal_spi_bus_host_deinit(host->hal_cfg.host_id));
    osal_mutex_destroy(host->bus_mutex);
    spi_host_pool_release((int)(host - s_spi_hosts));

    return VFS_OK;
}

int spi_bus_child_host_role(struct device* dev)
{
    struct bus_controller* ctlr;
    struct spi_bus_host*   host;

    if (!dev)
        return -1;

    ctlr = bus_controller_get_by_parent(dev);
    if (!ctlr || bus_controller_type_get(ctlr) != BUS_CONTROLLER_SPI)
        return -1;

    host = (struct spi_bus_host*)bus_controller_priv(ctlr);
    if (!host)
        return -1;

    return host->hal_cfg.bus_role;
}

int spi_bus_client_register(struct device* dev,
                             const struct spi_bus_client_config* cfg,
                             struct spi_bus_client** out)
{
    struct bus_controller* ctlr;
    struct spi_bus_host*   host;
    struct spi_bus_client* client;
    int                    id;

    if (!dev || !cfg || !out)
        return VFS_ERR_INVAL;
    *out = NULL;

    ctlr = bus_controller_get_by_parent(dev);
    if (!ctlr || bus_controller_type_get(ctlr) != BUS_CONTROLLER_SPI)
        return VFS_ERR_NODEV;

    host = (struct spi_bus_host*)bus_controller_priv(ctlr);
    if (!host)
        return VFS_ERR_IO;

    id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    client = &s_spi_clients[id];
    memset(client, 0, sizeof(*client));
    client->dev  = dev;
    client->host = host;
    client->cfg  = *cfg;

    if (bus_client_register(dev, NULL, client) != VFS_OK)
    {
        memset(client, 0, sizeof(*client));
        return VFS_ERR_IO;
    }

    *out = client;
    return VFS_OK;
}

void spi_bus_client_unregister(struct device* dev)
{
    struct spi_bus_client* client;

    client = spi_client_from_device(dev);
    if (!client)
        return;

    bus_client_unregister(dev);
    memset(client, 0, sizeof(*client));
}

int spi_bus_open(struct device* dev)
{
    struct spi_bus_client*       client;
    struct hal_spi_device_config dev_cfg;
    int                          ret;

    client = spi_client_from_device(dev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
        return VFS_OK;

    dev_cfg.mode           = client->cfg.mode;
    dev_cfg.clock_speed_hz = client->cfg.clock_speed_hz;
    dev_cfg.cs_pin         = HAL_MAKE_PIN((client->cfg.cs_pin >> 16) & 0xFFFF,
                                           client->cfg.cs_pin & 0xFFFF);
    dev_cfg.queue_size     = client->cfg.queue_size > 0 ? client->cfg.queue_size : 4;

    hal_spi_dev_init(&client->hal_dev, (int)(client - s_spi_clients),
                     client->host->hal_host, &dev_cfg);
    ret = hal_spi_dev_hw_open(&client->hal_dev);
    if (ret != VFS_OK)
        return ret;

    client->hw_open = 1;
    client->host->ref_count++;
    return VFS_OK;
}

int spi_bus_close(struct device* dev)
{
    struct spi_bus_client* client;

    client = spi_client_from_device(dev);
    if (!client)
        return VFS_ERR_NODEV;

    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(hal_spi_dev_hw_close(&client->hal_dev));
        client->hw_open = 0;
        if (client->host->ref_count > 0)
            client->host->ref_count--;
    }
    return VFS_OK;
}

int spi_bus_slave_sync(struct device* dev,
                        const uint8_t* tx, uint8_t* rx,
                        size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || len == 0 || (!tx && !rx))
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (client->host->hal_cfg.bus_role != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return spi_slave_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

int spi_bus_slave_queue_tx(struct device* dev,
                            const uint8_t* data, size_t len,
                            uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (client->host->hal_cfg.bus_role != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return spi_slave_queue_tx(&client->hal_dev, data, len, timeout_ms);
}

int spi_bus_slave_get_trans_result(struct device* dev,
                                    uint8_t* rx_data, size_t rx_cap,
                                    size_t* trans_len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (client->host->hal_cfg.bus_role != SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    return hal_spi_get_trans_result(&client->hal_dev, rx_data, rx_cap,
                                     trans_len, timeout_ms);
}

int spi_bus_transfer(struct device* dev,
                      const uint8_t* tx, uint8_t* rx,
                      size_t len, uint32_t timeout_ms)
{
    struct spi_bus_client* client;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    client = spi_client_from_device(dev);
    if (!client || !client->hw_open)
        return VFS_ERR_NODEV;

    if (client->host->hal_cfg.bus_role == SPI_BUS_ROLE_SLAVE)
        return spi_bus_slave_sync(dev, tx, rx, len, timeout_ms);

    return spi_sync(&client->hal_dev, tx, rx, len, timeout_ms);
}

DRIVER_REGISTER(spi_bus_host_esp32_master, "esp32,spi-master",
                spi_bus_host_probe, spi_bus_host_remove)
DRIVER_REGISTER(spi_bus_host_esp32_slave, "esp32,spi",
                spi_bus_host_probe_slave, spi_bus_host_remove)

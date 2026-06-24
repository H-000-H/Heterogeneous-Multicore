/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI VFS 驱动
 *
 * 职责：
 *   - 实现 file_operations，对接 Device Model 生命周期
 *   - 所有实际 I/O 通过 spi_bus 层路由到 Controller / HAL
 */
#include "spi_vfs.h"
#include "spi_bus.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define SPI_VFS_CLIENT_COUNT 4

struct spi_vfs_client {
    struct file_operations ops;
    struct spi_bus_client_config  cfg;
    struct osal_mutex*     io_mutex;
    uint8_t                mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                    pool_idx;
};

static struct spi_vfs_client s_spi_vfs_pool[SPI_VFS_CLIENT_COUNT];
static uint8_t               s_spi_vfs_used[SPI_VFS_CLIENT_COUNT];
static osal_pool_t           s_spi_vfs_pool_ctrl;
static const char* const     kTag = "spi_vfs";

pre_execution(160)
static void spi_vfs_pool_init(void)
{
    osal_pool_init(&s_spi_vfs_pool_ctrl, s_spi_vfs_used, SPI_VFS_CLIENT_COUNT);
}

static struct spi_vfs_client* spi_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct spi_vfs_client, ops);
}

static int spi_vfs_open(struct device* dev, void* arg)
{
    struct spi_vfs_client* priv;
    struct dev_lifecycle*  lc;
    int                    first;

    (void)arg;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    if (first == 1)
    {
        if (spi_bus_open(dev) != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

static int spi_vfs_close(struct device* dev)
{
    struct spi_vfs_client* priv;
    struct dev_lifecycle*  lc;
    int                    last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(spi_bus_close(dev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int spi_vfs_write(struct device* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    ret = spi_bus_transfer(dev, (const uint8_t*)buf, NULL, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_vfs_read(struct device* dev, void* buf, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops || !buf || len == 0)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    ret = spi_bus_transfer(dev, NULL, (uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_vfs_ioctl(struct device* dev, int cmd, void* arg,
                          size_t arg_len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    (void)timeout_ms;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd)
    {
    case SPI_CMD_TRANSFER:
    {
        struct spi_transfer_arg* t = (struct spi_transfer_arg*)arg;
        if (!t || arg_len != sizeof(*t) || !t->tx || !t->rx || t->len == 0)
        {
            ret = VFS_ERR_INVAL;
            break;
        }
        ret = spi_bus_transfer(dev, t->tx, t->rx, t->len, timeout_ms);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations spi_vfs_fops = {
    .open   = spi_vfs_open,
    .close  = spi_vfs_close,
    .write  = spi_vfs_write,
    .read   = spi_vfs_read,
    .ioctl  = spi_vfs_ioctl,
};

static int spi_vfs_parse_dts(struct device* dev, struct spi_bus_client_config* cfg)
{
    hal_pin_t cs = {0};

    if (hal_pin_probe(dev, "cs-port", "cs-pin", &cs) != VFS_OK ||
        device_get_prop_int(dev, "spi-mode", &cfg->mode) != VFS_OK ||
        device_get_prop_int(dev, "spi-max-frequency", &cfg->clock_speed_hz) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    cfg->cs_pin = HAL_PIN_NUM(cs) | (HAL_PIN_PORT(cs) << 16);
    return VFS_OK;
}

int spi_vfs_probe(struct device* dev)
{
    struct spi_vfs_client* priv;
    struct spi_bus_client* bus_cli;
    int                    pool_idx;
    int                    ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_spi_vfs_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_spi_vfs_pool[pool_idx];
    memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_spi_vfs_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    ret = spi_vfs_parse_dts(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = spi_bus_client_register(dev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);

    priv->ops = spi_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        spi_bus_client_unregister(dev);
        goto err_mutex;
    }

    SYS_LOGI(kTag, "probe OK: %s mode=%d freq=%d", device_get_name(dev),
             priv->cfg.mode, priv->cfg.clock_speed_hz);
    return VFS_OK;

err_mutex:
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_spi_vfs_pool_ctrl, pool_idx);
    return ret;
}

int spi_vfs_remove(struct device* dev)
{
    struct spi_vfs_client* priv;
    struct dev_lifecycle*  lc;
    int                    pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
        return VFS_ERR_IO;

    spi_bus_client_unregister(dev);
    osal_mutex_destroy(priv->io_mutex);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_spi_vfs_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

int spi_vfs_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct spi_transfer_arg arg = {
        .tx = tx,
        .rx = rx,
        .len = len,
    };

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    return dev->ops->ioctl(dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

DRIVER_REGISTER(spi_vfs, "heterogeneous,w25q64-master", spi_vfs_probe, spi_vfs_remove)

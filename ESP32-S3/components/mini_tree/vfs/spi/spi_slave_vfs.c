/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI Slave VFS — 通过 spi_bus 层访问 ESP32 SPI slave
 */
#include "spi_slave_vfs.h"
#include "spi_vfs_drv.h"
#include "spi_bus.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "compiler_compat_poison.h"

#define SPI_SLAVE_VFS_COUNT DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE

struct spi_slave_vfs_client {
    struct file_operations       ops;
    struct spi_bus_client_config cfg;
    struct osal_mutex*           io_mutex;
    uint8_t                      mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                          pool_idx;
};

static struct spi_slave_vfs_client s_spi_slave_pool[SPI_SLAVE_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t                     s_spi_slave_used[SPI_SLAVE_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t                 s_spi_slave_pool_ctrl COMPAT_ALIGNED(4);

static const char* const kTag = "spi_slave_vfs";

pre_execution(160)
static void spi_slave_vfs_pool_boot_init(void)
{
    osal_pool_init(&s_spi_slave_pool_ctrl, s_spi_slave_used, SPI_SLAVE_VFS_COUNT);
}

static struct spi_slave_vfs_client* spi_slave_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct spi_slave_vfs_client, ops);
}

static int spi_slave_vfs_open(struct device* dev, void* arg)
{
    struct spi_slave_vfs_client* priv;
    struct dev_lifecycle*        lc;
    int                          first;
    int                          ret;

    (void)arg;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_slave_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = spi_bus_open(dev);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

static int spi_slave_vfs_close(struct device* dev)
{
    struct dev_lifecycle* lc;
    int                   last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
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

static int spi_slave_vfs_write(struct device* dev, const void* buffer,
                                size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = spi_bus_slave_sync(dev, (const uint8_t*)buffer, NULL, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_slave_vfs_read(struct device* dev, void* buffer,
                             size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = spi_bus_slave_sync(dev, NULL, (uint8_t*)buffer, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_slave_vfs_ioctl(struct device* dev, int cmd, void* arg,
                                size_t arg_len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int                   ret;

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
    case SPI_CMD_READ:
    {
        const struct spi_read_arg* ra = (const struct spi_read_arg*)arg;
        if (!ra || arg_len != sizeof(*ra) || !ra->data || ra->len == 0)
            ret = VFS_ERR_INVAL;
        else
            ret = spi_bus_slave_sync(dev, NULL, ra->data, ra->len, timeout_ms);
        break;
    }
    case SPI_CMD_QUEUE_TX:
    {
        const struct spi_queue_arg* qa = (const struct spi_queue_arg*)arg;
        if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
            ret = VFS_ERR_INVAL;
        else
            ret = spi_bus_slave_queue_tx(dev, qa->data, qa->len, timeout_ms);
        break;
    }
    case SPI_CMD_GET_TRANS_RESULT:
    {
        struct spi_trans_result_arg* tra = (struct spi_trans_result_arg*)arg;
        if (!tra || arg_len != sizeof(*tra))
            ret = VFS_ERR_INVAL;
        else
            ret = spi_bus_slave_get_trans_result(dev, tra->data, tra->len,
                                                  tra->trans_len, timeout_ms);
        break;
    }
    case SPI_CMD_DEINIT:
        COMPAT_IGNORE_RESULT(spi_bus_close(dev));
        ret = VFS_OK;
        break;
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations spi_slave_vfs_fops = {
    .open  = spi_slave_vfs_open,
    .close = spi_slave_vfs_close,
    .write = spi_slave_vfs_write,
    .read  = spi_slave_vfs_read,
    .ioctl = spi_slave_vfs_ioctl,
};

static int spi_slave_vfs_parse_dts(struct device* dev, struct spi_bus_client_config* cfg)
{
    hal_pin_t cs = {0};
    int       queue_size = -1;

    if (hal_pin_probe(dev, "cs-port", "cs-pin", &cs) != VFS_OK ||
        device_get_prop_int(dev, "spi-mode", &cfg->mode) != VFS_OK ||
        device_get_prop_int(dev, "spi-max-frequency", &cfg->clock_speed_hz) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "queue-size", &queue_size));

    cfg->cs_pin     = HAL_PIN_NUM(cs) | (HAL_PIN_PORT(cs) << 16);
    cfg->queue_size = queue_size;
    return VFS_OK;
}

int spi_slave_vfs_probe(struct device* dev)
{
    struct spi_slave_vfs_client* priv;
    struct spi_bus_client*       bus_cli;
    int                          pool_idx;
    int                          ret;

    if (!dev)
        return VFS_ERR_INVAL;

    if (spi_bus_child_host_role(dev) != SPI_BUS_ROLE_SLAVE)
    {
        SYS_LOGE(kTag, "SPI slave VFS requires SPI slave bus: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_spi_slave_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_spi_slave_pool[pool_idx];
    memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_spi_slave_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    ret = spi_slave_vfs_parse_dts(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = spi_bus_client_register(dev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);
    priv->ops = spi_slave_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        spi_bus_client_unregister(dev);
        goto err_mutex;
    }

    SYS_LOGI(kTag, "probe OK: %s mode=%d cs=%d:%d",
             device_get_name(dev), priv->cfg.mode,
             (priv->cfg.cs_pin >> 16) & 0xFFFF, priv->cfg.cs_pin & 0xFFFF);
    return VFS_OK;

err_mutex:
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_spi_slave_pool_ctrl, pool_idx);
    return ret;
}

int spi_slave_vfs_remove(struct device* dev)
{
    struct spi_slave_vfs_client* priv;
    struct dev_lifecycle*        lc;
    int                          pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_slave_vfs_from_ops(dev->ops);
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
    osal_pool_release(&s_spi_slave_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(spi_slave_vfs, "heterogeneous,fft-spi-slave",
                spi_slave_vfs_probe, spi_slave_vfs_remove)

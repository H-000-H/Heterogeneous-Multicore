/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART VFS 驱动
 *
 * 职责：
 *   - 实现 file_operations，对接 Device Model 生命周期
 *   - 所有实际 I/O 通过 uart_bus 层路由到 Controller / HAL
 */
#include "uart_vfs.h"
#include "uart_bus.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define UART_VFS_COUNT 2

struct uart_vfs_client {
    struct file_operations ops;
    struct osal_mutex*     io_mutex;
    uint8_t                mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                    pool_idx;
};

static struct uart_vfs_client s_uart_vfs_pool[UART_VFS_COUNT];
static uint8_t                s_uart_vfs_used[UART_VFS_COUNT];
static osal_pool_t            s_uart_vfs_pool_ctrl;
static const char* const      kTag = "uart_vfs";

pre_execution(160)
static void uart_vfs_pool_init(void)
{
    osal_pool_init(&s_uart_vfs_pool_ctrl, s_uart_vfs_used, UART_VFS_COUNT);
}

static struct uart_vfs_client* uart_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct uart_vfs_client, ops);
}

static int uart_vfs_open(struct device* dev, void* arg)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     first;

    (void)arg;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    if (first == 1)
    {
        if (uart_bus_open(dev) != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return VFS_ERR_IO;
        }
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

static int uart_vfs_close(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
        COMPAT_IGNORE_RESULT(uart_bus_close(dev));

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int uart_vfs_write(struct device* dev, const void* buf, size_t len,
                           uint32_t timeout_ms)
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

    ret = uart_bus_write(dev, (const uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int uart_vfs_read(struct device* dev, void* buf, size_t len,
                          uint32_t timeout_ms)
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

    ret = uart_bus_read(dev, (uint8_t*)buf, len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static int uart_vfs_ioctl(struct device* dev, int cmd, void* arg,
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
    case UART_CMD_TRANSFER:
    {
        struct uart_transfer_arg* t = (struct uart_transfer_arg*)arg;
        if (!t || arg_len != sizeof(*t) || !t->tx || !t->rx)
        {
            ret = VFS_ERR_INVAL;
            break;
        }
        ret = uart_bus_write(dev, t->tx, t->tx_len, timeout_ms);
        if (ret == VFS_OK && t->rx_len > 0)
            ret = uart_bus_read(dev, t->rx, t->rx_len, timeout_ms);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations uart_vfs_fops = {
    .open   = uart_vfs_open,
    .close  = uart_vfs_close,
    .write  = uart_vfs_write,
    .read   = uart_vfs_read,
    .ioctl  = uart_vfs_ioctl,
};

int uart_vfs_probe(struct device* dev)
{
    struct uart_vfs_client* priv;
    int                     pool_idx;
    int                     ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_uart_vfs_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_uart_vfs_pool[pool_idx];
    memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    ret = uart_bus_device_register(dev);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);

    priv->ops = uart_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        uart_bus_device_unregister(dev);
        goto err_mutex;
    }

    SYS_LOGI(kTag, "probe OK: %s", device_get_name(dev));
    return VFS_OK;

err_mutex:
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);
    return ret;
}

int uart_vfs_remove(struct device* dev)
{
    struct uart_vfs_client* priv;
    struct dev_lifecycle*   lc;
    int                     pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = uart_vfs_from_ops(dev->ops);
    lc   = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
        return VFS_ERR_IO;

    uart_bus_device_unregister(dev);
    osal_mutex_destroy(priv->io_mutex);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_uart_vfs_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(uart_vfs, "stm32,uart1", uart_vfs_probe, uart_vfs_remove)

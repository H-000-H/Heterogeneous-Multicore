/* SPDX-License-Identifier: GPL-2.0-or-later */
/*@=========================================================================================================================*
 * SPI VFS 实现 — SPI 总线子系统 VFS 层
 *
 * 三层结构:
 *   - Host VFS:       DTS 解析 + spi_bus_host_init (controller driver)
 *   - Master Client VFS: spi_bus_client_register + fops 挂载 (bus client driver)
 *   - Slave Client VFS:  spi_bus_slave_register + fops 挂载 (slave client driver)
 *
 * 生命周期管理 (dev_lifecycle):
 *   - open:  dev_lc_open_begin → first? bus_open : noop → dev_lc_open_end
 *   - close: dev_lc_close_begin → last? bus_close : noop → dev_lc_close_end
 *   - io:    dev_lc_io_begin → bus_transfer → dev_lc_io_end
 *   - remove: dev_lc_remove_start → ops_unregister → remove_drain → bus_unregister
 *
 * I/O 路径:
 *   - read/write: 走 spi_bus_read/write (legacy, 单工)
 *   - ioctl(SPI_CMD_TRANSFER): 走 spi_bus_transfer (全双工)
 *   - spi_vfs_transfer: 便捷 API, 封装 ioctl
 *
 * DTS 三层嵌套 (Linux 风格):
 *   spi@1 (esp32,spi-master)                          ← host controller
 *   └── spi-master@0 (heterogeneous,spi-master-client) ← bus client (spi_vfs_master)
 *       └── w25q64@0 (winbond,w25q64)                 ← leaf device (w25q64_spi)
 *
 *   w25q64_spi_probe: device_get_parent(dev) → client (有 fops) → spi_vfs_transfer
 *@=========================================================================================================================*/
#define SPI_VFS_IMPL
#include "spi_vfs.h"
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

#include <string.h>

/*===========================================================================================================================================================*/
                                                              /* Host VFS */
/*===========================================================================================================================================================*/
#define SPI_HOST_VFS_COUNT 4

struct spi_host_vfs {
    struct spi_bus_host_config  cfg;
    int                         pool_idx;
};

static struct spi_host_vfs s_host_pool[SPI_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_host_used[SPI_HOST_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_host_pool_ctrl COMPAT_ALIGNED(4);
static const char* const   kHostTag = "spi_vfs_host";

pre_execution(150)
static void spi_host_vfs_pool_init(void)
{
    osal_pool_init(&s_host_pool_ctrl, s_host_used, SPI_HOST_VFS_COUNT);
}

static int spi_host_vfs_parse_dts(struct device* dev,
                                  struct spi_bus_host_config* cfg,
                                  int bus_role)
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

    cfg->mosi_pin = HAL_PIN_NUM(mosi) | (HAL_PIN_PORT(mosi) << 16);
    cfg->miso_pin = HAL_PIN_NUM(miso) | (HAL_PIN_PORT(miso) << 16);
    cfg->sclk_pin = HAL_PIN_NUM(sclk) | (HAL_PIN_PORT(sclk) << 16);
    cfg->dma_chan = -1;
    cfg->max_transfer_sz = 0;
    cfg->bus_role = bus_role;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "dma-chan", &cfg->dma_chan));
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-trans-buffer", &cfg->max_transfer_sz));
    if (cfg->max_transfer_sz <= 0)
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-transfer-buffer", &cfg->max_transfer_sz));

    /* ceiling 由 spi_bus 层 clamp (HAL 静态缓冲区上限) */

    return VFS_OK;
}

static int spi_host_vfs_probe_impl(struct device* dev, int bus_role)
{
    struct spi_host_vfs* priv;
    int                  pool_idx;
    int                  ret;

    if (!dev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_host_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_host_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = spi_host_vfs_parse_dts(dev, &priv->cfg, bus_role);
    if (ret != VFS_OK)
        goto err_pool;

    ret = spi_bus_host_init(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(kHostTag, "probe OK: %s role=%s host=%d",
             device_get_name(dev),
             bus_role == SPI_BUS_ROLE_MASTER ? "master" : "slave",
             priv->cfg.host_id);
    return VFS_OK;

err_bus:
    spi_bus_host_deinit(dev);
err_pool:
    osal_pool_release(&s_host_pool_ctrl, pool_idx);
    return ret;
}

static int spi_host_vfs_probe_master(struct device* dev)
{
    return spi_host_vfs_probe_impl(dev, SPI_BUS_ROLE_MASTER);
}

static int spi_host_vfs_probe_slave(struct device* dev)
{
    return spi_host_vfs_probe_impl(dev, SPI_BUS_ROLE_SLAVE);
}

static int spi_host_vfs_remove(struct device* dev)
{
    struct spi_host_vfs* priv;
    int                  pool_idx;
    int                  ret;

    if (!dev)
        return VFS_ERR_INVAL;

    priv = (struct spi_host_vfs*)device_get_priv(dev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);

    pool_idx = priv->pool_idx;

    ret = spi_bus_host_deinit(dev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(kHostTag, "host remove busy: %s (ret=%d) — keeping resources",
                 device_get_name(dev), ret);
        return ret;
    }

    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_host_pool_ctrl, pool_idx);

    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Master Client VFS */
/*===========================================================================================================================================================*/
#define SPI_MASTER_VFS_COUNT 4

struct spi_master_vfs {
    struct file_operations       ops;
    struct spi_bus_client_config cfg;
    struct osal_mutex*           io_mutex;
    uint8_t                      mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                          pool_idx;
};

static struct spi_master_vfs s_master_pool[SPI_MASTER_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t               s_master_used[SPI_MASTER_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t           s_master_pool_ctrl COMPAT_ALIGNED(4);
static const char* const     kMasterTag = "spi_vfs_master";

pre_execution(160)
static void spi_master_vfs_pool_init(void)
{
    osal_pool_init(&s_master_pool_ctrl, s_master_used, SPI_MASTER_VFS_COUNT);
}

static struct spi_master_vfs* spi_master_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct spi_master_vfs, ops);
}

static int spi_master_vfs_open(struct device* dev, void* arg)
{
    struct spi_master_vfs* priv;
    struct dev_lifecycle*  lc;
    int                    first;

    (void)arg;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_master_vfs_from_ops(dev->ops);
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

static int spi_master_vfs_close(struct device* dev)
{
    struct spi_master_vfs* priv;
    struct dev_lifecycle*  lc;
    int                    last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_master_vfs_from_ops(dev->ops);
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

static int spi_master_vfs_write(struct device* dev, const void* buf, size_t len, uint32_t timeout_ms)
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

static int spi_master_vfs_read(struct device* dev, void* buf, size_t len, uint32_t timeout_ms)
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

static int spi_master_vfs_ioctl(struct device* dev, int cmd, void* arg,
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

static const struct file_operations spi_master_vfs_fops = {
    .open   = spi_master_vfs_open,
    .close  = spi_master_vfs_close,
    .write  = spi_master_vfs_write,
    .read   = spi_master_vfs_read,
    .ioctl  = spi_master_vfs_ioctl,
};

/*===========================================================================================================================================================*/
                                                              /* 便捷 API (上层驱动调用) */
/*===========================================================================================================================================================*/
int spi_vfs_transfer(struct device* dev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;

    if (!dev || len == 0)
        return VFS_ERR_INVAL;

    arg.tx  = tx;
    arg.rx  = rx;
    arg.len = len;

    return device_ioctl(dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

static int spi_master_vfs_parse_dts(struct device* dev, struct spi_bus_client_config* cfg)
{
    hal_pin_t cs = {0};

    if (hal_pin_probe(dev, "cs-port", "cs-pin", &cs) != VFS_OK ||
        device_get_prop_int(dev, "spi-mode", &cfg->mode) != VFS_OK ||
        device_get_prop_int(dev, "spi-max-frequency", &cfg->clock_speed_hz) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    cfg->cs_pin = HAL_PIN_NUM(cs) | (HAL_PIN_PORT(cs) << 16);
    cfg->queue_size = 0;
    return VFS_OK;
}

static int spi_master_vfs_probe(struct device* dev)
{
    struct spi_master_vfs*     priv;
    struct spi_bus_client*     bus_cli;
    int                        pool_idx;
    int                        ret;

    if (!dev)
        return VFS_ERR_INVAL;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_MASTER)
    {
        SYS_LOGE(kMasterTag, "requires SPI master bus: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_master_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_master_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_master_pool_ctrl, pool_idx);
        return VFS_ERR_NOMEM;
    }

    ret = spi_master_vfs_parse_dts(dev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_mutex;

    ret = spi_bus_client_register(dev, &priv->cfg, &bus_cli);
    if (ret != VFS_OK)
        goto err_mutex;

    device_lc_bind(dev, priv->io_mutex);

    priv->ops = spi_master_vfs_fops;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != VFS_OK)
    {
        spi_bus_client_unregister(dev);
        ret = VFS_ERR_IO;
        goto err_mutex;
    }

    SYS_LOGI(kMasterTag, "probe OK: %s mode=%d freq=%d", device_get_name(dev),
             priv->cfg.mode, priv->cfg.clock_speed_hz);
    return VFS_OK;

err_mutex:
    dev->ops = NULL;                   /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(dev));       /* 切断 io_lock 绑定 */
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_master_pool_ctrl, pool_idx);
    return ret;
}

static int spi_master_vfs_remove(struct device* dev)
{
    struct spi_master_vfs*     priv;
    struct dev_lifecycle*      lc;
    int                        pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = spi_master_vfs_from_ops(dev->ops);
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
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_master_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Slave Client VFS */
/*===========================================================================================================================================================*/
#define SPI_SLAVE_VFS_COUNT DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE

struct spi_slave_vfs {
    struct file_operations       ops;
    struct spi_bus_client_config cfg;
    struct osal_mutex*           io_mutex;
    uint8_t                      mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
    int                          pool_idx;
};

static struct spi_slave_vfs  s_slave_pool[SPI_SLAVE_VFS_COUNT] COMPAT_ALIGNED(4);
static uint8_t               s_slave_used[SPI_SLAVE_VFS_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t           s_slave_pool_ctrl COMPAT_ALIGNED(4);
static const char* const     kSlaveTag = "spi_vfs_slave";

pre_execution(160)
static void spi_slave_vfs_pool_init(void)
{
    osal_pool_init(&s_slave_pool_ctrl, s_slave_used, SPI_SLAVE_VFS_COUNT);
}

static struct spi_slave_vfs* spi_slave_vfs_from_ops(const struct file_operations* ops)
{
    return container_of(ops, struct spi_slave_vfs, ops);
}

static int spi_slave_vfs_open(struct device* dev, void* arg)
{
    struct spi_slave_vfs* priv;
    struct dev_lifecycle* lc;
    int                   first;
    int                   ret;

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
    case SPI_CMD_QUEUE_TX:
    {
        struct spi_queue_arg* qa = (struct spi_queue_arg*)arg;
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
    struct spi_slave_vfs*  priv;
    struct spi_bus_client* bus_cli;
    int                    pool_idx;
    int                    ret;

    if (!dev)
        return VFS_ERR_INVAL;

    if (spi_bus_host_role(dev) != SPI_BUS_ROLE_SLAVE)
    {
        SYS_LOGE(kSlaveTag, "requires SPI slave bus: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    pool_idx = osal_pool_claim(&s_slave_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_slave_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    if (osal_mutex_create_static(&priv->io_mutex, priv->mutex_storage,
                                  sizeof(priv->mutex_storage)) != 0)
    {
        osal_pool_release(&s_slave_pool_ctrl, pool_idx);
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
        ret = VFS_ERR_IO;
        goto err_mutex;
    }

    SYS_LOGI(kSlaveTag, "probe OK: %s mode=%d cs=%d:%d",
             device_get_name(dev), priv->cfg.mode,
             (priv->cfg.cs_pin >> 16) & 0xFFFF, priv->cfg.cs_pin & 0xFFFF);
    return VFS_OK;

err_mutex:
    dev->ops = NULL;                   /* 切断 fops, 防 UAF */
    dev_lc_reset(device_lc(dev));       /* 切断 io_lock 绑定 */
    osal_mutex_destroy(priv->io_mutex);
    osal_pool_release(&s_slave_pool_ctrl, pool_idx);
    return ret;
}

int spi_slave_vfs_remove(struct device* dev)
{
    struct spi_slave_vfs*  priv;
    struct dev_lifecycle*  lc;
    int                    pool_idx;

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
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_slave_pool_ctrl, pool_idx);

    dev_lc_remove_finish(lc);
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                              /* Driver Registration */
/*===========================================================================================================================================================*/
DRIVER_REGISTER(spi_host_esp32_master, "esp32,spi-master",
                spi_host_vfs_probe_master, spi_host_vfs_remove)
DRIVER_REGISTER(spi_host_esp32_slave, "esp32,spi",
                spi_host_vfs_probe_slave, spi_host_vfs_remove)
DRIVER_REGISTER(spi_vfs_master, "heterogeneous,spi-master-client",
                spi_master_vfs_probe, spi_master_vfs_remove)
DRIVER_REGISTER(spi_vfs_slave, "heterogeneous,fft-spi-slave",
                spi_slave_vfs_probe, spi_slave_vfs_remove)
/*===========================================================================================================================================================*/

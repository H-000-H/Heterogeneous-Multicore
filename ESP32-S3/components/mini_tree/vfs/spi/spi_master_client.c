#include "spi_master_client.h"
#include "spi_vfs_drv.h"
#include "bus.h"
#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "device.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "dev_lifecycle.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"

#include <stddef.h>
#include <stdint.h>

#define SPI_MASTER_CLIENT_COUNT DTC_GEN_COUNT_HETEROGENEOUS_W25Q64_MASTER

struct spi_master_client
{
    struct file_operations ops;
    struct hal_spi_ctx     ctx;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

static struct spi_master_client s_spi_master_pool[SPI_MASTER_CLIENT_COUNT];
static uint8_t                  s_spi_master_used[SPI_MASTER_CLIENT_COUNT];
static osal_pool_t              s_spi_master_pool_ctrl;
static uint8_t s_spi_master_mutex_storage[SPI_MASTER_CLIENT_COUNT][OSAL_MUTEX_STORAGE_SIZE];

pre_execution(160)
static void spi_master_client_pool_boot_init(void)
{
    osal_pool_init(&s_spi_master_pool_ctrl, s_spi_master_used, SPI_MASTER_CLIENT_COUNT);
}

static const char* const kTag = "spi_master_client";

/* unlock 失败且 I/O 成功时上报 VFS_ERR_IO */
static int spi_xfer_session_end(struct hal_spi_ctx* ctx, int io_ret)
{
    int end_ret = hal_spi_xfer_end(ctx);
    if (end_ret != VFS_OK && io_ret == VFS_OK)
        return VFS_ERR_IO;
    return io_ret;
}

static int spi_master_open(struct device* pdev, void* arg)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_master_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = hal_spi_interface_attach(&priv->ctx);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
        else
            priv->ctx.attached = 1;
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

static int spi_master_close(struct device* pdev)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_master_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last && priv->ctx.attached)
    {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(&priv->ctx));
        priv->ctx.attached = 0;
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int spi_master_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_master_client, ops);
    lc = device_lc(pdev);
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

    ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
    if (ret == VFS_OK)
    {
        int write_bytes = priv->ctx.host->bus.write(&priv->ctx.host->bus,
                                                    (const uint8_t*)buffer, len);
        if (write_bytes > 0)
            ret = VFS_OK;
        else
            ret = VFS_ERR_IO;

        ret = spi_xfer_session_end(&priv->ctx, ret);
    }

    dev_lc_io_end(lc);
    return ret;
}

static int spi_master_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_master_client, ops);
    lc = device_lc(pdev);
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

    ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
    if (ret == VFS_OK)
    {
        ret = priv->ctx.host->bus.read(&priv->ctx.host->bus, (uint8_t*)buffer, len);
        ret = spi_xfer_session_end(&priv->ctx, ret);
    }

    dev_lc_io_end(lc);
    return ret;
}

static int spi_master_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_master_client, ops);
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
        {
            ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
            if (ret == VFS_OK)
            {
                ret = priv->ctx.host->bus.read(&priv->ctx.host->bus, ra->data, ra->len);
                ret = spi_xfer_session_end(&priv->ctx, ret);
            }
        }
        break;
    }
    case SPI_CMD_QUEUE_TX:
    {
        const struct spi_queue_arg* qa = (const struct spi_queue_arg*)arg;
        if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
            ret = VFS_ERR_INVAL;
        else if (!priv->ctx.host->bus.write_top_half)
            ret = VFS_ERR_IO;
        else
        {
            ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
            if (ret == VFS_OK)
            {
                ret = priv->ctx.host->bus.write_top_half(&priv->ctx.host->bus,
                                                         qa->data, qa->len);
                ret = spi_xfer_session_end(&priv->ctx, ret);
            }
        }
        break;
    }
    case SPI_CMD_GET_TRANS_RESULT:
    {
        struct spi_trans_result_arg* tra = (struct spi_trans_result_arg*)arg;
        if (!tra || arg_len != sizeof(*tra))
            ret = VFS_ERR_INVAL;
        else
            ret = hal_spi_get_trans_result(&priv->ctx, tra->data, tra->len,
                                           tra->trans_len, timeout_ms);
        break;
    }
    case SPI_CMD_DEINIT:
        if (priv->ctx.attached)
        {
            COMPAT_IGNORE_RESULT(hal_spi_interface_detach(&priv->ctx));
            priv->ctx.attached = 0;
        }
        ret = VFS_OK;
        break;
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations spi_master_fops =
{
    .open  = spi_master_open,
    .close = spi_master_close,
    .write = spi_master_write,
    .read  = spi_master_read,
    .ioctl = spi_master_ioctl,
};

int spi_master_client_probe(struct device* pdev)
{
    struct spi_master_client* priv;
    struct bus_controller* ctrl;
    int cs = -1, mode = -1, clock_speed_hz = -1, queue_size = -1;
    struct hal_spi_device_config dev_cfg;
    struct hal_spi_bus_host* bus_host;
    int pool_idx;
    int ret;

    ret = bus_controller_of(pdev, &ctrl);
    if (ret != VFS_OK || !ctrl->hw_priv)
    {
        SYS_LOGE(kTag, "parent bus controller not ready: %s", device_get_name(pdev));
        return ret != VFS_OK ? ret : VFS_ERR_IO;
    }

    bus_host = (struct hal_spi_bus_host*)ctrl->hw_priv;

    if (device_get_prop_int(pdev, "cs-pin", &cs) ||
        device_get_prop_int(pdev, "spi-mode", &mode) ||
        device_get_prop_int(pdev, "spi-max-frequency", &clock_speed_hz) ||
        device_get_prop_int(pdev, "queue-size", &queue_size))
        goto err_prop;

    pool_idx = osal_pool_claim(&s_spi_master_pool_ctrl);
    if (pool_idx < 0)
    {
        SYS_LOGE(kTag, "Failed to claim SPI client pool");
        return VFS_ERR_NOMEM;
    }

    priv = &s_spi_master_pool[pool_idx];
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    dev_cfg.mode           = mode;
    dev_cfg.clock_speed_hz = clock_speed_hz;
    dev_cfg.cs_pin         = cs;
    dev_cfg.queue_size     = queue_size;

    hal_spi_ctx_init(&priv->ctx, pool_idx, bus_host, &dev_cfg);
    hal_spi_ctx_attach(&priv->ctx);

    if (osal_mutex_create_static(&priv->io_mutex, s_spi_master_mutex_storage[pool_idx],
                                 sizeof(s_spi_master_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(pdev, priv->io_mutex);
    priv->ops = spi_master_fops;
    pdev->ops  = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
        goto err_pool;

    COMPAT_IGNORE_RESULT(bus_client_bind(pdev, ctrl->dev, priv));

    SYS_LOGI(kTag, "client probe OK: cs=%d mode=%d", cs, mode);
    return VFS_OK;

err_pool:
    hal_spi_ctx_detach(&priv->ctx);
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_spi_master_pool_ctrl, pool_idx);
    return VFS_ERR_IO;

err_prop:
    SYS_LOGE(kTag, "Failed to get property: %s", device_get_name(pdev));
    return VFS_ERR_INVAL;
}

int spi_master_client_remove(struct device* pdev)
{
    struct spi_master_client* priv;
    struct dev_lifecycle* lc;
    struct osal_mutex* io_mutex;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_master_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    io_mutex = priv->io_mutex;
    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    bus_client_unbind(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        SYS_LOGE(kTag, "remove drain failed");
        return VFS_ERR_IO;
    }

    if (priv->ctx.attached)
    {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(&priv->ctx));
        priv->ctx.attached = 0;
    }

    hal_spi_ctx_detach(&priv->ctx);
    osal_mutex_destroy(io_mutex);
    __builtin_memset(priv, 0, sizeof(*priv));
    osal_pool_release(&s_spi_master_pool_ctrl, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

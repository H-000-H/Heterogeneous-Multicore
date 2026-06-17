#include "spi_client.h"
#include "spi_vfs.h"
#include "bus.h"
#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "dev_lifecycle.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SPI_CLIENT_COUNT DTC_GEN_COUNT_STM32_SPI_DEVICE

struct spi_client {
    struct file_operations ops;
    struct hal_spi_ctx     ctx;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

static struct spi_client s_spi_client_pool[SPI_CLIENT_COUNT];
static uint8_t           s_spi_client_used[SPI_CLIENT_COUNT];
static uint8_t           s_spi_mutex_storage[SPI_CLIENT_COUNT][OSAL_MUTEX_STORAGE_SIZE];

static const char* const kTag = "spi_client";

struct hal_spi_bus* device_get_spi_bus(struct device* pdev)
{
    struct spi_client* priv;

    if (!pdev || !pdev->ops)
        return NULL;

    priv = container_of(pdev->ops, struct spi_client, ops);
    if (!priv->ctx.pool_idx)
        return NULL;

    return &priv->ctx.host->bus;
}

static int spi_open(struct device* pdev, void* arg)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_client, ops);
    lc = device_lc(pdev);
    if (!lc)
        return VFS_ERR_INVAL;

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1) {
        ret = hal_spi_interface_attach(priv->ctx.host, &priv->ctx.cfg);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
        else
            priv->ctx.attached = 1;
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

static int spi_close(struct device* pdev)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_client, ops);
    lc = device_lc(pdev);
    if (!lc)
        return VFS_ERR_INVAL;

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last && priv->ctx.attached) {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(priv->ctx.host));
        priv->ctx.attached = 0;
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int spi_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_client, ops);
    lc = device_lc(pdev);
    if (!lc)
        return VFS_ERR_INVAL;

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0) {
        dev_lc_io_end(lc);
        return 0;
    }
    if (!buffer) {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
    if (ret == VFS_OK) {
        int write_bytes = priv->ctx.host->bus.write(&priv->ctx.host->bus,
                                                    (const uint8_t*)buffer, len);
        if (write_bytes > 0)
            ret = VFS_OK;
        else
            ret = VFS_ERR_IO;

        COMPAT_IGNORE_RESULT(hal_spi_xfer_end(&priv->ctx));
    }

    dev_lc_io_end(lc);
    return ret;
}

static int spi_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct spi_client, ops);
    lc = device_lc(pdev);
    if (!lc)
        return VFS_ERR_INVAL;

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0) {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buffer) {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
    if (ret == VFS_OK)
        ret = priv->ctx.host->bus.read(&priv->ctx.host->bus, (uint8_t*)buffer, len);

    COMPAT_IGNORE_RESULT(hal_spi_xfer_end(&priv->ctx));
    dev_lc_io_end(lc);
    return ret;
}

static int spi_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    int ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_client, ops);
    lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd) {
    case SPI_CMD_READ: {
        const struct spi_read_arg* ra = (const struct spi_read_arg*)arg;
        if (!ra || arg_len != sizeof(*ra) || !ra->data || ra->len == 0)
            ret = VFS_ERR_INVAL;
        else {
            ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
            if (ret == VFS_OK)
                ret = priv->ctx.host->bus.read(&priv->ctx.host->bus, ra->data, ra->len);
            COMPAT_IGNORE_RESULT(hal_spi_xfer_end(&priv->ctx));
        }
        break;
    }
    case SPI_CMD_QUEUE_TX: {
        const struct spi_queue_arg* qa = (const struct spi_queue_arg*)arg;
        if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
            ret = VFS_ERR_INVAL;
        else if (!priv->ctx.host->bus.write_top_half)
            ret = VFS_ERR_IO;
        else {
            ret = hal_spi_xfer_begin(&priv->ctx, timeout_ms);
            if (ret == VFS_OK)
                ret = priv->ctx.host->bus.write_top_half(&priv->ctx.host->bus,
                                                         qa->data, qa->len);
            COMPAT_IGNORE_RESULT(hal_spi_xfer_end(&priv->ctx));
        }
        break;
    }
    case SPI_CMD_GET_TRANS_RESULT: {
        struct spi_trans_result_arg* tra = (struct spi_trans_result_arg*)arg;
        if (!tra || arg_len != sizeof(*tra))
            ret = VFS_ERR_INVAL;
        else
            ret = hal_spi_get_trans_result(&priv->ctx, tra->data, tra->len,
                                           tra->trans_len, timeout_ms);
        break;
    }
    case SPI_CMD_DEINIT:
        if (priv->ctx.attached) {
            COMPAT_IGNORE_RESULT(hal_spi_interface_detach(priv->ctx.host));
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

static const struct file_operations spi_operations_template = {
    .open  = spi_open,
    .close = spi_close,
    .write = spi_write,
    .read  = spi_read,
    .ioctl = spi_ioctl,
};

int spi_client_probe(struct device* dev)
{
    struct spi_client* priv;
    struct bus_controller* ctrl;
    int cs = -1, mode = -1, clock_speed_hz = -1, queue_size = -1;
    struct hal_spi_device_config dev_cfg;
    struct hal_spi_bus_host* bus_host;
    int pool_idx;

    ctrl = bus_controller_of(dev);
    if (!ctrl || !ctrl->hw_priv) {
        SYS_LOGE(kTag, "parent bus controller not ready: %s", device_get_name(dev));
        return VFS_ERR_IO;
    }

    bus_host = (struct hal_spi_bus_host*)ctrl->hw_priv;

    if (device_get_prop_int(dev, "spi-mode", &mode) ||
        device_get_prop_int(dev, "spi-max-frequency", &clock_speed_hz) ||
        device_get_prop_int(dev, "queue-size", &queue_size))
        goto err_prop;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "cs-pin", &cs));

    pool_idx = osal_pool_claim(s_spi_client_used, SPI_CLIENT_COUNT);
    if (pool_idx < 0) {
        SYS_LOGE(kTag, "Failed to claim SPI client pool");
        return VFS_ERR_NOMEM;
    }

    priv = &s_spi_client_pool[pool_idx];
    memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    dev_cfg.mode           = mode;
    dev_cfg.clock_speed_hz = clock_speed_hz;
    dev_cfg.cs_pin         = cs;
    dev_cfg.queue_size     = queue_size;

    hal_spi_ctx_init(&priv->ctx, pool_idx, bus_host, &dev_cfg);
    hal_spi_ctx_attach(&priv->ctx);

    if (osal_mutex_create_static(&priv->io_mutex, s_spi_mutex_storage[pool_idx],
                                 sizeof(s_spi_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(dev, priv->io_mutex);
    priv->ops = spi_operations_template;
    dev->ops  = &priv->ops;

    if (device_set_priv(dev, priv) != 0)
        goto err_pool;

    COMPAT_IGNORE_RESULT(bus_client_bind(dev, ctrl->dev, priv));

    SYS_LOGI(kTag, "client probe OK: cs=%d mode=%d", cs, mode);
    return VFS_OK;

err_pool:
    hal_spi_ctx_detach(&priv->ctx);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(s_spi_client_used, SPI_CLIENT_COUNT, pool_idx);
    return VFS_ERR_IO;

err_prop:
    SYS_LOGE(kTag, "Failed to get property: %s", device_get_name(dev));
    return VFS_ERR_INVAL;
}

int spi_client_remove(struct device* dev)
{
    struct spi_client* priv;
    struct dev_lifecycle* lc;
    struct osal_mutex* io_mutex;
    int pool_idx;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(dev->ops, struct spi_client, ops);
    lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    io_mutex = priv->io_mutex;
    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    bus_client_unbind(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK) {
        SYS_LOGE(kTag, "remove drain failed");
        return VFS_ERR_IO;
    }

    if (priv->ctx.attached) {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(priv->ctx.host));
        priv->ctx.attached = 0;
    }

    hal_spi_ctx_detach(&priv->ctx);
    osal_mutex_destroy(io_mutex);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(s_spi_client_used, SPI_CLIENT_COUNT, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

static int stm32_spi_device_probe(struct device* dev)
{
    return spi_client_probe(dev);
}

static int stm32_spi_device_remove(struct device* dev)
{
    return spi_client_remove(dev);
}

DRIVER_REGISTER(spi_client, "stm32,spi-device", stm32_spi_device_probe, stm32_spi_device_remove)

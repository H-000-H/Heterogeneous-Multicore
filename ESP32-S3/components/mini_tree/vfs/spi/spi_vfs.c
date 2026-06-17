#include "spi_vfs.h"
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "system_log.h"

#define SPI_DEVICE_COUNT DTC_GEN_COUNT_ESP32_SPI_DEVICE

struct spi_vfs_priv
{
    struct file_operations ops;
    struct hal_spi_ctx     hal;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

static struct spi_vfs_priv s_spi_vfs_pool[SPI_DEVICE_COUNT];
static uint8_t s_spi_vfs_used[SPI_DEVICE_COUNT];
static uint8_t s_spi_mutex_storage[SPI_DEVICE_COUNT][OSAL_MUTEX_STORAGE_SIZE];

static const char* const kTag = "spi_vfs";

struct hal_spi_bus* device_get_spi_bus(struct device* dev)
{
    if (!dev || !dev->ops)
        return NULL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);
    if (!priv->hal.host)
        return NULL;
    return &priv->hal.host->bus;
}

static int spi_open(struct device* dev, void* arg)
{
    (void)arg;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    int first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    int ret = VFS_OK;
    if (first) {
        ret = hal_spi_interface_attach(priv->hal.host, &priv->hal.cfg);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
        else
            priv->hal.attached = 1;
    }

    if (ret == VFS_OK)
        dev_lc_open_end(lc);

    return ret;
}

static int spi_close(struct device* dev)
{
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    int last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last && priv->hal.attached) {
        (void)hal_spi_interface_detach(priv->hal.host);
        priv->hal.attached = 0;
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int spi_write(struct device* dev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    int ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
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

    ret = hal_spi_xfer_begin(&priv->hal, timeout_ms);
    if (ret == VFS_OK)
        ret = priv->hal.host->bus.write(&priv->hal.host->bus, (const uint8_t*)buffer, len);
    (void)hal_spi_xfer_end(&priv->hal);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_read(struct device* dev, void* buffer, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    int ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
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

    ret = hal_spi_xfer_begin(&priv->hal, timeout_ms);
    if (ret == VFS_OK)
        ret = priv->hal.host->bus.read(&priv->hal.host->bus, (uint8_t*)buffer, len);
    (void)hal_spi_xfer_end(&priv->hal);

    dev_lc_io_end(lc);
    return ret;
}

static int spi_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    int ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd) {
    case SPI_CMD_READ: {
        const struct spi_read_arg* ra = (const struct spi_read_arg*)arg;
        if (!ra || arg_len != sizeof(*ra) || !ra->data || ra->len == 0)
            ret = VFS_ERR_INVAL;
        else {
            ret = hal_spi_xfer_begin(&priv->hal, timeout_ms);
            if (ret == VFS_OK)
                ret = priv->hal.host->bus.read(&priv->hal.host->bus, ra->data, ra->len);
            (void)hal_spi_xfer_end(&priv->hal);
        }
        break;
    }
    case SPI_CMD_QUEUE_TX: {
        const struct spi_queue_arg* qa = (const struct spi_queue_arg*)arg;
        if (!qa || arg_len != sizeof(*qa) || !qa->data || qa->len == 0)
            ret = VFS_ERR_INVAL;
        else if (!priv->hal.host->bus.write_top_half)
            ret = VFS_ERR_IO;
        else {
            ret = hal_spi_xfer_begin(&priv->hal, timeout_ms);
            if (ret == VFS_OK)
                ret = priv->hal.host->bus.write_top_half(&priv->hal.host->bus, qa->data, qa->len);
            (void)hal_spi_xfer_end(&priv->hal);
        }
        break;
    }
    case SPI_CMD_GET_TRANS_RESULT: {
        struct spi_trans_result_arg* tra = (struct spi_trans_result_arg*)arg;
        if (!tra || arg_len != sizeof(*tra))
            ret = VFS_ERR_INVAL;
        else
            ret = hal_spi_get_trans_result(&priv->hal, tra->data, tra->len,
                                           tra->trans_len, timeout_ms);
        break;
    }
    case SPI_CMD_DEINIT:
        if (priv->hal.attached) {
            (void)hal_spi_interface_detach(priv->hal.host);
            priv->hal.attached = 0;
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
    .open = spi_open,
    .close = spi_close,
    .write = spi_write,
    .read = spi_read,
    .ioctl = spi_ioctl,
};

static int spi_probe(struct device* dev)
{
    struct spi_vfs_priv* priv;
    struct device* parent;
    int host = -1, clock_speed_hz = -1, cs = -1, mode = -1, queue_size = -1;
    struct hal_spi_device_config dev_cfg;
    struct hal_spi_bus_host* bus_host;

    parent = device_get_parent(dev);
    if (!parent || device_get_prop_int(parent, "host-id", &host))
        goto err_prop;

    bus_host = hal_spi_bus_host_get(host);
    if (!bus_host) {
        SYS_LOGE(kTag, "parent host %d not probed", host);
        return VFS_ERR_IO;
    }

    if (device_get_prop_int(dev, "cs-pin", &cs) ||
        device_get_prop_int(dev, "spi-mode", &mode) ||
        device_get_prop_int(dev, "spi-max-frequency", &clock_speed_hz) ||
        device_get_prop_int(dev, "queue-size", &queue_size))
        goto err_prop;

    int pool_idx = osal_pool_claim(s_spi_vfs_used, SPI_DEVICE_COUNT);
    if (pool_idx < 0) {
        SYS_LOGE(kTag, "Failed to claim SPI VFS pool");
        return VFS_ERR_NOMEM;
    }

    priv = &s_spi_vfs_pool[pool_idx];
    memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    dev_cfg.mode = mode;
    dev_cfg.clock_speed_hz = clock_speed_hz;
    dev_cfg.cs_pin = cs;
    dev_cfg.queue_size = queue_size;

    hal_spi_ctx_init(&priv->hal, pool_idx, bus_host, &dev_cfg);
    hal_spi_ctx_attach(&priv->hal);

    if (osal_mutex_create_static(&priv->io_mutex, s_spi_mutex_storage[pool_idx],
                                 sizeof(s_spi_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(dev, priv->io_mutex);
    priv->ops = spi_operations_template;
    dev->ops = &priv->ops;
    if (device_set_priv(dev, priv) != 0)
        goto err_pool;

    SYS_LOGI(kTag, "device probe OK: host=%d cs=%d mode=%d",
             host, cs, mode);
    return VFS_OK;

err_pool:
    hal_spi_ctx_detach(&priv->hal);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(s_spi_vfs_used, SPI_DEVICE_COUNT, pool_idx);
    return VFS_ERR_IO;

err_prop:
    SYS_LOGE(kTag, "Failed to get property: %s", device_get_name(dev));
    return VFS_ERR_INVAL;
}

static int spi_remove(struct device* dev)
{
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    struct spi_vfs_priv* priv = container_of(dev->ops, struct spi_vfs_priv, ops);

    struct dev_lifecycle* lc = device_lc(dev);
    if (!lc)
        return VFS_ERR_INVAL;

    struct osal_mutex* io_mutex = priv->io_mutex;
    int pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    int ret = dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER);
    if (ret != VFS_OK) {
        SYS_LOGE(kTag, "remove drain failed ret=%d", ret);
        return ret;
    }

    if (priv->hal.attached) {
        (void)hal_spi_interface_detach(priv->hal.host);
        priv->hal.attached = 0;
    }

    hal_spi_ctx_detach(&priv->hal);
    osal_mutex_destroy(io_mutex);
    memset(priv, 0, sizeof(*priv));
    osal_pool_release(s_spi_vfs_used, SPI_DEVICE_COUNT, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(spi, "esp32,spi-device", spi_probe, spi_remove)

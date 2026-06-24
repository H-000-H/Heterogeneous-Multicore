/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART Bus Framework — 总线层实现
 *
 * UART 传输的唯一调度中心：锁仲裁、DMA/轮询选择、调用 uart_hal。
 */
#include "uart_bus.h"
#include "hal_uart.h"
#include "bus_controller.h"
#include "device.h"
#include "driver.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include <string.h>

#define UART_BUS_HOST_MAX 4

struct uart_bus_host {
    struct device*             dev;
    struct hal_uart_dev        hal_dev;
    const struct hal_uart_bus* vtable;
    struct osal_mutex*         bus_mutex;
    int                        ref_count;
    uint8_t                    in_use;
    uint8_t                    mutex_storage[OSAL_MUTEX_STORAGE_SIZE];
};

static struct uart_bus_host s_uart_hosts[UART_BUS_HOST_MAX];
static const char* const    kTag = "uart_bus";

static int uart_host_pool_claim(void)
{
    for (int i = 0; i < UART_BUS_HOST_MAX; i++)
    {
        if (!s_uart_hosts[i].in_use)
        {
            s_uart_hosts[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void uart_host_pool_release(int idx)
{
    if (idx >= 0 && idx < UART_BUS_HOST_MAX)
        memset(&s_uart_hosts[idx], 0, sizeof(s_uart_hosts[idx]));
}

static struct uart_bus_host* uart_host_from_device(struct device* dev)
{
    for (int i = 0; i < UART_BUS_HOST_MAX; i++)
    {
        if (s_uart_hosts[i].in_use && s_uart_hosts[i].dev == dev)
            return &s_uart_hosts[i];
    }
    return NULL;
}

static int uart_bus_parse_dts(struct device* dev, struct hal_uart_config_t* cfg)
{
    hal_pin_t tx = {0};
    hal_pin_t rx = {0};
    int       host_id = 0;
    int       hw_instance = 0;
    int       data_bits = 0;
    int       stop_bits = 0;
    int       parity = 0;

    if (device_get_prop_int(dev, "host-id", &host_id) != VFS_OK ||
        hal_pin_probe(dev, "tx-port", "tx-pin", &tx) != VFS_OK ||
        hal_pin_probe(dev, "rx-port", "rx-pin", &rx) != VFS_OK ||
        device_get_prop_int(dev, "uart-trans-baund", (int*)&cfg->baud_rate) != VFS_OK ||
        device_get_prop_int(dev, "data-bit", &data_bits) != VFS_OK ||
        device_get_prop_int(dev, "stop-bit", &stop_bits) != VFS_OK ||
        device_get_prop_int(dev, "parity", &parity) != VFS_OK)
    {
        return VFS_ERR_INVAL;
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "hw-instance", &hw_instance));
    if (hw_instance <= 0)
        hw_instance = host_id;

    cfg->data_bits = (hal_uart_data_bits_t)data_bits;
    cfg->stop_bits = (hal_uart_stop_bits_t)stop_bits;
    cfg->parity    = (hal_uart_parity_t)parity;
    cfg->tx_io     = tx;
    cfg->rx_io     = rx;
    cfg->uart_host = hw_instance;

    (void)host_id;
    return VFS_OK;
}

static int uart_bus_host_transfer(struct bus_controller* ctlr,
                                   struct bus_client* cli,
                                   const void* tx, void* rx,
                                   size_t len, uint32_t timeout_ms)
{
    (void)ctlr;
    (void)cli;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_INVAL;
}

static const struct bus_controller_ops uart_bus_controller_ops = {
    .transfer = uart_bus_host_transfer,
};

int uart_bus_host_probe(struct device* dev)
{
    struct uart_bus_host*      host;
    struct hal_uart_config_t   cfg = {0};
    int                        idx;
    int                        ret;

    if (!dev)
        return VFS_ERR_INVAL;

    if (uart_bus_parse_dts(dev, &cfg) != VFS_OK)
    {
        SYS_LOGE(kTag, "dts parse failed: %s", device_get_name(dev));
        return VFS_ERR_INVAL;
    }

    idx = uart_host_pool_claim();
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_uart_hosts[idx];
    host->dev     = dev;
    host->vtable  = hal_uart_bus_get();

    /* 嵌入的 hal_dev 必须在 cfg 之前被清零，因为 HAL 用 container_of */
    memset(&host->hal_dev, 0, sizeof(host->hal_dev));
    host->hal_dev.cfg       = cfg;
    host->hal_dev.pool_idx  = cfg.uart_host;
    host->hal_dev.status    = UART_STATE_UNINIT;

    if (osal_mutex_create_static(&host->bus_mutex, host->mutex_storage,
                                  sizeof(host->mutex_storage)) != 0)
    {
        uart_host_pool_release(idx);
        return VFS_ERR_NOMEM;
    }

    ret = bus_controller_register(dev, BUS_CONTROLLER_UART, &uart_bus_controller_ops, host);
    if (ret != VFS_OK)
        goto err_mutex_destroy;

    if (device_set_priv(dev, host) != VFS_OK)
    {
        bus_controller_unregister(dev);
        goto err_mutex_destroy;
    }

    SYS_LOGI(kTag, "host probe OK: %s uart_host=%d baud=%lu",
             device_get_name(dev), cfg.uart_host, (unsigned long)cfg.baud_rate);
    return VFS_OK;

err_mutex_destroy:
    osal_mutex_destroy(host->bus_mutex);
    uart_host_pool_release(idx);
    return ret;
}

int uart_bus_host_remove(struct device* dev)
{
    struct uart_bus_host* host;

    if (!dev)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    bus_controller_unregister(dev);

    if (host->vtable && host->vtable->deinit)
        COMPAT_IGNORE_RESULT(host->vtable->deinit(&host->hal_dev.cfg));

    osal_mutex_destroy(host->bus_mutex);
    uart_host_pool_release((int)(host - s_uart_hosts));

    return VFS_OK;
}

int uart_bus_device_register(struct device* dev)
{
    struct uart_bus_host* host;
    int                   ret;

    if (!dev)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (!host->vtable || !host->vtable->open)
        return VFS_ERR_IO;

    ret = host->vtable->open(&host->hal_dev.cfg);
    if (ret != VFS_OK)
        return ret;

    return VFS_OK;
}

void uart_bus_device_unregister(struct device* dev)
{
    struct uart_bus_host* host;

    host = uart_host_from_device(dev);
    if (!host)
        return;

    if (host->vtable && host->vtable->close)
        COMPAT_IGNORE_RESULT(host->vtable->close(&host->hal_dev.cfg));
}

int uart_bus_open(struct device* dev)
{
    struct uart_bus_host* host;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    host->ref_count++;
    return VFS_OK;
}

int uart_bus_close(struct device* dev)
{
    struct uart_bus_host* host;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (host->ref_count > 0)
        host->ref_count--;

    return VFS_OK;
}

int uart_bus_write(struct device* dev,
                    const uint8_t* data, size_t len,
                    uint32_t timeout_ms)
{
    struct uart_bus_host* host;
    int                   ret;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (osal_mutex_lock(host->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    if (host->vtable && host->vtable->write)
        ret = host->vtable->write(&host->hal_dev, data, len);
    else
        ret = VFS_ERR_IO;

    if (osal_mutex_unlock(host->bus_mutex) != 0 && ret == VFS_OK)
        ret = VFS_ERR_IO;
    return ret;
}

int uart_bus_read(struct device* dev,
                   uint8_t* data, size_t len,
                   uint32_t timeout_ms)
{
    struct uart_bus_host* host;
    int                   ret;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    host = uart_host_from_device(dev);
    if (!host)
        return VFS_ERR_NODEV;

    if (osal_mutex_lock(host->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    if (host->vtable && host->vtable->read)
        ret = host->vtable->read(&host->hal_dev, data, len);
    else
        ret = VFS_ERR_IO;

    if (osal_mutex_unlock(host->bus_mutex) != 0 && ret == VFS_OK)
        ret = VFS_ERR_IO;
    return ret;
}

DRIVER_REGISTER(uart_bus_host, "stm32,uart1", uart_bus_host_probe, uart_bus_host_remove)
DRIVER_REGISTER(uart_bus_host_ch32, "ch32,uart1", uart_bus_host_probe, uart_bus_host_remove)
DRIVER_REGISTER(uart_bus_host_esp32, "esp32,uart1", uart_bus_host_probe, uart_bus_host_remove)

/*
 * STM32F407 SPI HAL — 仅主机模式
 *
 * - 时钟/GPIO：由 CubeMX MX_SPIx_Init() 在 main pre_execution 钩子完成
 * - 传输/模式切换：LL_SPI_* 库函数，不直接写寄存器，不配置 RCC
 */
#include "hal_spi.h"
#include "bus/bus.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx.h"

#include <string.h>

#define SPI_HOST_MAX           4
#define SPI_DEVICE_MAX         8
#define SPI_MASTER_MAX_XFER    4096

struct hal_spi_hw
{
    SPI_TypeDef* spi;
    int          cs_active;
};

static struct hal_spi_bus_host s_spi_hosts[SPI_HOST_MAX];
static struct hal_spi_hw       s_spi_hw[SPI_DEVICE_MAX];
static struct hal_spi_dev        s_registered_dev[SPI_DEVICE_MAX];
static uint8_t                   s_host_mutex_storage[SPI_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE];

static SPI_TypeDef* stm32_spi_instance(int host_id)
{
    switch (host_id)
    {
    case 0:
        return SPI1;
    default:
        return NULL;
    }
}

static uint32_t stm32_spi_prescaler(int clock_hz)
{
    if (clock_hz <= 0)
        return LL_SPI_BAUDRATEPRESCALER_DIV256;

    if (clock_hz >= 21000000)
        return LL_SPI_BAUDRATEPRESCALER_DIV2;
    if (clock_hz >= 10500000)
        return LL_SPI_BAUDRATEPRESCALER_DIV4;
    if (clock_hz >= 5250000)
        return LL_SPI_BAUDRATEPRESCALER_DIV8;
    if (clock_hz >= 2625000)
        return LL_SPI_BAUDRATEPRESCALER_DIV16;
    if (clock_hz >= 1312500)
        return LL_SPI_BAUDRATEPRESCALER_DIV32;
    if (clock_hz >= 656250)
        return LL_SPI_BAUDRATEPRESCALER_DIV64;
    if (clock_hz >= 328125)
        return LL_SPI_BAUDRATEPRESCALER_DIV128;
    return LL_SPI_BAUDRATEPRESCALER_DIV256;
}

static void stm32_spi_apply_mode(SPI_TypeDef* spi, const struct hal_spi_device_config* cfg)
{
    LL_SPI_InitTypeDef init = {0};

    if (!spi || !cfg)
        return;

    LL_SPI_Disable(spi);
    init.TransferDirection = LL_SPI_FULL_DUPLEX;
    init.Mode              = LL_SPI_MODE_MASTER;
    init.DataWidth         = LL_SPI_DATAWIDTH_8BIT;
    init.ClockPolarity     = (cfg->mode & 2) ? LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW;
    init.ClockPhase        = (cfg->mode & 1) ? LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE;
    init.NSS               = LL_SPI_NSS_SOFT;
    init.BaudRate          = stm32_spi_prescaler(cfg->clock_speed_hz);
    init.BitOrder          = LL_SPI_MSB_FIRST;
    init.CRCCalculation    = LL_SPI_CRCCALCULATION_DISABLE;
    init.CRCPoly           = 7;
    LL_SPI_Init(spi, &init);
    LL_SPI_SetStandard(spi, LL_SPI_PROTOCOL_MOTOROLA);
    LL_SPI_Enable(spi);
}

static int stm32_spi_cs_set(const struct hal_spi_device_config* cfg, int level)
{
    if (!hal_pin_is_valid(cfg->cs_pin))
        return VFS_OK;

    return hal_gpio_fast_set_level(cfg->cs_pin, level ? HAL_GPIO_HIGH_LEVEL : HAL_GPIO_LOW_LEVEL);
}

static int stm32_spi_transfer_poll(SPI_TypeDef* spi, const uint8_t* tx, uint8_t* rx, size_t len)
{
    size_t i;

    if (!spi || len == 0)
        return VFS_ERR_INVAL;

    for (i = 0; i < len; i++)
    {
        uint8_t out = tx ? tx[i] : 0xFFU;
        while (!LL_SPI_IsActiveFlag_TXE(spi))
            ;
        LL_SPI_TransmitData8(spi, out);
        while (!LL_SPI_IsActiveFlag_RXNE(spi))
            ;
        if (rx)
            rx[i] = LL_SPI_ReceiveData8(spi);
        else
            (void)LL_SPI_ReceiveData8(spi);
    }

    return (int)len;
}

static int spi_host_mutex_ensure(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;
    if (host->bus_mutex)
        return VFS_OK;

    if (host->cfg.host_id < 0 || host->cfg.host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    struct osal_mutex* mtx = NULL;
    if (osal_mutex_create_static(&mtx, s_host_mutex_storage[host->cfg.host_id],
                                 sizeof(s_host_mutex_storage[host->cfg.host_id])) != 0)
        return VFS_ERR_NOMEM;

    host->bus_mutex = mtx;
    host->dev.mutex = mtx;
    return VFS_OK;
}

static int spi_master_write_impl(bus_device_t* bus, const uint8_t* data, size_t len);
static int spi_master_read_impl(bus_device_t* bus, uint8_t* data, size_t len);
static int spi_master_transfer_impl(bus_device_t* bus, const uint8_t* tx, size_t tx_len,
                                    uint8_t* rx, size_t rx_len);

static int spi_bus_open(bus_device_t* bus)
{
    (void)bus;
    return VFS_OK;
}

static int spi_bus_close(bus_device_t* bus)
{
    (void)bus;
    return VFS_OK;
}

static int spi_bus_ioctl(bus_device_t* bus, uint32_t cmd, void* arg)
{
    (void)bus;
    (void)cmd;
    (void)arg;
    return VFS_ERR_INVAL;
}

static int spi_bus_write_op(bus_device_t* bus, const uint8_t* buf, size_t len)
{
    return spi_master_write_impl(bus, buf, len);
}

static int spi_bus_read_op(bus_device_t* bus, uint8_t* buf, size_t len)
{
    return spi_master_read_impl(bus, buf, len);
}

static int spi_bus_transfer_op(bus_device_t* bus, const uint8_t* tx_buf, size_t tx_len,
                             uint8_t* rx_buf, size_t rx_len)
{
    return spi_master_transfer_impl(bus, tx_buf, tx_len, rx_buf, rx_len);
}

static const bus_ops_t s_spi_master_bus_ops =
{
    .open     = spi_bus_open,
    .close    = spi_bus_close,
    .ioctl    = spi_bus_ioctl,
    .write    = spi_bus_write_op,
    .read     = spi_bus_read_op,
    .transfer = spi_bus_transfer_op,
    .transfer_async = NULL,
};

static struct hal_spi_bus_host* spi_bus_to_host(bus_device_t* bus)
{
    return bus ? container_of(bus, struct hal_spi_bus_host, dev) : NULL;
}

static struct hal_spi_dev* spi_host_active_dev(const struct hal_spi_bus_host* host)
{
    return host ? host->active_dev : NULL;
}

static struct hal_spi_hw* spi_dev_hw(const struct hal_spi_dev* dev)
{
    if (!dev || dev->pool_idx < 0 || dev->pool_idx >= SPI_DEVICE_MAX)
        return NULL;
    return &s_spi_hw[dev->pool_idx];
}

static void spi_bus_device_init(struct hal_spi_bus_host* host)
{
    bus_device_t* bus = &host->dev;

    memset(bus, 0, sizeof(*bus));
    bus->type      = BUS_TYPE_SPI;
    bus->ops       = &s_spi_master_bus_ops;
    bus->hw_handle = host;
    bus->status    = BUS_STATE_UNINIT;
}

static int spi_master_write_impl(bus_device_t* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_dev*      dev  = spi_host_active_dev(host);
    struct hal_spi_hw*       hw;

    if (!host || !dev || !data || len == 0)
        return VFS_ERR_INVAL;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->spi)
        return VFS_ERR_IO;

    stm32_spi_cs_set(&dev->cfg, 1);
    int ret = stm32_spi_transfer_poll(hw->spi, data, NULL, len);
    stm32_spi_cs_set(&dev->cfg, 0);
    return ret;
}

static int spi_master_read_impl(bus_device_t* bus, uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_dev*      dev  = spi_host_active_dev(host);
    struct hal_spi_hw*       hw;

    if (!host || !dev || !data || len == 0)
        return VFS_ERR_INVAL;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->spi)
        return VFS_ERR_IO;

    stm32_spi_cs_set(&dev->cfg, 1);
    int ret = stm32_spi_transfer_poll(hw->spi, NULL, data, len);
    stm32_spi_cs_set(&dev->cfg, 0);
    return ret;
}

static int spi_master_transfer_impl(bus_device_t* bus, const uint8_t* tx_buf, size_t tx_len,
                                    uint8_t* rx_buf, size_t rx_len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_dev*      dev  = spi_host_active_dev(host);
    struct hal_spi_hw*       hw;
    size_t                   len = tx_len;

    if (rx_len > 0 && rx_len != tx_len)
        return VFS_ERR_INVAL;
    if (!host || !dev || len == 0)
        return VFS_ERR_INVAL;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->spi)
        return VFS_ERR_IO;

    stm32_spi_cs_set(&dev->cfg, 1);
    int ret = stm32_spi_transfer_poll(hw->spi, tx_buf, rx_buf, len);
    stm32_spi_cs_set(&dev->cfg, 0);
    return ret;
}

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    struct hal_spi_bus_host* host;
    SPI_TypeDef*             spi;

    if (!cfg || host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (cfg->host_id != host_id)
        return VFS_ERR_INVAL;
    if (cfg->bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    spi = stm32_spi_instance(host_id);
    if (!spi)
        return VFS_ERR_NODEV;

    host = &s_spi_hosts[host_id];
    if (host->bus_ready)
        return VFS_OK;

    memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    host->cfg.bus_role = HAL_SPI_BUS_ROLE_MASTER;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = SPI_MASTER_MAX_XFER;

    if (spi_host_mutex_ensure(host) != VFS_OK)
        return VFS_ERR_NOMEM;

    spi_bus_device_init(host);
    host->bus_ready  = 1;
    host->hw_inited  = 1;
    host->dev.status = BUS_STATE_READY;
    return VFS_OK;
}

int hal_spi_bus_host_deinit(int host_id)
{
    struct hal_spi_bus_host* host;

    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (!host->bus_ready)
        return VFS_OK;
    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    memset(host, 0, sizeof(*host));
    return VFS_OK;
}

int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out)
{
    struct hal_spi_bus_host* host;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (!host->bus_ready)
        return VFS_ERR_NODEV;

    *out = host;
    return VFS_OK;
}

int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    struct hal_spi_hw* hw;

    if (!dev || !dev->host || !dev->host->bus_ready)
        return VFS_ERR_INVAL;

    hw = spi_dev_hw(dev);
    if (!hw)
        return VFS_ERR_INVAL;

    hw->spi = stm32_spi_instance(dev->host->cfg.host_id);
    if (!hw->spi)
        return VFS_ERR_NODEV;

    stm32_spi_apply_mode(hw->spi, &dev->cfg);
    dev->hw_open = 1;
    dev->host->ref_count++;
    return VFS_OK;
}

int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    if (!dev || !dev->host)
        return VFS_ERR_INVAL;

    if (dev->hw_open)
    {
        dev->hw_open = 0;
        if (dev->host->ref_count > 0)
            dev->host->ref_count--;
    }
    if (dev->host->active_dev == dev)
        dev->host->active_dev = NULL;
    return VFS_OK;
}

int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg)
{
    struct hal_spi_dev* dev;

    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    dev = host->active_dev;
    if (dev && dev->hw_open)
        stm32_spi_apply_mode(stm32_spi_instance(host->cfg.host_id), dev_cfg);

    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

int hal_spi_xfer_begin(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    if (!dev || !dev->host || !dev->hw_open)
        return VFS_ERR_INVAL;

    if (osal_mutex_lock(dev->host->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    if (hal_spi_bus_reconfigure(dev->host, &dev->cfg) != VFS_OK)
    {
        (void)osal_mutex_unlock(dev->host->bus_mutex);
        return VFS_ERR_IO;
    }

    dev->host->active_dev = dev;
    dev->host->dev.status = BUS_STATE_BUSY;
    return VFS_OK;
}

int hal_spi_xfer_end(struct hal_spi_dev* dev)
{
    if (!dev || !dev->host)
        return VFS_ERR_INVAL;

    if (dev->host->active_dev == dev)
    {
        dev->host->active_dev = NULL;
        dev->host->dev.status = BUS_STATE_READY;
    }

    return osal_mutex_unlock(dev->host->bus_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!dev)
        return VFS_ERR_INVAL;
    if (trans_len)
        *trans_len = 0;
    if (rx_data && rx_cap > 0)
        memset(rx_data, 0, rx_cap);
    return VFS_ERR_INVAL;
}

int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int                ret;

    if (!dev || !dev->host || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    ret = hal_spi_xfer_begin(dev, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->spi || len > (size_t)dev->host->cfg.max_transfer_sz)
        ret = VFS_ERR_INVAL;
    else
    {
        stm32_spi_cs_set(&dev->cfg, 1);
        ret = stm32_spi_transfer_poll(hw->spi, tx, rx, len);
        stm32_spi_cs_set(&dev->cfg, 0);
        if (ret < 0)
            ret = VFS_ERR_IO;
        else
            ret = VFS_OK;
    }

    if (hal_spi_xfer_end(dev) != VFS_OK && ret == VFS_OK)
        return VFS_ERR_IO;
    return ret;
}

void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    struct hal_spi_hw* hw;

    if (!dev || !host || !dev_cfg || pool_idx < 0 || pool_idx >= SPI_DEVICE_MAX)
        return;
    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return;

    memset(dev, 0, sizeof(*dev));
    dev->pool_idx = pool_idx;
    dev->host     = host;
    dev->cfg      = *dev_cfg;

    hw = &s_spi_hw[pool_idx];
    memset(hw, 0, sizeof(*hw));
}

void hal_spi_dev_register(struct hal_spi_dev* dev)
{
    if (!dev || dev->pool_idx < 0 || dev->pool_idx >= SPI_DEVICE_MAX)
        return;
    s_registered_dev[dev->pool_idx] = *dev;
}

void hal_spi_dev_unregister(struct hal_spi_dev* dev)
{
    if (!dev || dev->pool_idx < 0 || dev->pool_idx >= SPI_DEVICE_MAX)
        return;
    memset(&s_registered_dev[dev->pool_idx], 0, sizeof(s_registered_dev[0]));
}

/*
 * ESP32-S3 SPI controller — ESP-IDF master/slave 执行层
 *
 * 硬件初始化、设备绑定与底层传输；不含总线锁与会话管理（由 spi_core.c 负责）。
 */
#include "hal_spi.h"
#include "spi_internal.h"
#include "hal_gpio.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include "driver/spi_slave.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#define SPI_SLAVE_DEVICE_COUNT        DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
#define SPI_MASTER_DEVICE_COUNT       DTC_GEN_COUNT_HETEROGENEOUS_W25Q64_MASTER
#define SPI_DEVICE_COUNT              (SPI_SLAVE_DEVICE_COUNT + SPI_MASTER_DEVICE_COUNT)
#define SPI_SLAVE_MAX_TRANSFER_BYTES  2048
#define SPI_HOST_MAX                  3

struct hal_spi_hw
{
    union
    {
        struct
        {
            spi_slave_transaction_t trans;
            atomic_bool             trans_queued;
        } slave;
        struct
        {
            spi_device_handle_t handle;
        } master;
    } u;
    int is_master;
};

static struct hal_spi_hw s_spi_hw[SPI_DEVICE_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_spi_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(4) = {0};
static uint8_t s_spi_tx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(4) = {0};
static uint8_t s_spi_dummy_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(4) = {0};

static struct hal_spi_dev* s_registered_dev[SPI_DEVICE_COUNT] COMPAT_ALIGNED(4);
static struct hal_spi_bus_host s_spi_hosts[SPI_HOST_MAX] COMPAT_ALIGNED(4);
static uint8_t s_host_mutex_storage[SPI_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4) = {0};

static const char* const kTag = "spi_controller_esp32";

static int spi_dev_hw_idx(const struct hal_spi_dev* dev)
{
    if (!dev || !dev->ctlr || dev->pool_idx < 0)
        return -1;

    if (dev->ctlr->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        if (dev->pool_idx >= SPI_MASTER_DEVICE_COUNT)
            return -1;
        return SPI_SLAVE_DEVICE_COUNT + dev->pool_idx;
    }

    if (dev->pool_idx >= SPI_SLAVE_DEVICE_COUNT)
        return -1;
    return dev->pool_idx;
}

struct hal_spi_hw* spi_dev_hw(const struct hal_spi_dev* dev)
{
    int hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx < 0 || hw_idx >= SPI_DEVICE_COUNT)
        return NULL;
    return &s_spi_hw[hw_idx];
}

void spi_dev_hw_slot_init(int pool_idx, int is_master)
{
    int hw_idx;

    if (pool_idx < 0)
        return;

    hw_idx = is_master ? SPI_SLAVE_DEVICE_COUNT + pool_idx : pool_idx;
    if (hw_idx < 0 || hw_idx >= SPI_DEVICE_COUNT)
        return;

    memset(&s_spi_hw[hw_idx], 0, sizeof(s_spi_hw[hw_idx]));
    s_spi_hw[hw_idx].is_master = is_master;
    if (!is_master)
        atomic_init(&s_spi_hw[hw_idx].u.slave.trans_queued, false);
}

static size_t spi_host_max_transfer_bytes(const struct hal_spi_bus_host* host)
{
    if (!host)
        return SPI_SLAVE_MAX_TRANSFER_BYTES;
    if (host->cfg.max_transfer_sz > 0)
        return (size_t)host->cfg.max_transfer_sz;
    return SPI_SLAVE_MAX_TRANSFER_BYTES;
}

static spi_host_device_t spi_host_id(const struct hal_spi_bus_host* host)
{
    return (spi_host_device_t)host->cfg.host_id;
}

static int spi_host_mutex_ensure(struct hal_spi_bus_host* host)
{
    int host_id;

    if (!host)
        return VFS_ERR_INVAL;

    if (host->bus_mutex)
        return VFS_OK;

    host_id = host->cfg.host_id;
    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    struct osal_mutex* mtx = NULL;
    if (osal_mutex_create_static(&mtx, s_host_mutex_storage[host_id],
                                 sizeof(s_host_mutex_storage[host_id])) != 0)
        return VFS_ERR_NOMEM;

    host->bus_mutex = mtx;
    return VFS_OK;
}

static int spi_slave_hw_init(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg)
{
    const struct hal_spi_bus_config* bus_cfg;

    if (!host || !dev_cfg)
        return VFS_ERR_INVAL;
    if (host->hw_inited)
        return VFS_OK;

    bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = hal_pin_map_hw_gpio(bus_cfg->mosi),
        .miso_io_num = hal_pin_map_hw_gpio(bus_cfg->miso),
        .sclk_io_num = hal_pin_map_hw_gpio(bus_cfg->sclk),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus_cfg->max_transfer_sz > 0 ?
                           bus_cfg->max_transfer_sz : (int)SPI_SLAVE_MAX_TRANSFER_BYTES,
    };

    spi_slave_interface_config_t slave_cfg =
    {
        .spics_io_num = hal_pin_map_hw_gpio(dev_cfg->cs_pin),
        .mode = (uint8_t)dev_cfg->mode,
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL,
    };

    spi_dma_chan_t dma_chan = bus_cfg->dma_chan >= 0 ?
                              (spi_dma_chan_t)bus_cfg->dma_chan : SPI_DMA_CH_AUTO;

    esp_err_t err = spi_slave_initialize(spi_host_id(host), &idf_bus_cfg, &slave_cfg, dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        SYS_LOGE(kTag, "spi_slave_initialize failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    host->hw_inited = 1;
    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

static int spi_slave_hw_deinit(struct hal_spi_bus_host* host)
{
    spi_host_device_t idf_host;

    if (!host || !host->hw_inited)
        return VFS_OK;

    idf_host = spi_host_id(host);

    COMPAT_IGNORE_RESULT(spi_slave_disable(idf_host));
    if (spi_slave_free(idf_host) != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_slave_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

static int spi_slave_setup_trans(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                 size_t len, const uint8_t* tx, uint8_t* rx_buf)
{
    size_t max_len = spi_host_max_transfer_bytes(dev->ctlr);
    int    hw_idx  = spi_dev_hw_idx(dev);

    if (len > max_len || hw_idx < 0)
        return VFS_ERR_INVAL;

    if (tx)
        memcpy(s_spi_tx_buf[hw_idx], tx, len);
    else
        memset(s_spi_tx_buf[hw_idx], 0, len);

    memset(&hw->u.slave.trans, 0, sizeof(hw->u.slave.trans));
    hw->u.slave.trans.length = len * 8U;
    hw->u.slave.trans.tx_buffer = s_spi_tx_buf[hw_idx];
    hw->u.slave.trans.rx_buffer = rx_buf;
    return VFS_OK;
}

static int spi_master_bus_init(struct hal_spi_bus_host* host)
{
    const struct hal_spi_bus_config* bus_cfg;

    if (!host || host->hw_inited)
        return VFS_OK;

    bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = hal_pin_map_hw_gpio(bus_cfg->mosi),
        .miso_io_num = hal_pin_map_hw_gpio(bus_cfg->miso),
        .sclk_io_num = hal_pin_map_hw_gpio(bus_cfg->sclk),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus_cfg->max_transfer_sz > 0 ?
                           bus_cfg->max_transfer_sz : 4096,
    };

    spi_dma_chan_t dma_chan = bus_cfg->dma_chan >= 0 ?
                              (spi_dma_chan_t)bus_cfg->dma_chan : SPI_DMA_CH_AUTO;

    esp_err_t err = spi_bus_initialize(spi_host_id(host), &idf_bus_cfg, dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        SYS_LOGE(kTag, "spi_bus_initialize failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    host->hw_inited = 1;
    return VFS_OK;
}

static int spi_master_bus_deinit(struct hal_spi_bus_host* host)
{
    if (!host || !host->hw_inited)
        return VFS_OK;

    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    if (spi_bus_free(spi_host_id(host)) != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_bus_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

static int spi_master_device_add(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                 const struct hal_spi_device_config* dev_cfg)
{
    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = dev_cfg->clock_speed_hz > 0 ? dev_cfg->clock_speed_hz : 1000000,
        .mode = (uint8_t)dev_cfg->mode,
        .spics_io_num = hal_pin_map_hw_gpio(dev_cfg->cs_pin),
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
    };

    esp_err_t err = spi_bus_add_device(spi_host_id(dev->ctlr), &devcfg, &hw->u.master.handle);
    if (err != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_bus_add_device failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    return VFS_OK;
}

static int spi_master_hw_init(struct hal_spi_bus_host* host, struct hal_spi_dev* dev,
                              struct hal_spi_hw* hw,
                              const struct hal_spi_device_config* dev_cfg)
{
    int ret;

    ret = spi_master_bus_init(host);
    if (ret != VFS_OK)
        return ret;

    if (hw->u.master.handle)
        return VFS_OK;

    ret = spi_master_device_add(dev, hw, dev_cfg);
    if (ret != VFS_OK)
        return ret;

    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

static int spi_master_transmit(struct hal_spi_hw* hw, const uint8_t* tx, uint8_t* rx, size_t len)
{
    spi_transaction_t trans =
    {
        .length = len * 8U,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    if (spi_device_transmit(hw->u.master.handle, &trans) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

int spi_controller_xfer(struct hal_spi_bus_host* host, struct hal_spi_hw* hw,
                        const uint8_t* tx, uint8_t* rx, size_t len)
{
    int hw_idx;

    if (!host || !hw || !hw->is_master || len == 0)
        return VFS_ERR_INVAL;

    hw_idx = (int)(hw - s_spi_hw);
    if (hw_idx < SPI_SLAVE_DEVICE_COUNT || hw_idx >= SPI_DEVICE_COUNT ||
        len > spi_host_max_transfer_bytes(host))
        return VFS_ERR_INVAL;

    const uint8_t* tx_p = tx ? tx : s_spi_tx_buf[hw_idx];
    uint8_t*       rx_p = rx ? rx : s_spi_rx_buf[hw_idx];

    if (!tx)
        memset(s_spi_tx_buf[hw_idx], 0, len);

    return spi_master_transmit(hw, tx_p, rx_p, len);
}

int spi_controller_apply_dev_cfg(struct hal_spi_dev* dev)
{
    (void)dev;
    return VFS_OK;
}

int spi_slave_bus_reconfigure(struct hal_spi_bus_host* host,
                              const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    if (!host->hw_inited)
        return VFS_ERR_IO;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_OK;

    if (hal_pin_equal(host->active_cfg.cs_pin, dev_cfg->cs_pin) &&
        host->active_cfg.mode == dev_cfg->mode &&
        host->active_cfg.queue_size == dev_cfg->queue_size)
        return VFS_OK;

    return VFS_ERR_IO;
}

int spi_slave_controller_xfer(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                              const uint8_t* tx, uint8_t* rx, size_t len,
                              uint32_t timeout_ms)
{
    int    hw_idx;
    int    ret;
    uint8_t* rx_work;

    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    rx_work = rx ? rx : s_spi_dummy_rx_buf[hw_idx];

    ret = spi_slave_setup_trans(dev, hw, len, tx, rx_work);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(dev->ctlr), &hw->u.slave.trans,
                           osal_timeout_to_ticks(timeout_ms)) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

int spi_slave_queue_tx_internal(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    int hw_idx;
    int ret;

    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    ret = spi_slave_setup_trans(dev, hw, len, data, s_spi_dummy_rx_buf[hw_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_queue_trans(spi_host_id(dev->ctlr), &hw->u.slave.trans,
                              osal_timeout_to_ticks(timeout_ms)) != ESP_OK)
        return VFS_ERR_IO;

    atomic_store_explicit(&hw->u.slave.trans_queued, true, memory_order_release);
    return (int)len;
}

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    struct hal_spi_bus_host* host;

    if (!cfg || host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (cfg->host_id != host_id)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (host->bus_ready)
        return VFS_OK;

    memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER &&
        host->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        host->cfg.bus_role = HAL_SPI_BUS_ROLE_SLAVE;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = (int)SPI_SLAVE_MAX_TRANSFER_BYTES;

    if (spi_host_mutex_ensure(host) != VFS_OK)
        return VFS_ERR_NOMEM;

    host->bus_ready = 1;
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
    {
        SYS_LOGW(kTag, "host %d deinit with ref_count=%d", host_id, host->ref_count);
        return VFS_ERR_BUSY;
    }

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        (void)spi_master_bus_deinit(host);
    else
        (void)spi_slave_hw_deinit(host);

    host->bus_ready = 0;
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
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;
    int ret;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    host = dev->ctlr;
    hw = spi_dev_hw(dev);
    if (!host->bus_ready || !hw)
        return VFS_ERR_INVAL;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        ret = spi_master_hw_init(host, dev, hw, &dev->cfg);
        if (ret != VFS_OK)
            return ret;

        dev->hw_open = 1;
        host->ref_count++;
        return VFS_OK;
    }

    if (host->hw_inited &&
        (!hal_pin_equal(host->active_cfg.cs_pin, dev->cfg.cs_pin) ||
         host->active_cfg.mode != dev->cfg.mode))
    {
        SYS_LOGE(kTag, "ESP32 slave: cannot attach second device on host %d",
                 host->cfg.host_id);
        return VFS_ERR_BUSY;
    }

    ret = spi_slave_hw_init(host, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    dev->hw_open = 1;
    host->ref_count++;
    return VFS_OK;
}

int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    host = dev->ctlr;
    hw = spi_dev_hw(dev);
    if (!host->bus_ready || host->ref_count <= 0)
        return VFS_ERR_INVAL;

    host->ref_count--;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER && hw && hw->u.master.handle)
    {
        COMPAT_IGNORE_RESULT(spi_bus_remove_device(hw->u.master.handle));
        hw->u.master.handle = NULL;
    }

    dev->hw_open = 0;
    return VFS_OK;
}

void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!dev || !host || !dev_cfg || pool_idx < 0)
        return;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        if (pool_idx >= SPI_MASTER_DEVICE_COUNT)
            return;
    }
    else if (pool_idx >= SPI_SLAVE_DEVICE_COUNT)
    {
        return;
    }

    memset(dev, 0, sizeof(*dev));
    dev->pool_idx = pool_idx;
    dev->ctlr     = host;
    dev->cfg      = *dev_cfg;

    spi_dev_hw_slot_init(pool_idx, host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER);
}

void hal_spi_dev_register(struct hal_spi_dev* dev)
{
    int hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx >= 0 && hw_idx < SPI_DEVICE_COUNT)
        s_registered_dev[hw_idx] = dev;
}

void hal_spi_dev_unregister(struct hal_spi_dev* dev)
{
    int hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx >= 0 && hw_idx < SPI_DEVICE_COUNT)
        s_registered_dev[hw_idx] = NULL;
}

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    spi_slave_transaction_t* done = NULL;
    int hw_idx;

    hw = spi_dev_hw(dev);
    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited ||
        dev->ctlr->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ||
        !atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_INVAL;

    esp_err_t err = spi_slave_get_trans_result(spi_host_id(dev->ctlr), &done,
                                               osal_timeout_to_ticks(timeout_ms));

    if (err == ESP_ERR_TIMEOUT)
        return VFS_ERR_BUSY;
    if (err != ESP_OK || !done)
    {
        SYS_LOGE(kTag, "spi_slave_get_trans_result failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    atomic_store_explicit(&hw->u.slave.trans_queued, false, memory_order_release);

    size_t rx_bytes = (done->trans_len + 7U) / 8U;
    if (trans_len)
        *trans_len = rx_bytes;

    if (rx_data && rx_cap > 0U && rx_bytes > 0U)
    {
        hw_idx = spi_dev_hw_idx(dev);
        if (hw_idx < 0)
            return VFS_ERR_IO;

        if (rx_bytes > rx_cap)
        {
            SYS_LOGW(kTag, "rx buffer overflow! host sent %zu bytes, cap is %zu",
                     rx_bytes, rx_cap);
            return VFS_ERR_NOMEM;
        }

        size_t copy_len = rx_bytes < rx_cap ? rx_bytes : rx_cap;
        memcpy(rx_data, s_spi_dummy_rx_buf[hw_idx], copy_len);
    }

    return VFS_OK;
}

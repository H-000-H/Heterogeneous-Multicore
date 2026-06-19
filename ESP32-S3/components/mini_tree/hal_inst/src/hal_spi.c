#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "osal.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "system_log.h"
#include "driver/spi_slave.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define SPI_SLAVE_DEVICE_COUNT  DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
#define SPI_MASTER_DEVICE_COUNT DTC_GEN_COUNT_HETEROGENEOUS_W25Q64_MASTER
#define SPI_DEVICE_COUNT        (SPI_SLAVE_DEVICE_COUNT + SPI_MASTER_DEVICE_COUNT)
#define SPI_SLAVE_MAX_TRANSFER_BYTES  2048
#define SPI_HOST_MAX           3
#define DMA_SPI_ALIGNMENT      COMPAT_ALIGNED(4)

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

static struct hal_spi_hw s_spi_hw[SPI_DEVICE_COUNT];
static uint8_t s_spi_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};
static uint8_t s_spi_tx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};
static uint8_t s_spi_dummy_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};

static struct hal_spi_ctx* s_active_ctx[SPI_DEVICE_COUNT];
static struct hal_spi_bus_host s_spi_hosts[SPI_HOST_MAX];
static uint8_t s_host_mutex_storage[SPI_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE] = {0};

static const char* const kTag = "hal_spi";

/*bus函数反推  SPI 总线控制器实体*/
static struct hal_spi_bus_host* spi_bus_to_host(struct hal_spi_bus* bus)
{
    return bus ? (struct hal_spi_bus_host*)bus->_impl : NULL;
}

/*总线反推软件*/
static struct hal_spi_ctx* spi_host_active_ctx(const struct hal_spi_bus_host* host)
{
    return host ? host->active_ctx : NULL;
}

/*软件层反推硬件层*/
static int spi_ctx_hw_idx(const struct hal_spi_ctx* ctx)
{
    if (!ctx || !ctx->host || ctx->pool_idx < 0)
        return -1;

    if (ctx->host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        if (ctx->pool_idx >= SPI_MASTER_DEVICE_COUNT)
            return -1;
        return SPI_SLAVE_DEVICE_COUNT + ctx->pool_idx;
    }

    if (ctx->pool_idx >= SPI_SLAVE_DEVICE_COUNT)
        return -1;
    return ctx->pool_idx;
}

static struct hal_spi_hw* spi_ctx_hw(const struct hal_spi_ctx* ctx)
{
    int hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0 || hw_idx >= SPI_DEVICE_COUNT)
        return NULL;
    return &s_spi_hw[hw_idx];
}

static size_t spi_host_max_transfer_bytes(const struct hal_spi_bus_host* host)
{
    if (!host)
        return SPI_SLAVE_MAX_TRANSFER_BYTES;
    if (host->cfg.max_transfer_sz > 0)
        return (size_t)host->cfg.max_transfer_sz;
    return SPI_SLAVE_MAX_TRANSFER_BYTES;
}

/*强转为esp spi id*/
static spi_host_device_t spi_host_id(const struct hal_spi_bus_host* host)
{
    return (spi_host_device_t)host->cfg.host_id;
}

/*上锁*/
static int spi_host_mutex_ensure(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;

    /*懒加载只有不为NULL才会创建互斥锁*/
    if (host->bus_mutex)
        return VFS_OK;

    int host_id = host->cfg.host_id;
    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    struct osal_mutex* mtx = NULL;
    if (osal_mutex_create_static(&mtx, s_host_mutex_storage[host_id],
                                 sizeof(s_host_mutex_storage[host_id])) != 0)
        return VFS_ERR_NOMEM;

    host->bus_mutex = mtx;
    return VFS_OK;
}

/*从设备初始化*/
static int spi_slave_hw_init(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg)
        return VFS_ERR_INVAL;
    if (host->hw_inited)
        return VFS_OK;

    const struct hal_spi_bus_config* bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = bus_cfg->mosi,
        .miso_io_num = bus_cfg->miso,
        .sclk_io_num = bus_cfg->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus_cfg->max_transfer_sz > 0 ?
                           bus_cfg->max_transfer_sz : (int)SPI_SLAVE_MAX_TRANSFER_BYTES,
    };

    spi_slave_interface_config_t slave_cfg =
    {
        .spics_io_num = dev_cfg->cs_pin,
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

/*从设备反初始化*/
static int spi_slave_hw_deinit(struct hal_spi_bus_host* host)
{
    if (!host || !host->hw_inited)
        return VFS_OK;

    spi_host_device_t idf_host = spi_host_id(host);

    COMPAT_IGNORE_RESULT(spi_slave_disable(idf_host));
    if (spi_slave_free(idf_host) != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_slave_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    __builtin_memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

/*数据扔进缓冲区(传输前必须使用)*/
static int spi_slave_setup_trans(struct hal_spi_ctx* ctx, struct hal_spi_hw* hw,
                                 size_t len, const uint8_t* tx, uint8_t* rx_buf)
{
    size_t max_len = spi_host_max_transfer_bytes(ctx->host);
    int hw_idx = spi_ctx_hw_idx(ctx);

    if (len > max_len || hw_idx < 0)
        return VFS_ERR_INVAL;

    if (tx)
        __builtin_memcpy(s_spi_tx_buf[hw_idx], tx, len);
    else
        __builtin_memset(s_spi_tx_buf[hw_idx], 0, len);

    __builtin_memset(&hw->u.slave.trans, 0, sizeof(hw->u.slave.trans));
    hw->u.slave.trans.length = len * 8U;
    hw->u.slave.trans.tx_buffer = s_spi_tx_buf[hw_idx];
    hw->u.slave.trans.rx_buffer = rx_buf;
    return VFS_OK;
}

static int spi_master_bus_init(struct hal_spi_bus_host* host)
{
    if (!host || host->hw_inited)
        return VFS_OK;

    const struct hal_spi_bus_config* bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = bus_cfg->mosi,
        .miso_io_num = bus_cfg->miso,
        .sclk_io_num = bus_cfg->sclk,
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
    __builtin_memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

static int spi_master_device_add(struct hal_spi_ctx* ctx, struct hal_spi_hw* hw,
                                 const struct hal_spi_device_config* dev_cfg)
{
    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = dev_cfg->clock_speed_hz > 0 ? dev_cfg->clock_speed_hz : 1000000,
        .mode = (uint8_t)dev_cfg->mode,
        .spics_io_num = dev_cfg->cs_pin,
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
    };

    esp_err_t err = spi_bus_add_device(spi_host_id(ctx->host), &devcfg, &hw->u.master.handle);
    if (err != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_bus_add_device failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    return VFS_OK;
}

static int spi_master_hw_init(struct hal_spi_bus_host* host,
                              struct hal_spi_ctx* ctx,
                              struct hal_spi_hw* hw,
                              const struct hal_spi_device_config* dev_cfg)
{
    int ret = spi_master_bus_init(host);
    if (ret != VFS_OK)
        return ret;

    if (hw->u.master.handle)
        return VFS_OK;

    ret = spi_master_device_add(ctx, hw, dev_cfg);
    if (ret != VFS_OK)
        return ret;

    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

static int spi_master_hw_deinit(struct hal_spi_bus_host* host, struct hal_spi_hw* hw)
{
    if (hw && hw->u.master.handle)
    {
        COMPAT_IGNORE_RESULT(spi_bus_remove_device(hw->u.master.handle));
        hw->u.master.handle = NULL;
    }

    return spi_master_bus_deinit(host);
}

static int spi_master_transmit(struct hal_spi_ctx* ctx, struct hal_spi_hw* hw,
                               const uint8_t* tx, uint8_t* rx, size_t len)
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

/*上半部异步进队 (仅 Slave) */
static int spi_bus_write_top_half_impl(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    int hw_idx;

    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0 ||
        host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    int ret = spi_slave_setup_trans(ctx, hw, len, data, s_spi_dummy_rx_buf[hw_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_queue_trans(spi_host_id(host), &hw->u.slave.trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    atomic_store_explicit(&hw->u.slave.trans_queued, true, memory_order_release);
    return (int)len;
}

/*同步写 (Slave) */
static int spi_bus_write_impl(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    int hw_idx;

    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0 ||
        host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    int ret = spi_slave_setup_trans(ctx, hw, len, data, s_spi_dummy_rx_buf[hw_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(host), &hw->u.slave.trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

/*同步读取 (Slave) */
static int spi_bus_read_impl(struct hal_spi_bus* bus, uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    int hw_idx;

    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0 ||
        host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    int ret = spi_slave_setup_trans(ctx, hw, len, NULL, s_spi_rx_buf[hw_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(host), &hw->u.slave.trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    __builtin_memcpy(data, s_spi_rx_buf[hw_idx], len);
    return (int)len;
}

static int spi_master_write_impl(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    int hw_idx;

    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0 || len > spi_host_max_transfer_bytes(host))
        return VFS_ERR_INVAL;

    __builtin_memcpy(s_spi_tx_buf[hw_idx], data, len);
    return spi_master_transmit(ctx, hw, s_spi_tx_buf[hw_idx], NULL, len);
}

static int spi_master_read_impl(struct hal_spi_bus* bus, uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    int hw_idx;

    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx < 0 || len > spi_host_max_transfer_bytes(host))
        return VFS_ERR_INVAL;

    __builtin_memset(s_spi_tx_buf[hw_idx], 0, len);
    int ret = spi_master_transmit(ctx, hw, s_spi_tx_buf[hw_idx], s_spi_rx_buf[hw_idx], len);
    if (ret < 0)
        return ret;

    __builtin_memcpy(data, s_spi_rx_buf[hw_idx], len);
    return (int)len;
}

void hal_spi_bus_init_struct(struct hal_spi_bus* bus, int bus_role)
{
    if (!bus)
        return;

    if (bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        bus->write = spi_master_write_impl;
        bus->write_top_half = NULL;
        bus->read = spi_master_read_impl;
    }
    else
    {
        bus->write = spi_bus_write_impl;
        bus->write_top_half = spi_bus_write_top_half_impl;
        bus->read = spi_bus_read_impl;
    }
    bus->_impl = NULL;
}

/*SPI 总线主机资源初始化接口*/
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    if (!cfg || host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (cfg->host_id != host_id)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = &s_spi_hosts[host_id];
    if (host->bus_ready)
        return VFS_OK;

    __builtin_memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER &&
        host->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        host->cfg.bus_role = HAL_SPI_BUS_ROLE_SLAVE;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = (int)SPI_SLAVE_MAX_TRANSFER_BYTES;

    if (spi_host_mutex_ensure(host) != VFS_OK)
        return VFS_ERR_NOMEM;

    hal_spi_bus_init_struct(&host->bus, host->cfg.bus_role);
    host->bus._impl = host;
    host->bus_ready = 1;
    return VFS_OK;
}

int hal_spi_bus_host_deinit(int host_id)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = &s_spi_hosts[host_id];
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
    host->active_ctx = NULL;
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

/*SPI 硬件资源初始化绑定接口*/
int hal_spi_interface_attach(struct hal_spi_ctx* ctx)
{
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;
    int ret;

    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;

    host = ctx->host;
    hw = spi_ctx_hw(ctx);
    if (!host->bus_ready || !hw)
        return VFS_ERR_INVAL;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        ret = spi_master_hw_init(host, ctx, hw, &ctx->cfg);
        if (ret != VFS_OK)
            return ret;

        host->ref_count++;
        return VFS_OK;
    }

    /* ESP32 SPI slave: 每 host 仅一个 interface 可激活硬件 */
    if (host->hw_inited &&
        (host->active_cfg.cs_pin != ctx->cfg.cs_pin ||
         host->active_cfg.mode != ctx->cfg.mode))
    {
        SYS_LOGE(kTag, "ESP32 slave: cannot attach second device on host %d",
                 host->cfg.host_id);
        return VFS_ERR_BUSY;
    }

    ret = spi_slave_hw_init(host, &ctx->cfg);
    if (ret != VFS_OK)
        return ret;

    host->ref_count++;
    return VFS_OK;
}

int hal_spi_interface_detach(struct hal_spi_ctx* ctx)
{
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;

    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;

    host = ctx->host;
    hw = spi_ctx_hw(ctx);
    if (!host->bus_ready || host->ref_count <= 0)
        return VFS_ERR_INVAL;

    host->ref_count--;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER && hw && hw->u.master.handle)
    {
        COMPAT_IGNORE_RESULT(spi_bus_remove_device(hw->u.master.handle));
        hw->u.master.handle = NULL;
    }

    return VFS_OK;
}

/*绑死 ESP32 SPI Slave 的 CS 和 mode */
int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    if (!host->hw_inited)
        return VFS_ERR_IO;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_OK;

    if (host->active_cfg.cs_pin == dev_cfg->cs_pin &&
        host->active_cfg.mode == dev_cfg->mode &&
        host->active_cfg.queue_size == dev_cfg->queue_size)
        return VFS_OK;

    return VFS_ERR_IO;
}

/*传输会话上锁 + 总线动态重配*/
int hal_spi_xfer_begin(struct hal_spi_ctx* ctx, uint32_t timeout_ms)
{
    if (!ctx || !ctx->host || !ctx->attached)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = ctx->host;
    if (osal_mutex_lock(host->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    int ret = hal_spi_bus_reconfigure(host, &ctx->cfg);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(osal_mutex_unlock(host->bus_mutex));
        return ret;
    }

    host->active_ctx = ctx;
    return VFS_OK;
}

/*传输会话解锁*/
int hal_spi_xfer_end(struct hal_spi_ctx* ctx)
{
    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = ctx->host;
    if (host->active_ctx == ctx)
        host->active_ctx = NULL;

    return osal_mutex_unlock(host->bus_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

int hal_spi_transfer(struct hal_spi_ctx* ctx, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int hw_idx;
    int ret;
    int n;

    if (!ctx || !ctx->host || !ctx->attached || len == 0)
        return VFS_ERR_INVAL;

    if (ctx->host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    ret = hal_spi_xfer_begin(ctx, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    hw = spi_ctx_hw(ctx);
    hw_idx = spi_ctx_hw_idx(ctx);
    if (!hw || hw_idx < 0 || len > spi_host_max_transfer_bytes(ctx->host))
        ret = VFS_ERR_INVAL;
    else
    {
        const uint8_t* tx_p = tx ? tx : s_spi_tx_buf[hw_idx];
        uint8_t* rx_p = rx ? rx : s_spi_rx_buf[hw_idx];

        if (!tx)
            __builtin_memset(s_spi_tx_buf[hw_idx], 0, len);

        n = spi_master_transmit(ctx, hw, tx_p, rx_p, len);
        ret = (n >= 0) ? VFS_OK : VFS_ERR_IO;
    }

    if (hal_spi_xfer_end(ctx) != VFS_OK && ret == VFS_OK)
        return VFS_ERR_IO;

    return ret;
}

/*设备上下文初始化*/
void hal_spi_ctx_init(struct hal_spi_ctx* ctx, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    int hw_idx;
    struct hal_spi_hw* hw;

    if (!ctx || !host || !dev_cfg || pool_idx < 0)
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

    __builtin_memset(ctx, 0, sizeof(*ctx));
    ctx->pool_idx = pool_idx;
    ctx->host = host;
    ctx->cfg = *dev_cfg;

    hw_idx = (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER) ?
             SPI_SLAVE_DEVICE_COUNT + pool_idx : pool_idx;
    hw = &s_spi_hw[hw_idx];
    __builtin_memset(hw, 0, sizeof(*hw));
    hw->is_master = (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER);
    if (!hw->is_master)
        atomic_init(&hw->u.slave.trans_queued, false);
}

/*将设备挂载到全局活跃池*/
void hal_spi_ctx_attach(struct hal_spi_ctx* ctx)
{
    int hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx >= 0 && hw_idx < SPI_DEVICE_COUNT)
        s_active_ctx[hw_idx] = ctx;
}

/*设备从全局池解绑*/
void hal_spi_ctx_detach(struct hal_spi_ctx* ctx)
{
    int hw_idx = spi_ctx_hw_idx(ctx);
    if (hw_idx >= 0 && hw_idx < SPI_DEVICE_COUNT)
        s_active_ctx[hw_idx] = NULL;
}

/*SPI Slave（从设备）异步 DMA 传输完成后的结果捞取接口*/
int hal_spi_get_trans_result(struct hal_spi_ctx* ctx, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    spi_slave_transaction_t* done = NULL;

    if (!ctx || !ctx->host || !hw || !ctx->host->hw_inited ||
        ctx->host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ||
        !atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_INVAL;

    esp_err_t err = spi_slave_get_trans_result(spi_host_id(ctx->host), &done,
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
        int hw_idx = spi_ctx_hw_idx(ctx);

        if (rx_bytes > rx_cap)
        {
            SYS_LOGW(kTag, "rx buffer overflow! host sent %zu bytes, cap is %zu",
                     rx_bytes, rx_cap);
            return VFS_ERR_NOMEM;
        }

        if (hw_idx < 0)
            return VFS_ERR_IO;

        size_t copy_len = rx_bytes < rx_cap ? rx_bytes : rx_cap;
        __builtin_memcpy(rx_data, s_spi_dummy_rx_buf[hw_idx], copy_len);
    }

    return VFS_OK;
}

/*总线锁*/
int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms)
{
    struct hal_spi_bus_host* host;
    int ret;

    ret = hal_spi_bus_host_get(bus_id, &host);
    if (ret != VFS_OK || !host->bus_mutex)
        return ret != VFS_OK ? ret : VFS_ERR_INVAL;

    return osal_mutex_lock(host->bus_mutex, timeout_ms) == 0 ? VFS_OK : VFS_ERR_BUSY;
}

int hal_spi_unlock_bus(int bus_id)
{
    struct hal_spi_bus_host* host;
    int ret;

    ret = hal_spi_bus_host_get(bus_id, &host);
    if (ret != VFS_OK || !host->bus_mutex)
        return ret != VFS_OK ? ret : VFS_ERR_INVAL;

    return osal_mutex_unlock(host->bus_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

/*拉低 CS*/
int hal_spi_assert_cs(int bus_id, int cs_line)
{
    SYS_LOGW(kTag, "hardware CS enabled, skip manual cs assert"); COMPAT_IGNORE_RESULT(bus_id); COMPAT_IGNORE_RESULT(cs_line);
    return VFS_OK;
}

/*拉高 CS*/
int hal_spi_deassert_cs(int bus_id, int cs_line)
{
    COMPAT_IGNORE_RESULT(bus_id); COMPAT_IGNORE_RESULT(cs_line);
    return VFS_OK;
}

/*强行停机注销全部资源*/
void hal_spi_force_stop(void)
{
    for (int i = 0; i < SPI_HOST_MAX; i++)
    {
        struct hal_spi_bus_host* host = &s_spi_hosts[i];
        if (!host->bus_ready)
            continue;

        if (host->active_ctx)
        {
            struct hal_spi_hw* hw = spi_ctx_hw(host->active_ctx);
            if (hw && !hw->is_master &&
                atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
            {
                spi_slave_transaction_t* done = NULL;
                COMPAT_IGNORE_RESULT(spi_slave_get_trans_result(spi_host_id(host), &done, osal_ticks_from_ms(50)));
                atomic_store_explicit(&hw->u.slave.trans_queued, false, memory_order_release);
            }
        }

        if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
            COMPAT_IGNORE_RESULT(spi_master_bus_deinit(host));
        else
            COMPAT_IGNORE_RESULT(spi_slave_hw_deinit(host));
        host->ref_count = 0;
        host->bus_ready = 0;
        host->active_ctx = NULL;
    }
}


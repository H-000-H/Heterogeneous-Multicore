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
#include "esp_err.h"

#define SPI_DEVICE_COUNT       DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
#define SPI_MAX_TRANSFER_BYTES 64
#define SPI_HOST_MAX           3
#define DMA_SPI_ALIGNMENT      COMPAT_ALIGNED(4)

struct hal_spi_hw
{
    spi_slave_transaction_t trans;
    atomic_bool             trans_queued;
};

static struct hal_spi_hw s_spi_hw[SPI_DEVICE_COUNT];
static uint8_t s_spi_rx_buf[SPI_DEVICE_COUNT][SPI_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};
static uint8_t s_spi_tx_buf[SPI_DEVICE_COUNT][SPI_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};
static uint8_t s_spi_dummy_rx_buf[SPI_DEVICE_COUNT][SPI_MAX_TRANSFER_BYTES] DMA_SPI_ALIGNMENT = {0};

static struct hal_spi_ctx* s_active_ctx[SPI_DEVICE_COUNT];
static struct hal_spi_bus_host s_spi_hosts[SPI_HOST_MAX];
static uint8_t s_host_mutex_storage[SPI_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE] = {0};

static const char* const kTag = "hal_spi";

static struct hal_spi_bus_host* spi_bus_to_host(struct hal_spi_bus* bus)
{
    return bus ? (struct hal_spi_bus_host*)bus->_impl : NULL;
}

static struct hal_spi_ctx* spi_host_active_ctx(const struct hal_spi_bus_host* host)
{
    return host ? host->active_ctx : NULL;
}

static struct hal_spi_hw* spi_ctx_hw(const struct hal_spi_ctx* ctx)
{
    if (!ctx || ctx->pool_idx < 0 || ctx->pool_idx >= SPI_DEVICE_COUNT)
        return NULL;
    return &s_spi_hw[ctx->pool_idx];
}

static spi_host_device_t spi_host_id(const struct hal_spi_bus_host* host)
{
    return (spi_host_device_t)host->cfg.host_id;
}

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
                           bus_cfg->max_transfer_sz : SPI_MAX_TRANSFER_BYTES,
    };

    spi_slave_interface_config_t slave_cfg = {
        .spics_io_num = dev_cfg->cs_pin,
        .mode = (uint8_t)dev_cfg->mode,
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL,
    };

    spi_dma_chan_t dma_chan = bus_cfg->dma_chan >= 0 ?
                              (spi_dma_chan_t)bus_cfg->dma_chan : SPI_DMA_CH_AUTO;

    esp_err_t err = spi_slave_initialize(spi_host_id(host), &idf_bus_cfg, &slave_cfg, dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        SYS_LOGE(kTag, "spi_slave_initialize failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    host->hw_inited = 1;
    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

static int spi_slave_hw_deinit(struct hal_spi_bus_host* host)
{
    if (!host || !host->hw_inited)
        return VFS_OK;

    spi_host_device_t idf_host = spi_host_id(host);

    COMPAT_IGNORE_RESULT(spi_slave_disable(idf_host));
    if (spi_slave_free(idf_host) != ESP_OK) {
        SYS_LOGE(kTag, "spi_slave_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    __builtin_memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

static int spi_slave_setup_trans(struct hal_spi_ctx* ctx, struct hal_spi_hw* hw,
                                 size_t len, const uint8_t* tx, uint8_t* rx_buf)
{
    if (len > SPI_MAX_TRANSFER_BYTES)
        return VFS_ERR_INVAL;

    if (tx)
        __builtin_memcpy(s_spi_tx_buf[ctx->pool_idx], tx, len);
    else
        __builtin_memset(s_spi_tx_buf[ctx->pool_idx], 0, len);

    __builtin_memset(&hw->trans, 0, sizeof(hw->trans));
    hw->trans.length = len * 8U;
    hw->trans.tx_buffer = s_spi_tx_buf[ctx->pool_idx];
    hw->trans.rx_buffer = rx_buf;
    return VFS_OK;
}

static int spi_bus_write_top_half_impl(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    int ret = spi_slave_setup_trans(ctx, hw, len, data, s_spi_dummy_rx_buf[ctx->pool_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_queue_trans(spi_host_id(host), &hw->trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    atomic_store_explicit(&hw->trans_queued, true, memory_order_release);
    return (int)len;
}

static int spi_bus_write_impl(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    int ret = spi_slave_setup_trans(ctx, hw, len, data, s_spi_dummy_rx_buf[ctx->pool_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(host), &hw->trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

static int spi_bus_read_impl(struct hal_spi_bus* bus, uint8_t* data, size_t len)
{
    struct hal_spi_bus_host* host = spi_bus_to_host(bus);
    struct hal_spi_ctx* ctx = spi_host_active_ctx(host);
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    if (!host || !ctx || !hw || !host->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    int ret = spi_slave_setup_trans(ctx, hw, len, NULL, s_spi_rx_buf[ctx->pool_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(host), &hw->trans, osal_ticks_from_ms(100)) != ESP_OK)
        return VFS_ERR_IO;

    __builtin_memcpy(data, s_spi_rx_buf[ctx->pool_idx], len);
    return (int)len;
}

void hal_spi_bus_init_struct(struct hal_spi_bus* bus)
{
    if (!bus) return;

    bus->write = spi_bus_write_impl;
    bus->write_top_half = spi_bus_write_top_half_impl;
    bus->read = spi_bus_read_impl;
    bus->_impl = NULL;
}

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
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = SPI_MAX_TRANSFER_BYTES;

    if (spi_host_mutex_ensure(host) != VFS_OK)
        return VFS_ERR_NOMEM;

    hal_spi_bus_init_struct(&host->bus);
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

    if (host->ref_count > 0) {
        SYS_LOGW(kTag, "host %d deinit with ref_count=%d", host_id, host->ref_count);
        return VFS_ERR_BUSY;
    }

    (void)spi_slave_hw_deinit(host);
    host->bus_ready = 0;
    host->active_ctx = NULL;
    return VFS_OK;
}

struct hal_spi_bus_host* hal_spi_bus_host_get(int host_id)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX)
        return NULL;

    struct hal_spi_bus_host* host = &s_spi_hosts[host_id];
    return host->bus_ready ? host : NULL;
}

int hal_spi_interface_attach(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    /* ESP32 SPI slave: 每 host 仅一个 interface 可激活硬件; 多 interface 共享需 master 路径 */
    if (host->hw_inited &&
        (host->active_cfg.cs_pin != dev_cfg->cs_pin ||
         host->active_cfg.mode != dev_cfg->mode)) {
        SYS_LOGE(kTag, "ESP32 slave: cannot attach second device on host %d",
                 host->cfg.host_id);
        return VFS_ERR_BUSY;
    }

    int ret = spi_slave_hw_init(host, dev_cfg);
    if (ret != VFS_OK)
        return ret;

    host->ref_count++;
    return VFS_OK;
}

int hal_spi_interface_detach(struct hal_spi_bus_host* host)
{
    if (!host || !host->bus_ready)
        return VFS_ERR_INVAL;

    if (host->ref_count <= 0)
        return VFS_ERR_INVAL;

    host->ref_count--;
  /* 不 deinit 硬件 — 总线控制器常驻, 供同 host 其他 interface 继续使用 */
    return VFS_OK;
}

/*改写寄存器设置同步不同的配置*/
int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    if (!host->hw_inited)
        return VFS_ERR_IO;

    /* ESP32 slave: CS/mode 在 spi_slave_initialize 时固定, 运行时切换需 master 实现 */
    if (host->active_cfg.cs_pin == dev_cfg->cs_pin &&
        host->active_cfg.mode == dev_cfg->mode &&
        host->active_cfg.queue_size == dev_cfg->queue_size)
        return VFS_OK;

    return VFS_ERR_IO;
}

int hal_spi_xfer_begin(struct hal_spi_ctx* ctx, uint32_t timeout_ms)
{
    if (!ctx || !ctx->host || !ctx->attached)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = ctx->host;
    if (osal_mutex_lock(host->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    int ret = hal_spi_bus_reconfigure(host, &ctx->cfg);
    if (ret != VFS_OK) {
        COMPAT_IGNORE_RESULT(osal_mutex_unlock(host->bus_mutex));
        return ret;
    }

    host->active_ctx = ctx;
    return VFS_OK;
}

int hal_spi_xfer_end(struct hal_spi_ctx* ctx)
{
    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;

    struct hal_spi_bus_host* host = ctx->host;
    if (host->active_ctx == ctx)
        host->active_ctx = NULL;

    return osal_mutex_unlock(host->bus_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

void hal_spi_ctx_init(struct hal_spi_ctx* ctx, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!ctx || !host || !dev_cfg || pool_idx < 0 || pool_idx >= SPI_DEVICE_COUNT)
        return;

    __builtin_memset(ctx, 0, sizeof(*ctx));
    ctx->pool_idx = pool_idx;
    ctx->host = host;
    ctx->cfg = *dev_cfg;

    __builtin_memset(&s_spi_hw[pool_idx], 0, sizeof(s_spi_hw[pool_idx]));
    atomic_init(&s_spi_hw[pool_idx].trans_queued, false);
}

void hal_spi_ctx_attach(struct hal_spi_ctx* ctx)
{
    if (!ctx || ctx->pool_idx < 0 || ctx->pool_idx >= SPI_DEVICE_COUNT) return;
    s_active_ctx[ctx->pool_idx] = ctx;
}

void hal_spi_ctx_detach(struct hal_spi_ctx* ctx)
{
    if (!ctx || ctx->pool_idx < 0 || ctx->pool_idx >= SPI_DEVICE_COUNT) return;
    s_active_ctx[ctx->pool_idx] = NULL;
}

int hal_spi_get_trans_result(struct hal_spi_ctx* ctx, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw = spi_ctx_hw(ctx);
    spi_slave_transaction_t* done = NULL;

    if (!ctx || !ctx->host || !hw || !ctx->host->hw_inited ||
        !atomic_load_explicit(&hw->trans_queued, memory_order_acquire)) {
        return VFS_ERR_INVAL;
    }

    esp_err_t err = spi_slave_get_trans_result(spi_host_id(ctx->host), &done,
                                               osal_timeout_to_ticks(timeout_ms));

    if (err == ESP_ERR_TIMEOUT)
        return VFS_ERR_BUSY;
    if (err != ESP_OK || !done) {
        SYS_LOGE(kTag, "spi_slave_get_trans_result failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    atomic_store_explicit(&hw->trans_queued, false, memory_order_release);

    size_t rx_bytes = (done->trans_len + 7U) / 8U;
    if (trans_len)
        *trans_len = rx_bytes;

    if (rx_data && rx_cap > 0U && rx_bytes > 0U) {
        if (rx_bytes > rx_cap) {
            SYS_LOGW(kTag, "rx buffer overflow! host sent %zu bytes, cap is %zu",
                     rx_bytes, rx_cap);
            return VFS_ERR_NOMEM;
        }

        size_t copy_len = rx_bytes < rx_cap ? rx_bytes : rx_cap;
        __builtin_memcpy(rx_data, s_spi_dummy_rx_buf[ctx->pool_idx], copy_len);
    }

    return VFS_OK;
}

int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms)
{
    struct hal_spi_bus_host* host = hal_spi_bus_host_get(bus_id);
    if (!host || !host->bus_mutex)
        return VFS_ERR_INVAL;

    return osal_mutex_lock(host->bus_mutex, timeout_ms) == 0 ? VFS_OK : VFS_ERR_BUSY;
}

int hal_spi_unlock_bus(int bus_id)
{
    struct hal_spi_bus_host* host = hal_spi_bus_host_get(bus_id);
    if (!host || !host->bus_mutex)
        return VFS_ERR_INVAL;

    return osal_mutex_unlock(host->bus_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

int hal_spi_assert_cs(int bus_id, int cs_line)
{
    COMPAT_IGNORE_RESULT(bus_id); COMPAT_IGNORE_RESULT(cs_line);
    return VFS_OK;
}

int hal_spi_deassert_cs(int bus_id, int cs_line)
{
    COMPAT_IGNORE_RESULT(bus_id); COMPAT_IGNORE_RESULT(cs_line);
    return VFS_OK;
}

void hal_spi_force_stop(void)
{
    for (int i = 0; i < SPI_HOST_MAX; i++) {
        struct hal_spi_bus_host* host = &s_spi_hosts[i];
        if (!host->bus_ready)
            continue;

        if (host->active_ctx) {
            struct hal_spi_hw* hw = spi_ctx_hw(host->active_ctx);
            if (hw && atomic_load_explicit(&hw->trans_queued, memory_order_acquire)) {
                spi_slave_transaction_t* done = NULL;
                (void)spi_slave_get_trans_result(spi_host_id(host), &done, osal_ticks_from_ms(50));
                atomic_store_explicit(&hw->trans_queued, false, memory_order_release);
            }
        }

        (void)spi_slave_hw_deinit(host);
        host->ref_count = 0;
        host->bus_ready = 0;
        host->active_ctx = NULL;
    }
}

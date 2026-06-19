/* HAL SPI 占位实现 — 平台 SPI 硬件就绪后替换为 hal_spi.c */
#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "VFS.h"
#include "board_config.h"
#include "osal.h"

#include <string.h>

#define SPI_HOST_MAX    4
#define SPI_DEVICE_MAX  8

static struct hal_spi_bus_host s_spi_hosts[SPI_HOST_MAX];
static uint8_t                 s_host_ready[SPI_HOST_MAX];
static struct hal_spi_ctx*       s_active_ctx[SPI_DEVICE_MAX];

static int stub_bus_write(struct hal_spi_bus* bus, const uint8_t* data, size_t len)
{
    (void)bus;
    (void)data;
    return (int)len;
}

static int stub_bus_read(struct hal_spi_bus* bus, uint8_t* data, size_t len)
{
    (void)bus;
    if (data && len > 0)
        memset(data, 0, len);
    return (int)len;
}

void hal_spi_bus_init_struct(struct hal_spi_bus* bus)
{
    if (!bus)
        return;
    memset(bus, 0, sizeof(*bus));
    bus->write = stub_bus_write;
    bus->read  = stub_bus_read;
}

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    struct hal_spi_bus_host* host;

    if (!cfg || host_id < 0 || host_id >= SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    hal_spi_bus_init_struct(&host->bus);
    host->bus._impl = host;
    host->bus_ready = 1;
    host->hw_inited = 1;
    s_host_ready[host_id] = 1;
    return VFS_OK;
}

int hal_spi_bus_host_deinit(int host_id)
{
    if (host_id < 0 || host_id >= SPI_HOST_MAX || !s_host_ready[host_id])
        return VFS_ERR_INVAL;

    memset(&s_spi_hosts[host_id], 0, sizeof(s_spi_hosts[host_id]));
    s_host_ready[host_id] = 0;
    return VFS_OK;
}

int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out)
{
    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (host_id < 0 || host_id >= SPI_HOST_MAX || !s_host_ready[host_id])
        return VFS_ERR_NODEV;

    *out = &s_spi_hosts[host_id];
    return VFS_OK;
}

int hal_spi_interface_attach(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->hw_inited)
        return VFS_ERR_INVAL;
    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

int hal_spi_interface_detach(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;
    host->active_ctx = NULL;
    return VFS_OK;
}

int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg)
        return VFS_ERR_INVAL;
    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

int hal_spi_xfer_begin(struct hal_spi_ctx* ctx, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!ctx || !ctx->host || !ctx->host->hw_inited)
        return VFS_ERR_INVAL;
    ctx->host->active_ctx = ctx;
    return VFS_OK;
}

int hal_spi_xfer_end(struct hal_spi_ctx* ctx)
{
    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;
    if (ctx->host->active_ctx == ctx)
        ctx->host->active_ctx = NULL;
    return VFS_OK;
}

void hal_spi_ctx_init(struct hal_spi_ctx* ctx, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!ctx || !host || !dev_cfg || pool_idx < 0 || pool_idx >= SPI_DEVICE_MAX)
        return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->pool_idx = pool_idx;
    ctx->host     = host;
    ctx->cfg      = *dev_cfg;
}

void hal_spi_ctx_attach(struct hal_spi_ctx* ctx)
{
    if (!ctx || ctx->pool_idx < 0 || ctx->pool_idx >= SPI_DEVICE_MAX)
        return;
    s_active_ctx[ctx->pool_idx] = ctx;
}

void hal_spi_ctx_detach(struct hal_spi_ctx* ctx)
{
    if (!ctx || ctx->pool_idx < 0 || ctx->pool_idx >= SPI_DEVICE_MAX)
        return;
    s_active_ctx[ctx->pool_idx] = NULL;
}

int hal_spi_get_trans_result(struct hal_spi_ctx* ctx, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!ctx || !ctx->host)
        return VFS_ERR_INVAL;
    if (trans_len)
        *trans_len = 0;
    if (rx_data && rx_cap > 0)
        memset(rx_data, 0, rx_cap);
    return VFS_OK;
}

int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms)
{
    (void)timeout_ms;
    return bus_id >= 0 ? VFS_OK : VFS_ERR_INVAL;
}

int hal_spi_unlock_bus(int bus_id)
{
    (void)bus_id;
    return VFS_OK;
}

int hal_spi_assert_cs(int bus_id, int cs_line)
{
    (void)bus_id;
    (void)cs_line;
    return VFS_OK;
}

int hal_spi_deassert_cs(int bus_id, int cs_line)
{
    (void)bus_id;
    (void)cs_line;
    return VFS_OK;
}

void hal_spi_force_stop(void)
{
}

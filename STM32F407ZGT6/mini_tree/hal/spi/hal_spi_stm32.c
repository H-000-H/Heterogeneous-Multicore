/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI HAL — STM32F4 实现 (Master only)
 *
 * 适配 ESP32 hal_spi.h 结构体与 API, 保留 STM32 LL_SPI 寄存器操作。
 * slave / async 返回 VFS_ERR_NOTSUPP。
 */
#include "hal_spi.h"
#include "dma.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

#define HAL_SPI_HOST_MAX   3   /* SPI1/SPI2/SPI3 */
#define HAL_SPI_MAX_XFER    512U

struct hal_spi_stm32_priv {
    SPI_TypeDef*     spi;
    struct osal_sem* sync_sem;
    int              hw_idx;   /* host_id, 索引 per-host dummy buffer */
};

_Static_assert(sizeof(struct hal_spi_stm32_priv) <= HAL_SPI_HW_PRIV_SIZE,
               "hal_spi_stm32_priv exceeds HAL_SPI_HW_PRIV_SIZE");

static struct hal_spi_bus_host s_spi_hosts[HAL_SPI_HOST_MAX];
/* per-host dummy buffer, 避免多 host 并发时踩踏 */
static uint8_t s_dummy_tx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);
static uint8_t s_dummy_rx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);

static SPI_TypeDef* stm32_spi_instance(int host_id)
{
    switch (host_id)
    {
    case 0: return SPI1;
    case 1: return SPI2;
    case 2: return SPI3;
    default: return NULL;
    }
}

static uint32_t stm32_spi_prescaler(int clock_hz)
{
    if (clock_hz <= 0)
        return LL_SPI_BAUDRATEPRESCALER_DIV256;
    if (clock_hz >= 21000000) return LL_SPI_BAUDRATEPRESCALER_DIV2;
    if (clock_hz >= 10500000) return LL_SPI_BAUDRATEPRESCALER_DIV4;
    if (clock_hz >= 5250000)  return LL_SPI_BAUDRATEPRESCALER_DIV8;
    if (clock_hz >= 2625000)  return LL_SPI_BAUDRATEPRESCALER_DIV16;
    if (clock_hz >= 1312500)  return LL_SPI_BAUDRATEPRESCALER_DIV32;
    if (clock_hz >= 656250)   return LL_SPI_BAUDRATEPRESCALER_DIV64;
    if (clock_hz >= 328125)   return LL_SPI_BAUDRATEPRESCALER_DIV128;
    return LL_SPI_BAUDRATEPRESCALER_DIV256;
}

static struct hal_spi_stm32_priv* stm32_priv(struct hal_spi_bus_host* host)
{
    return (struct hal_spi_stm32_priv*)host->hw_priv_storage;
}

static void hal_spi_wait_idle(SPI_TypeDef* spi, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (LL_SPI_IsActiveFlag_BSY(spi))
    {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
            return;
    }
}

/* ===== Host 管理 API ===== */
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    struct hal_spi_bus_host*        host;
    struct hal_spi_stm32_priv*      priv;
    SPI_TypeDef*                    spi;

    if (!cfg || host_id < 0 || host_id >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (cfg->host_id != host_id)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (host->bus_ready)
        return VFS_OK;

    spi = stm32_spi_instance(host_id);
    if (!spi)
        return VFS_ERR_NODEV;

    __builtin_memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER &&
        host->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        host->cfg.bus_role = HAL_SPI_BUS_ROLE_MASTER;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = (int)HAL_SPI_MAX_TRANSFER_BYTES;
    else if (host->cfg.max_transfer_sz > (int)HAL_SPI_MAX_TRANSFER_BYTES)
        return VFS_ERR_INVAL;

    priv = stm32_priv(host);
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->spi    = spi;
    priv->hw_idx = host_id;

    host->bus_ready = 1;
    return VFS_OK;
}

int hal_spi_bus_host_deinit(int host_id)
{
    struct hal_spi_bus_host* host;

    if (host_id < 0 || host_id >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (!host->bus_ready)
        return VFS_OK;

    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    host->bus_ready = 0;
    return VFS_OK;
}

int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out)
{
    struct hal_spi_bus_host* host;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (host_id < 0 || host_id >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (!host->bus_ready)
        return VFS_ERR_NODEV;

    *out = host;
    return VFS_OK;
}

/* ===== Device 管理 API ===== */
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!dev || !host || !dev_cfg || pool_idx < 0)
        return;

    __builtin_memset(dev, 0, sizeof(*dev));
    dev->pool_idx = pool_idx;
    dev->ctlr     = host;
    dev->cfg      = *dev_cfg;
}

static int stm32_spi_apply_dev_cfg(struct hal_spi_bus_host* host,
                                   const struct hal_spi_device_config* dev_cfg)
{
    struct hal_spi_stm32_priv* priv;
    SPI_TypeDef*               spi;
    LL_SPI_InitTypeDef         init = {0};

    priv = stm32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    spi = priv->spi;

    LL_SPI_Disable(spi);
    init.TransferDirection = LL_SPI_FULL_DUPLEX;
    init.Mode              = LL_SPI_MODE_MASTER;
    init.DataWidth         = LL_SPI_DATAWIDTH_8BIT;
    init.ClockPolarity     = (dev_cfg->mode & 2) ? LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW;
    init.ClockPhase        = (dev_cfg->mode & 1) ? LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE;
    init.NSS               = LL_SPI_NSS_SOFT;
    init.BaudRate          = stm32_spi_prescaler(dev_cfg->clock_speed_hz);
    init.BitOrder          = LL_SPI_MSB_FIRST;
    init.CRCCalculation    = LL_SPI_CRCCALCULATION_DISABLE;
    init.CRCPoly           = 7;

    if (LL_SPI_Init(spi, &init) != SUCCESS)
        return VFS_ERR_IO;

    LL_SPI_SetStandard(spi, LL_SPI_PROTOCOL_MOTOROLA);
    LL_SPI_Enable(spi);

    return VFS_OK;
}

int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;
    int                      ret;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (!host->bus_ready)
        return VFS_ERR_INVAL;

    ret = stm32_spi_apply_dev_cfg(host, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    dev->hw_open = 1;
    host->ref_count++;
    host->active_cfg = dev->cfg;
    return VFS_OK;
}

int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (!dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (host->ref_count > 0)
        host->ref_count--;

    dev->hw_open = 0;
    return VFS_OK;
}

/* ===== 同步传输 (Master) ===== */
static int stm32_spi_transfer_poll(struct hal_spi_bus_host* host,
                                    const uint8_t* tx, uint8_t* rx,
                                    size_t len, uint32_t timeout_ms)
{
    struct hal_spi_stm32_priv* priv;
    SPI_TypeDef*               spi;
    uint32_t                   start;
    size_t                     i;

    if (!host || len == 0)
        return VFS_ERR_INVAL;

    priv = stm32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi   = priv->spi;
    start = HAL_GetTick();

    for (i = 0; i < len; i++)
    {
        uint8_t out = tx ? tx[i] : 0xFFU;

        while (!LL_SPI_IsActiveFlag_TXE(spi))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        LL_SPI_TransmitData8(spi, out);

        while (!LL_SPI_IsActiveFlag_RXNE(spi))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        if (rx)
            rx[i] = LL_SPI_ReceiveData8(spi);
        else
            (void)LL_SPI_ReceiveData8(spi);
    }

    hal_spi_wait_idle(spi, timeout_ms);
    return VFS_OK;
}

int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > (size_t)dev->ctlr->cfg.max_transfer_sz)
        return VFS_ERR_INVAL;

    /* 若 device config 变化则重新应用 */
    if (!hal_pin_equal(dev->ctlr->active_cfg.cs_pin, dev->cfg.cs_pin) ||
        dev->ctlr->active_cfg.mode != dev->cfg.mode ||
        dev->ctlr->active_cfg.clock_speed_hz != dev->cfg.clock_speed_hz)
    {
        int ret = stm32_spi_apply_dev_cfg(dev->ctlr, &dev->cfg);
        if (ret != VFS_OK)
            return ret;
        dev->ctlr->active_cfg = dev->cfg;
    }

    return stm32_spi_transfer_poll(dev->ctlr, tx, rx, len, timeout_ms);
}

/* ===== Slave / Async API: st/ch 不支持, bus 层直接返回 NOTSUPP ===== */

/* ===== DMA 传输 (保留原接口, 供 bus 层选用) ===== */
static void hal_spi_dma_isr(struct bus_dma_chan* chan, void* user_data)
{
    struct hal_spi_stm32_priv* priv = (struct hal_spi_stm32_priv*)user_data;
    (void)chan;
    if (priv && priv->sync_sem)
        COMPAT_IGNORE_RESULT(osal_sem_post_from_isr(priv->sync_sem, NULL));
}

int hal_spi_transfer_dma_stm32(struct hal_spi_bus_host* host,
                               struct bus_dma_chan* dma_tx,
                               struct bus_dma_chan* dma_rx,
                               const uint8_t* tx, uint8_t* rx,
                               size_t len, uint32_t timeout_ms)
{
    struct hal_spi_stm32_priv* priv;
    SPI_TypeDef*               spi;
    bus_dma_xfer_t             rx_cfg = {0};
    bus_dma_xfer_t             tx_cfg = {0};
    const uint8_t*             tx_buf = tx;
    uint8_t*                   rx_buf = rx;
    int                        ret;
    uint8_t                    sem_storage[OSAL_SEM_STORAGE_SIZE];

    if (!host || !dma_tx || !dma_rx || len == 0)
        return VFS_ERR_INVAL;

    priv = stm32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi = priv->spi;

    if (osal_sem_create_binary_static(&priv->sync_sem, sem_storage, sizeof(sem_storage)) != 0)
        return VFS_ERR_NOMEM;

    if (!tx_buf)
    {
        __builtin_memset(s_dummy_tx[priv->hw_idx], 0xFF, len);
        tx_buf = s_dummy_tx[priv->hw_idx];
    }
    if (!rx_buf)
    {
        rx_buf = s_dummy_rx[priv->hw_idx];
    }

    bus_dma_set_callback(dma_rx, hal_spi_dma_isr, priv);

    rx_cfg.src     = (const void*)&spi->DR;
    rx_cfg.dst     = rx_buf;
    rx_cfg.len     = len;
    rx_cfg.dir     = BUS_DMA_DIR_PERIPH_TO_MEM;
    rx_cfg.width   = BUS_DMA_WIDTH_BYTE;
    rx_cfg.src_inc = BUS_DMA_INC_FIXED;
    rx_cfg.dst_inc = BUS_DMA_INC_INCREMENT;

    tx_cfg.src     = tx_buf;
    tx_cfg.dst     = (void*)&spi->DR;
    tx_cfg.len     = len;
    tx_cfg.dir     = BUS_DMA_DIR_MEM_TO_PERIPH;
    tx_cfg.width   = BUS_DMA_WIDTH_BYTE;
    tx_cfg.src_inc = BUS_DMA_INC_INCREMENT;
    tx_cfg.dst_inc = BUS_DMA_INC_FIXED;

    ret = bus_dma_submit(dma_rx, &rx_cfg);
    if (ret != VFS_OK)
        goto out;

    LL_SPI_EnableDMAReq_RX(spi);

    ret = bus_dma_submit(dma_tx, &tx_cfg);
    if (ret != VFS_OK)
        goto out;

    LL_SPI_EnableDMAReq_TX(spi);

    if (osal_sem_wait(priv->sync_sem, timeout_ms) != 0)
    {
        ret = VFS_ERR_TIMEOUT;
        goto out;
    }

    hal_spi_wait_idle(spi, timeout_ms);
    ret = VFS_OK;

out:
    LL_SPI_DisableDMAReq_TX(spi);
    LL_SPI_DisableDMAReq_RX(spi);
    COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
    COMPAT_IGNORE_RESULT(bus_dma_abort(dma_rx));
    osal_sem_destroy(priv->sync_sem);
    priv->sync_sem = NULL;
    return ret;
}

void hal_spi_abort_stm32(struct hal_spi_bus_host* host,
                         struct bus_dma_chan* dma_tx,
                         struct bus_dma_chan* dma_rx)
{
    struct hal_spi_stm32_priv* priv;

    if (!host)
        return;

    priv = stm32_priv(host);
    if (!priv || !priv->spi)
        return;

    LL_SPI_DisableDMAReq_TX(priv->spi);
    LL_SPI_DisableDMAReq_RX(priv->spi);

    if (dma_tx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
    if (dma_rx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_rx));
}

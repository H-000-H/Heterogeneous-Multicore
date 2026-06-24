/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI HAL — CH32V307 实现 (Master only)
 *
 * 适配 ESP32 hal_spi.h 结构体与 API, 保留 CH32 寄存器操作。
 * slave / async 返回 VFS_ERR_NOTSUPP。
 */
#include "hal_spi.h"
#include "hal_dma_ch32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "ch32v30x_spi.h"
#include "ch32v30x.h"

#define HAL_SPI_HOST_MAX   1
#define HAL_SPI_MAX_XFER    512U
#define CH32_SPI1_DR_ADDR   ((uint32_t)&SPI1->DATAR)

struct hal_spi_ch32_priv {
    SPI_TypeDef* spi;
};

static struct hal_spi_bus_host s_spi_hosts[HAL_SPI_HOST_MAX];
static uint8_t s_dummy_tx[HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);
static uint8_t s_dummy_rx[HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);

static SPI_TypeDef* ch32_spi_instance(int host_id)
{
    switch (host_id)
    {
    case 0: return SPI1;
    default: return NULL;
    }
}

static uint16_t ch32_spi_prescaler(int clock_hz)
{
    if (clock_hz <= 0) return SPI_BaudRatePrescaler_256;
    if (clock_hz >= 21000000) return SPI_BaudRatePrescaler_2;
    if (clock_hz >= 10500000) return SPI_BaudRatePrescaler_4;
    if (clock_hz >= 5250000)  return SPI_BaudRatePrescaler_8;
    if (clock_hz >= 2625000)  return SPI_BaudRatePrescaler_16;
    if (clock_hz >= 1312500)  return SPI_BaudRatePrescaler_32;
    if (clock_hz >= 656250)   return SPI_BaudRatePrescaler_64;
    if (clock_hz >= 328125)   return SPI_BaudRatePrescaler_128;
    return SPI_BaudRatePrescaler_256;
}

static void ch32_spi_clear_errors(SPI_TypeDef* spi)
{
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_RXNE) != RESET)
        (void)SPI_I2S_ReceiveData(spi);
}

static int ch32_spi_wait_idle(SPI_TypeDef* spi, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_BSY) != RESET)
    {
        if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
    return VFS_OK;
}

static struct hal_spi_ch32_priv* ch32_priv(struct hal_spi_bus_host* host)
{
    return (struct hal_spi_ch32_priv*)host->hw_priv_storage;
}

/* ===== Host 管理 API ===== */
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg)
{
    struct hal_spi_bus_host*     host;
    struct hal_spi_ch32_priv*    priv;
    SPI_TypeDef*                 spi;

    if (!cfg || host_id < 0 || host_id >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (cfg->host_id != host_id)
        return VFS_ERR_INVAL;

    host = &s_spi_hosts[host_id];
    if (host->bus_ready)
        return VFS_OK;

    spi = ch32_spi_instance(host_id);
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

    priv = ch32_priv(host);
    priv->spi = spi;

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

static int ch32_spi_apply_dev_cfg(struct hal_spi_bus_host* host,
                                  const struct hal_spi_device_config* dev_cfg)
{
    struct hal_spi_ch32_priv* priv;
    SPI_InitTypeDef           init = {0};

    priv = ch32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    SPI_Cmd(priv->spi, DISABLE);
    init.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode              = SPI_Mode_Master;
    init.SPI_DataSize          = SPI_DataSize_8b;
    init.SPI_CPOL              = (dev_cfg->mode & 2) ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA              = (dev_cfg->mode & 1) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    init.SPI_NSS               = SPI_NSS_Soft;
    init.SPI_BaudRatePrescaler = ch32_spi_prescaler(dev_cfg->clock_speed_hz);
    init.SPI_FirstBit          = SPI_FirstBit_MSB;
    init.SPI_CRCPolynomial     = 7;
    SPI_Init(priv->spi, &init);
    SPI_Cmd(priv->spi, ENABLE);
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

    ret = ch32_spi_apply_dev_cfg(host, &dev->cfg);
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
static int ch32_spi_transfer_poll(struct hal_spi_bus_host* host,
                                   const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    struct hal_spi_ch32_priv* priv;
    SPI_TypeDef*              spi;
    uint32_t                  start;
    size_t                    i;

    (void)timeout_ms;
    if (!host || len == 0)
        return VFS_ERR_INVAL;

    priv = ch32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi   = priv->spi;
    start = osal_time_ms();

    for (i = 0; i < len; i++)
    {
        uint8_t out = tx ? tx[i] : 0xFFU;

        while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_TXE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        SPI_I2S_SendData(spi, out);
        while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_RXNE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        if (rx)
            rx[i] = (uint8_t)SPI_I2S_ReceiveData(spi);
        else
            (void)SPI_I2S_ReceiveData(spi);
    }

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
        int ret = ch32_spi_apply_dev_cfg(dev->ctlr, &dev->cfg);
        if (ret != VFS_OK)
            return ret;
        dev->ctlr->active_cfg = dev->cfg;
    }

    return ch32_spi_transfer_poll(dev->ctlr, tx, rx, len, timeout_ms);
}

/* ===== Slave / Async API (不支持) ===== */
int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms)
{
    (void)dev; (void)tx; (void)rx; (void)len; (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms)
{
    (void)dev; (void)data; (void)len; (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    (void)dev; (void)rx_data; (void)rx_cap; (void)trans_len; (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

int hal_spi_transfer_async(struct hal_spi_dev* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len, hal_spi_callback_t cb,
                           void* userdata)
{
    (void)dev; (void)tx; (void)rx; (void)len; (void)cb; (void)userdata;
    return VFS_ERR_NOTSUPP;
}

int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    (void)dev; (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

/* ===== DMA 传输 (保留原接口, 供 bus 层选用) ===== */
static void ch32_spi_dma_abort(SPI_TypeDef* spi)
{
    SPI_I2S_DMACmd(spi, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel2, HAL_DMA_XFER_TIMEOUT_MS);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel3, HAL_DMA_XFER_TIMEOUT_MS);
    (void)ch32_spi_wait_idle(spi, HAL_DMA_XFER_TIMEOUT_MS);
    ch32_spi_clear_errors(spi);
}

int hal_spi_transfer_dma_ch32(struct hal_spi_bus_host* host,
                               struct bus_dma_chan* dma_tx,
                               struct bus_dma_chan* dma_rx,
                               const uint8_t* tx, uint8_t* rx,
                               size_t len, uint32_t timeout_ms)
{
    struct hal_spi_ch32_priv* priv;
    SPI_TypeDef*              spi;
    const uint8_t*            tx_buf;
    uint8_t*                  rx_buf;
    hal_dma_ch32_xfer_t       rx_cfg;
    hal_dma_ch32_xfer_t       tx_cfg;
    int                       ret;

    (void)dma_tx; (void)dma_rx; (void)timeout_ms;

    if (!host || len == 0)
        return VFS_ERR_INVAL;

    priv = ch32_priv(host);
    if (!priv || !priv->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi = priv->spi;

    ret = hal_dma_ch32_lock();
    if (ret != VFS_OK)
        return ret;

    hal_dma_ch32_clocks_enable();

    tx_buf = tx;
    rx_buf = rx;
    if (!tx_buf)
    {
        __builtin_memset(s_dummy_tx, 0xFF, len);
        tx_buf = s_dummy_tx;
    }
    if (!rx_buf)
        rx_buf = s_dummy_rx;

    ch32_spi_clear_errors(spi);
    SPI_Cmd(spi, ENABLE);

    rx_cfg.channel     = DMA1_Channel2;
    rx_cfg.tc_flag     = DMA1_FLAG_TC2;
    rx_cfg.te_flag     = DMA1_FLAG_TE2;
    rx_cfg.periph_addr = CH32_SPI1_DR_ADDR;
    rx_cfg.mem_addr    = (uint32_t)rx_buf;
    rx_cfg.dir         = DMA_DIR_PeripheralSRC;
    rx_cfg.len         = (uint16_t)len;

    tx_cfg.channel     = DMA1_Channel3;
    tx_cfg.tc_flag     = DMA1_FLAG_TC3;
    tx_cfg.te_flag     = DMA1_FLAG_TE3;
    tx_cfg.periph_addr = CH32_SPI1_DR_ADDR;
    tx_cfg.mem_addr    = (uint32_t)tx_buf;
    tx_cfg.dir         = DMA_DIR_PeripheralDST;
    tx_cfg.len         = (uint16_t)len;

    ret = hal_dma_ch32_channel_setup(&rx_cfg);
    if (ret != VFS_OK)
        goto out;

    ret = hal_dma_ch32_channel_setup(&tx_cfg);
    if (ret != VFS_OK)
        goto out;

    (void)hal_dma_ch32_channel_enable(DMA1_Channel2);
    (void)hal_dma_ch32_channel_enable(DMA1_Channel3);
    SPI_I2S_DMACmd(spi, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);

    ret = hal_dma_ch32_channel_poll(DMA1_FLAG_TC3, DMA1_FLAG_TE3, HAL_DMA_XFER_TIMEOUT_MS);
    if (ret != VFS_OK)
        goto out;

    ret = ch32_spi_wait_idle(spi, HAL_DMA_XFER_TIMEOUT_MS);

out:
    ch32_spi_dma_abort(spi);
    hal_dma_ch32_unlock();
    return ret;
}

void hal_spi_abort_ch32(struct hal_spi_bus_host* host)
{
    struct hal_spi_ch32_priv* priv;

    if (!host)
        return;

    priv = ch32_priv(host);
    if (!priv || !priv->spi)
        return;

    ch32_spi_dma_abort(priv->spi);
}

void hal_spi_ch32_dma_abort(void)
{
    ch32_spi_dma_abort(SPI1);
}

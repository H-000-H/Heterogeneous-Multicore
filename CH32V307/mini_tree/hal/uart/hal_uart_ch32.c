/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART HAL — CH32V307 实现 (vtable 模式)
 *
 * 适配 ESP32 hal_uart.h vtable 架构, 保留 CH32 寄存器操作。
 */
#include "hal_uart.h"
#include "hal_dma_ch32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "ch32v30x_usart.h"
#include "ch32v30x.h"

#define CH32_UART_READ_TIMEOUT_MS  10U
#define CH32_UART_DMA_MAX_XFER      512U
#define CH32_USART1_DR_ADDR         ((uint32_t)&USART1->DATAR)

struct hal_uart_ch32_priv {
    USART_TypeDef* usart;
};

static USART_TypeDef* ch32_usart_instance(int uart_host)
{
    switch (uart_host)
    {
    case 0:
    case 1: return USART1;
    default: return NULL;
    }
}

static uint16_t ch32_uart_parity(hal_uart_parity_t parity)
{
    if (parity == HAL_UART_PARITY_EVEN) return USART_Parity_Even;
    if (parity == HAL_UART_PARITY_ODD)  return USART_Parity_Odd;
    return USART_Parity_No;
}

static uint16_t ch32_uart_stop_bits(hal_uart_stop_bits_t stop)
{
    if (stop == HAL_UART_STOP_BITS_2)   return USART_StopBits_2;
    if (stop == HAL_UART_STOP_BITS_1_5) return USART_StopBits_1_5;
    return USART_StopBits_1;
}

static int ch32_usart_wait_tc(USART_TypeDef* usart, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();
    while (USART_GetFlagStatus(usart, USART_FLAG_TC) == RESET)
    {
        if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
    return VFS_OK;
}

/* ===== vtable 实现 ===== */
static int ch32_uart_open(const struct hal_uart_config_t* cfg)
{
    USART_TypeDef* usart;
    USART_InitTypeDef init = {0};

    if (!cfg)
        return VFS_ERR_INVAL;

    usart = ch32_usart_instance(cfg->uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    init.USART_BaudRate            = cfg->baud_rate;
    init.USART_WordLength          = USART_WordLength_8b;
    init.USART_StopBits            = ch32_uart_stop_bits(cfg->stop_bits);
    init.USART_Parity              = ch32_uart_parity(cfg->parity);
    init.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(usart, &init);
    USART_Cmd(usart, ENABLE);
    return VFS_OK;
}

static int ch32_uart_close(const struct hal_uart_config_t* cfg)
{
    USART_TypeDef* usart;

    if (!cfg)
        return VFS_ERR_INVAL;

    usart = ch32_usart_instance(cfg->uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    USART_Cmd(usart, DISABLE);
    return VFS_OK;
}

static int ch32_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    uint32_t       start;
    size_t         i;

    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = ch32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_IO;

    start = osal_time_ms();
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(usart, USART_FLAG_TXE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= CH32_UART_READ_TIMEOUT_MS)
                return VFS_ERR_TIMEOUT;
        }
        USART_SendData(usart, data[i]);
    }

    return ch32_usart_wait_tc(usart, CH32_UART_READ_TIMEOUT_MS);
}

static int ch32_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    size_t         i;
    uint32_t       waited;

    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = ch32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_IO;

    for (i = 0; i < len; i++)
    {
        waited = 0;
        while (USART_GetFlagStatus(usart, USART_FLAG_RXNE) == RESET)
        {
            if (waited >= CH32_UART_READ_TIMEOUT_MS)
                return (i > 0) ? (int)i : VFS_ERR_TIMEOUT;
            osal_delay_ms(1U);
            waited++;
        }
        data[i] = (uint8_t)USART_ReceiveData(usart);
    }

    return (int)len;
}

static int ch32_uart_transmit(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx,
                               size_t rx_len, size_t tx_len)
{
    (void)rx; (void)tx; (void)rx_len; (void)tx_len;
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

static int ch32_uart_deinit(const struct hal_uart_config_t* cfg)
{
    return ch32_uart_close(cfg);
}

static const struct hal_uart_bus s_ch32_uart_bus = {
    .open     = ch32_uart_open,
    .close    = ch32_uart_close,
    .read     = ch32_uart_read,
    .write    = ch32_uart_write,
    .transmit = ch32_uart_transmit,
    .deinit   = ch32_uart_deinit,
    ._impl    = NULL
};

const struct hal_uart_bus* hal_uart_bus_get(void)
{
    return &s_ch32_uart_bus;
}

int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!pdev)
        return VFS_ERR_INVAL;
    pdev->status = UART_STATE_BUSY;
    return VFS_OK;
}

int hal_uart_xfer_end(struct hal_uart_dev* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    pdev->status = UART_STATE_READY;
    return VFS_OK;
}

int hal_uart_force_stop(void)
{
    USART_Cmd(USART1, DISABLE);
    return VFS_OK;
}

/* ===== DMA 传输 (保留原接口) ===== */
static void ch32_uart_dma_abort(USART_TypeDef* usart)
{
    USART_DMACmd(usart, USART_DMAReq_Tx, DISABLE);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel4, HAL_DMA_XFER_TIMEOUT_MS);
}

int hal_uart_write_dma_ch32(struct hal_uart_dev* pdev,
                             struct bus_dma_chan* dma_tx,
                             const uint8_t* data, size_t len,
                             uint32_t timeout_ms)
{
    USART_TypeDef* usart;
    hal_dma_ch32_xfer_t cfg;
    int ret;

    (void)dma_tx; (void)timeout_ms;

    if (!pdev || !data || len == 0 || len > CH32_UART_DMA_MAX_XFER)
        return VFS_ERR_INVAL;

    usart = ch32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_IO;

    ret = hal_dma_ch32_lock();
    if (ret != VFS_OK)
        return ret;

    hal_dma_ch32_clocks_enable();

    cfg.channel     = DMA1_Channel4;
    cfg.tc_flag     = DMA1_FLAG_TC4;
    cfg.te_flag     = DMA1_FLAG_TE4;
    cfg.periph_addr = CH32_USART1_DR_ADDR;
    cfg.mem_addr    = (uint32_t)data;
    cfg.dir         = DMA_DIR_PeripheralDST;
    cfg.len         = (uint16_t)len;

    ret = hal_dma_ch32_channel_setup(&cfg);
    if (ret == VFS_OK)
    {
        (void)hal_dma_ch32_channel_enable(DMA1_Channel4);
        USART_DMACmd(usart, USART_DMAReq_Tx, ENABLE);
        ret = hal_dma_ch32_channel_poll(DMA1_FLAG_TC4, DMA1_FLAG_TE4, HAL_DMA_XFER_TIMEOUT_MS);
        if (ret == VFS_OK)
            ret = ch32_usart_wait_tc(usart, HAL_DMA_XFER_TIMEOUT_MS);
    }

    ch32_uart_dma_abort(usart);
    hal_dma_ch32_unlock();
    return ret;
}

void hal_uart_ch32_dma_abort(void)
{
    ch32_uart_dma_abort(USART1);
}

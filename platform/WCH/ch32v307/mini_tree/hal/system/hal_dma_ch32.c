/*
 * CH32V307 DMA 引擎 — 仅 channel 配置/启停/轮询
 *
 * 设计原则: HAL 层无锁。DMA 通道为独占资源, 通道间寄存器独立无共享状态。
 * 并发保护由 VFS 层 io_mutex 提供 (每个设备实例独立锁)。
 */
#include "hal_dma.h"
#include "hal_dma_ch32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "ch32v30x_rcc.h"

static int                 s_dma_engine_ready;

extern void hal_spi_ch32_dma_abort(void);
extern void hal_uart_ch32_dma_abort(void);

static int ch32_dma_timed_out(uint32_t start_ms, uint32_t timeout_ms)
{
    return (uint32_t)(osal_time_ms() - start_ms) >= timeout_ms;
}

void hal_dma_ch32_clocks_enable(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
}

void hal_dma_ch32_init(void)
{
    hal_dma_ch32_clocks_enable();
    s_dma_engine_ready = 1;
}

int hal_dma_ch32_channel_disable(DMA_Channel_TypeDef* channel, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();

    if (!channel)
        return VFS_ERR_INVAL;

    DMA_Cmd(channel, DISABLE);
    while (DMA_GetCurrDataCounter(channel) != 0U)
    {
        if (ch32_dma_timed_out(start, timeout_ms))
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

int hal_dma_ch32_channel_poll(uint32_t tc_flag, uint32_t te_flag, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();

    for (;;)
    {
        if (DMA_GetFlagStatus(te_flag) != RESET)
        {
            DMA_ClearFlag(te_flag);
            return VFS_ERR_IO;
        }
        if (DMA_GetFlagStatus(tc_flag) != RESET)
        {
            DMA_ClearFlag(tc_flag);
            return VFS_OK;
        }
        if (ch32_dma_timed_out(start, timeout_ms))
            return VFS_ERR_IO;
    }
}

int hal_dma_ch32_channel_setup(const hal_dma_ch32_xfer_t* cfg)
{
    DMA_InitTypeDef init = {0};

    if (!cfg || !cfg->channel || cfg->len == 0U)
        return VFS_ERR_INVAL;

    if (hal_dma_ch32_channel_disable(cfg->channel, HAL_DMA_XFER_TIMEOUT_MS) != VFS_OK)
        return VFS_ERR_IO;

    init.DMA_PeripheralBaseAddr = cfg->periph_addr;
    init.DMA_MemoryBaseAddr     = cfg->mem_addr;
    init.DMA_DIR                = cfg->dir;
    init.DMA_BufferSize         = cfg->len;
    init.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    init.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    init.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    init.DMA_Mode               = DMA_Mode_Normal;
    init.DMA_Priority           = DMA_Priority_Medium;
    init.DMA_M2M                = DMA_M2M_Disable;

    DMA_Init(cfg->channel, &init);
    return VFS_OK;
}

int hal_dma_ch32_channel_enable(DMA_Channel_TypeDef* channel)
{
    if (!channel)
        return VFS_ERR_INVAL;
    DMA_Cmd(channel, ENABLE);
    return VFS_OK;
}

void hal_dma_init_struct(struct hal_dma_chan* chan)
{
    if (chan)
        __builtin_memset(chan, 0, sizeof(*chan));
}

void hal_dma_force_stop(void)
{
    hal_spi_ch32_dma_abort();
    hal_uart_ch32_dma_abort();
}

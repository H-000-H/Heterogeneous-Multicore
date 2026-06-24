#ifndef HAL_DMA_CH32_H
#define HAL_DMA_CH32_H

#include "ch32v30x.h"
#include <stdint.h>

#define HAL_DMA_XFER_TIMEOUT_MS 3000U

#ifndef HAL_DMA_MIN_BLOCK
#define HAL_DMA_MIN_BLOCK 32U
#endif

#ifndef HAL_DMA_DISABLED
#define HAL_DMA_DISABLED (-2)
#endif

typedef struct hal_dma_ch32_xfer
{
    DMA_Channel_TypeDef* channel;
    uint32_t             tc_flag;
    uint32_t             te_flag;
    uint32_t             periph_addr;
    uint32_t             mem_addr;
    uint32_t             dir;
    uint16_t             len;
} hal_dma_ch32_xfer_t;

void hal_dma_ch32_clocks_enable(void);
int  hal_dma_ch32_lock(void);
void hal_dma_ch32_unlock(void);
void hal_dma_ch32_init(void);

int hal_dma_ch32_channel_disable(DMA_Channel_TypeDef* channel, uint32_t timeout_ms);
int hal_dma_ch32_channel_poll(uint32_t tc_flag, uint32_t te_flag, uint32_t timeout_ms);
int hal_dma_ch32_channel_setup(const hal_dma_ch32_xfer_t* cfg);
int hal_dma_ch32_channel_enable(DMA_Channel_TypeDef* channel);

#endif /* HAL_DMA_CH32_H */

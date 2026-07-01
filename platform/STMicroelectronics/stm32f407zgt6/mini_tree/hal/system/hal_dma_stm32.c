/* SPDX-License-Identifier: Apache-2.0 */
/*
 * STM32F4 DMA 驱动实现
 *
 * 直接调用 STM32 HAL/LL DMA 接口。
 * 当前只支持 memory-to-periph / periph-to-memory 的单次传输。
 */
#include "hal_dma_stm32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx.h"

#ifndef HAL_DMA_STM32_MAX_NODES
#define HAL_DMA_STM32_MAX_NODES 16
#endif

static DMA_HandleTypeDef s_dma_handles[HAL_DMA_STM32_MAX_NODES];
static hal_dma_callback_t s_dma_cbs[HAL_DMA_STM32_MAX_NODES];
static void*              s_dma_cb_args[HAL_DMA_STM32_MAX_NODES];
static hal_dma_dts_node_t s_dma_nodes[HAL_DMA_STM32_MAX_NODES];
static uint8_t            s_dma_inited[HAL_DMA_STM32_MAX_NODES];
static uint8_t            s_dma_node_count;

static DMA_TypeDef* dma_instance(uint8_t idx)
{
    return (idx == 0) ? DMA1 : DMA2;
}

static DMA_Stream_TypeDef* dma_stream_instance(uint8_t dma_idx, uint8_t stream)
{
    /* STM32F4 的 DMA 流基址：DMA1_Stream0 = DMA1_BASE + 0x10, 每流 0x18 */
    return (DMA_Stream_TypeDef*)(uintptr_t)(((uint32_t)dma_instance(dma_idx)) + 0x10U + 0x18U * stream);
}

static int node_index_from_dts_id(int dts_id)
{
    for (int i = 0; i < s_dma_node_count; i++)
    {
        if (s_dma_nodes[i].dts_id == dts_id)
            return i;
    }
    return -1;
}

static uint8_t stm32_irqn(uint8_t dma_idx, uint8_t stream)
{
    if (dma_idx == 0)
    {
        switch (stream)
        {
        case 0: return DMA1_Stream0_IRQn;
        case 1: return DMA1_Stream1_IRQn;
        case 2: return DMA1_Stream2_IRQn;
        case 3: return DMA1_Stream3_IRQn;
        case 4: return DMA1_Stream4_IRQn;
        case 5: return DMA1_Stream5_IRQn;
        case 6: return DMA1_Stream6_IRQn;
        case 7: return DMA1_Stream7_IRQn;
        }
    }
    else
    {
        switch (stream)
        {
        case 0: return DMA2_Stream0_IRQn;
        case 1: return DMA2_Stream1_IRQn;
        case 2: return DMA2_Stream2_IRQn;
        case 3: return DMA2_Stream3_IRQn;
        case 4: return DMA2_Stream4_IRQn;
        case 5: return DMA2_Stream5_IRQn;
        case 6: return DMA2_Stream6_IRQn;
        case 7: return DMA2_Stream7_IRQn;
        }
    }
    return 0;
}

int hal_dma_stm32_register_from_props(uint32_t dts_id, int controller,
                                       int stream, int channel)
{
    int idx;

    if (s_dma_node_count >= HAL_DMA_STM32_MAX_NODES)
        return VFS_ERR_NOMEM;

    if (controller < 1 || controller > 2 || stream < 0 || stream > 7 || channel < 0 || channel > 7)
        return VFS_ERR_INVAL;

    /* 重复注册检查 */
    for (idx = 0; idx < s_dma_node_count; idx++)
    {
        if (s_dma_nodes[idx].dts_id == (int)dts_id)
            return VFS_OK;
    }

    idx = s_dma_node_count++;
    s_dma_nodes[idx].dts_id  = (int)dts_id;
    s_dma_nodes[idx].dma_idx = (uint8_t)(controller - 1);
    s_dma_nodes[idx].stream  = (uint8_t)stream;
    s_dma_nodes[idx].channel = (uint8_t)channel;
    s_dma_nodes[idx].irqn    = stm32_irqn((uint8_t)(controller - 1), (uint8_t)stream);

    return VFS_OK;
}

int hal_dma_stm32_init(void)
{
    for (int i = 0; i < s_dma_node_count; i++)
    {
        if (s_dma_inited[i])
            continue;

        DMA_HandleTypeDef* hdma = &s_dma_handles[i];
        COMPAT_MEM_SET(hdma, 0, sizeof(*hdma));

        hdma->Instance                 = dma_stream_instance(s_dma_nodes[i].dma_idx, s_dma_nodes[i].stream);
        hdma->Init.Channel             = s_dma_nodes[i].channel << DMA_SxCR_CHSEL_Pos;
        hdma->Init.Direction           = DMA_PERIPH_TO_MEMORY;
        hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
        hdma->Init.MemInc              = DMA_MINC_ENABLE;
        hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        hdma->Init.Mode                = DMA_NORMAL;
        hdma->Init.Priority            = DMA_PRIORITY_MEDIUM;
        hdma->Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

        if (HAL_DMA_Init(hdma) != HAL_OK)
            return VFS_ERR_IO;

        s_dma_inited[i] = 1;
    }
    return VFS_OK;
}

const hal_dma_dts_node_t* hal_dma_stm32_lookup(int dts_id)
{
    int idx = node_index_from_dts_id(dts_id);
    if (idx < 0)
        return NULL;
    return &s_dma_nodes[idx];
}

static uint32_t hal_to_hal_data_size(hal_dma_stm32_size_t size)
{
    switch (size)
    {
    case HAL_DMA_STM32_SIZE_HALFWORD: return DMA_PDATAALIGN_HALFWORD;
    case HAL_DMA_STM32_SIZE_WORD:     return DMA_PDATAALIGN_WORD;
    default:                          return DMA_PDATAALIGN_BYTE;
    }
}

static uint32_t hal_to_hal_priority(hal_dma_stm32_prio_t p)
{
    switch (p)
    {
    case HAL_DMA_STM32_PRIO_LOW:       return DMA_PRIORITY_LOW;
    case HAL_DMA_STM32_PRIO_HIGH:      return DMA_PRIORITY_HIGH;
    case HAL_DMA_STM32_PRIO_VERY_HIGH: return DMA_PRIORITY_VERY_HIGH;
    default:                           return DMA_PRIORITY_MEDIUM;
    }
}

static uint32_t hal_to_hal_direction(hal_dma_stm32_dir_t dir)
{
    switch (dir)
    {
    case HAL_DMA_STM32_DIR_M2P: return DMA_MEMORY_TO_PERIPH;
    case HAL_DMA_STM32_DIR_M2M: return DMA_MEMORY_TO_MEMORY;
    default:                    return DMA_PERIPH_TO_MEMORY;
    }
}

static int hal_dma_setup_internal(int dts_id, hal_dma_callback_t cb, void* arg)
{
    int idx = node_index_from_dts_id(dts_id);
    if (idx < 0 || !s_dma_inited[idx])
        return VFS_ERR_NODEV;

    s_dma_cbs[idx]     = cb;
    s_dma_cb_args[idx] = arg;
    return VFS_OK;
}

int hal_dma_stm32_stream_setup(const hal_dma_stm32_xfer_t* cfg)
{
    return hal_dma_stm32_stream_setup_async(cfg, NULL, NULL);
}

int hal_dma_stm32_stream_setup_async(const hal_dma_stm32_xfer_t* cfg,
                                      hal_dma_callback_t cb, void* arg)
{
    DMA_HandleTypeDef* hdma;
    int                idx;

    if (!cfg)
        return VFS_ERR_INVAL;

    idx = node_index_from_dts_id(cfg->dts_id);
    if (idx < 0 || !s_dma_inited[idx])
        return VFS_ERR_NODEV;

    hdma = &s_dma_handles[idx];

    hdma->Init.Direction           = hal_to_hal_direction(cfg->direction);
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = hal_to_hal_data_size(cfg->data_size);
    hdma->Init.MemDataAlignment    = hal_to_hal_data_size(cfg->data_size);
    hdma->Init.Mode                = DMA_NORMAL;
    hdma->Init.Priority            = hal_to_hal_priority(cfg->priority);
    hdma->Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(hdma) != HAL_OK)
        return VFS_ERR_IO;

    return hal_dma_setup_internal(cfg->dts_id, cb, arg);
}

void hal_dma_stm32_stream_enable(int dts_id)
{
    DMA_HandleTypeDef* hdma = &s_dma_handles[node_index_from_dts_id(dts_id)];
    if (!hdma || !hdma->Instance)
        return;

    /* 实际启动需要源/目标地址和长度，这里仅清空状态供上层使用 HAL_DMA_Start */
    __HAL_DMA_CLEAR_FLAG(hdma, __HAL_DMA_GET_TC_FLAG_INDEX(hdma));
    __HAL_DMA_CLEAR_FLAG(hdma, __HAL_DMA_GET_TE_FLAG_INDEX(hdma));
    __HAL_DMA_ENABLE_IT(hdma, DMA_IT_TC);
}

int hal_dma_stm32_stream_disable(int dts_id, uint32_t timeout_ms)
{
    int idx = node_index_from_dts_id(dts_id);
    if (idx < 0 || !s_dma_inited[idx])
        return VFS_ERR_NODEV;

    COMPAT_IGNORE_RESULT(timeout_ms);
    HAL_DMA_Abort(&s_dma_handles[idx]);
    return VFS_OK;
}

int hal_dma_stm32_stream_poll(int dts_id, uint32_t timeout_ms)
{
    int idx = node_index_from_dts_id(dts_id);
    uint32_t start;

    if (idx < 0 || !s_dma_inited[idx])
        return VFS_ERR_NODEV;

    start = HAL_GetTick();
    while (HAL_DMA_GetState(&s_dma_handles[idx]) != HAL_DMA_STATE_READY)
    {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
    return VFS_OK;
}

void hal_dma_force_stop(void)
{
    for (int i = 0; i < s_dma_node_count; i++)
    {
        if (s_dma_inited[i])
            HAL_DMA_Abort(&s_dma_handles[i]);
    }
}

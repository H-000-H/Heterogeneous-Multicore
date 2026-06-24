/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * STM32F4 DMA 驱动 — 供 DMA Engine 后端使用
 *
 * 每个 DMA 流在 DTS 中用一个 dts_id 唯一标识，内部映射到
 * DMA_TypeDef + Stream + Channel + IRQ。
 */
#ifndef HAL_DMA_STM32_H
#define HAL_DMA_STM32_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_DMA_XFER_TIMEOUT_MS 5000U

typedef enum {
    HAL_DMA_STM32_DIR_P2M = 0,
    HAL_DMA_STM32_DIR_M2P,
    HAL_DMA_STM32_DIR_M2M
} hal_dma_stm32_dir_t;

typedef enum {
    HAL_DMA_STM32_SIZE_BYTE = 0,
    HAL_DMA_STM32_SIZE_HALFWORD,
    HAL_DMA_STM32_SIZE_WORD
} hal_dma_stm32_size_t;

typedef enum {
    HAL_DMA_STM32_PRIO_LOW = 0,
    HAL_DMA_STM32_PRIO_MEDIUM,
    HAL_DMA_STM32_PRIO_HIGH,
    HAL_DMA_STM32_PRIO_VERY_HIGH
} hal_dma_stm32_prio_t;

/* DMA 节点描述 */
typedef struct {
    int       dts_id;
    uint8_t   dma_idx;   /* 0: DMA1, 1: DMA2 */
    uint8_t   stream;    /* 0~7 */
    uint8_t   channel;   /* 0~7 */
    uint8_t   irqn;      /* NVIC IRQ number */
} hal_dma_dts_node_t;

/* 传输配置 */
typedef struct {
    int                     dts_id;
    uint32_t                len;
    hal_dma_stm32_dir_t     direction;
    hal_dma_stm32_size_t    data_size;
    hal_dma_stm32_prio_t    priority;
    uint32_t                periph_addr;
    uint32_t                mem_addr;
    uint32_t                flags;
} hal_dma_stm32_xfer_t;

typedef void (*hal_dma_callback_t)(void* arg);

/* 初始化和查询 */
int                        hal_dma_stm32_init(void);
int                        hal_dma_stm32_register_from_props(uint32_t dts_id,
                                                              int controller,
                                                              int stream,
                                                              int channel)
    COMPAT_WARN_UNUSED_RESULT;
const hal_dma_dts_node_t*  hal_dma_stm32_lookup(int dts_id) COMPAT_WARN_UNUSED_RESULT;

/* 单次/异步传输设置 */
int hal_dma_stm32_stream_setup(const hal_dma_stm32_xfer_t* cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_dma_stm32_stream_setup_async(const hal_dma_stm32_xfer_t* cfg,
                                      hal_dma_callback_t cb, void* arg) COMPAT_WARN_UNUSED_RESULT;

/* 启动/停止 */
void hal_dma_stm32_stream_enable(int dts_id);
int  hal_dma_stm32_stream_disable(int dts_id, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/* 轮询等待完成 */
int hal_dma_stm32_stream_poll(int dts_id, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/* 紧急停止所有 DMA */
void hal_dma_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DMA_STM32_H */

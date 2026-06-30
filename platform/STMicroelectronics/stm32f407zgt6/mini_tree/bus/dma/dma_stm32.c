/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DMA Capability — STM32F4 HAL backend
 *
 * 基于 HAL_DMA_Start / HAL_DMA_PollForTransfer，不直接操作寄存器。
 */
#include "dma.h"
#include "dma_internal.h"
#include "hal_dma_stm32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include <string.h>

/**
 * @brief DMA ISR 桥接回调 (HAL 回调转调用户回调)
 * @param arg 通道上下文 (struct bus_dma_chan*)
 */
static void bus_dma_stm32_isr_bridge(void* arg)
{
    struct bus_dma_chan* chan = (struct bus_dma_chan*)arg;
    if (chan && chan->cb)
        chan->cb(chan, chan->cb_arg);
}

/**
 * @brief STM32 DMA 通道申请 (校验 dts_id 是否存在于 HAL 查找表)
 * @param chan DMA 通道指针
 * @return 成功返回 VFS_OK, 未找到返回 VFS_ERR_NODEV
 */
static int bus_dma_stm32_request(struct bus_dma_chan* chan)
{
    if (!hal_dma_stm32_lookup((int)chan->dts_id))
        return VFS_ERR_NODEV;
    return VFS_OK;
}

/**
 * @brief STM32 DMA 通道释放 (空操作, 资源在 release_chan 中清零)
 * @param chan DMA 通道指针
 */
static void bus_dma_stm32_release(struct bus_dma_chan* chan)
{
    COMPAT_IGNORE_RESULT(chan);
}

/**
 * @brief 将 bus DMA 方向枚举转换为 STM32 HAL 方向枚举
 * @param dir bus DMA 方向
 * @return STM32 HAL 方向枚举值
 */
static hal_dma_stm32_dir_t to_hal_dir(bus_dma_dir_t dir)
{
    return (dir == BUS_DMA_DIR_MEM_TO_PERIPH) ? HAL_DMA_STM32_DIR_M2P : HAL_DMA_STM32_DIR_P2M;
}

/**
 * @brief 将 bus DMA 数据宽度枚举转换为 STM32 HAL size 枚举
 * @param w bus DMA 数据宽度
 * @return STM32 HAL size 枚举值
 */
static hal_dma_stm32_size_t to_hal_size(bus_dma_width_t w)
{
    if (w == BUS_DMA_WIDTH_WORD)     return HAL_DMA_STM32_SIZE_WORD;
    if (w == BUS_DMA_WIDTH_HALFWORD) return HAL_DMA_STM32_SIZE_HALFWORD;
    return HAL_DMA_STM32_SIZE_BYTE;
}

/**
 * @brief STM32 DMA 提交传输 (配置 stream 并启动, 有回调走 async, 无回调走同步)
 * @param chan DMA 通道指针
 * @param xfer 传输描述符
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 HAL 错误码
 */
static int bus_dma_stm32_submit(struct bus_dma_chan* chan,
                                 const bus_dma_xfer_t* xfer)
{
    hal_dma_stm32_xfer_t cfg = {0};
    int                  ret;

    if (!chan || !xfer)
        return VFS_ERR_INVAL;

    cfg.dts_id      = chan->dts_id;
    cfg.len         = (uint32_t)xfer->len;
    cfg.direction   = to_hal_dir(xfer->dir);
    cfg.data_size   = to_hal_size(xfer->width);
    cfg.priority    = HAL_DMA_STM32_PRIO_MEDIUM;
    cfg.flags       = xfer->flags;

    if (xfer->dir == BUS_DMA_DIR_MEM_TO_PERIPH)
    {
        cfg.periph_addr = (uint32_t)xfer->dst;
        cfg.mem_addr    = (uint32_t)xfer->src;
    }
    else
    {
        cfg.periph_addr = (uint32_t)xfer->src;
        cfg.mem_addr    = (uint32_t)xfer->dst;
    }

    if (chan->cb)
        ret = hal_dma_stm32_stream_setup_async(&cfg, bus_dma_stm32_isr_bridge, chan);
    else
        ret = hal_dma_stm32_stream_setup(&cfg);

    if (ret != VFS_OK)
        return ret;

    hal_dma_stm32_stream_enable(chan->dts_id);
    return VFS_OK;
}

/**
 * @brief STM32 DMA 等待传输完成 (轮询 poll)
 * @param chan DMA 通道指针
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT, 无效返回 VFS_ERR_INVAL
 */
static int bus_dma_stm32_wait(struct bus_dma_chan* chan, uint32_t timeout_ms)
{
    int ret;
    if (!chan)
        return VFS_ERR_INVAL;
    ret = hal_dma_stm32_stream_poll((int)chan->dts_id, timeout_ms);
    return (ret == VFS_OK) ? VFS_OK : VFS_ERR_TIMEOUT;
}

/**
 * @brief STM32 DMA 中止传输 (disable stream)
 * @param chan DMA 通道指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 HAL 错误码
 */
static int bus_dma_stm32_abort(struct bus_dma_chan* chan)
{
    if (!chan)
        return VFS_ERR_INVAL;
    return hal_dma_stm32_stream_disable((int)chan->dts_id, HAL_DMA_XFER_TIMEOUT_MS);
}

/**
 * @brief STM32 DMA 查询通道忙碌状态 (通过 HAL lookup 判断)
 * @param chan DMA 通道指针
 * @return 忙碌返回 1, 空闲或无效返回 0
 */
static int bus_dma_stm32_busy(struct bus_dma_chan* chan)
{
    const hal_dma_dts_node_t* info;
    if (!chan)
        return 0;
    info = hal_dma_stm32_lookup((int)chan->dts_id);
    return info ? 1 : 0;
}

const struct bus_dma_soc_ops g_bus_dma_soc_ops = {
    .init       = hal_dma_stm32_init,
    .force_stop = hal_dma_force_stop,
    .request    = bus_dma_stm32_request,
    .release    = bus_dma_stm32_release,
    .submit     = bus_dma_stm32_submit,
    .wait       = bus_dma_stm32_wait,
    .abort      = bus_dma_stm32_abort,
    .busy       = bus_dma_stm32_busy,
};

/* SPDX-License-Identifier: Apache-2.0 */

/*

 * DMA Capability — CH32 backend（暂用固定通道映射，后续可接 DTS phandle）

 */

#include "dma.h"

#include "dma_internal.h"

#include "hal_dma.h"

#include "VFS.h"



#include <string.h>



void hal_dma_ch32_init(void);

void hal_dma_force_stop(void);



/**
 * @brief CH32 DMA SoC 初始化 (调用 hal_dma_ch32_init)
 * @return 成功返回 VFS_OK
 */
static int bus_dma_ch32_soc_init(void)

{

    hal_dma_ch32_init();

    return VFS_OK;

}



/**
 * @brief CH32 DMA 通道申请 (空操作, 暂用固定通道映射)
 * @param chan DMA 通道指针
 * @return 成功返回 VFS_OK
 */
static int bus_dma_ch32_request(struct bus_dma_chan* chan)

{

    COMPAT_IGNORE_RESULT(chan);

    return VFS_OK;

}



/**
 * @brief CH32 DMA 通道释放 (空操作)
 * @param chan DMA 通道指针
 */
static void bus_dma_ch32_release(struct bus_dma_chan* chan)

{

    COMPAT_IGNORE_RESULT(chan);

}



/**
 * @brief CH32 DMA 提交传输 (暂未实现, 返回 VFS_ERR_NODEV)
 * @param chan DMA 通道指针
 * @param xfer 传输描述符
 * @return 固定返回 VFS_ERR_NODEV
 */
static int bus_dma_ch32_submit(struct bus_dma_chan* chan, const bus_dma_xfer_t* xfer)

{

    COMPAT_IGNORE_RESULT(chan);

    COMPAT_IGNORE_RESULT(xfer);

    return VFS_ERR_NODEV;

}



/**
 * @brief CH32 DMA 等待传输完成 (暂未实现, 直接返回 VFS_OK)
 * @param chan DMA 通道指针
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK
 */
static int bus_dma_ch32_wait(struct bus_dma_chan* chan, uint32_t timeout_ms)

{

    COMPAT_IGNORE_RESULT(chan);

    COMPAT_IGNORE_RESULT(timeout_ms);

    return VFS_OK;

}



/**
 * @brief CH32 DMA 中止传输 (暂未实现, 直接返回 VFS_OK)
 * @param chan DMA 通道指针
 * @return 成功返回 VFS_OK
 */
static int bus_dma_ch32_abort(struct bus_dma_chan* chan)

{

    COMPAT_IGNORE_RESULT(chan);

    return VFS_OK;

}



/**
 * @brief CH32 DMA 查询通道忙碌状态 (暂未实现, 固定返回 0)
 * @param chan DMA 通道指针
 * @return 固定返回 0
 */
static int bus_dma_ch32_busy(struct bus_dma_chan* chan)

{

    COMPAT_IGNORE_RESULT(chan);

    return 0;

}



const struct bus_dma_soc_ops g_bus_dma_soc_ops = {

    .init       = bus_dma_ch32_soc_init,

    .force_stop = hal_dma_force_stop,

    .request    = bus_dma_ch32_request,

    .release    = bus_dma_ch32_release,

    .submit     = bus_dma_ch32_submit,

    .wait       = bus_dma_ch32_wait,

    .abort      = bus_dma_ch32_abort,

    .busy       = bus_dma_ch32_busy,

};


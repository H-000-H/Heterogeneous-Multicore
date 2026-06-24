/* SPDX-License-Identifier: GPL-2.0-or-later */

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



static int bus_dma_ch32_soc_init(void)

{

    hal_dma_ch32_init();

    return VFS_OK;

}



static int bus_dma_ch32_request(struct bus_dma_chan* chan)

{

    (void)chan;

    return VFS_OK;

}



static void bus_dma_ch32_release(struct bus_dma_chan* chan)

{

    (void)chan;

}



static int bus_dma_ch32_submit(struct bus_dma_chan* chan, const bus_dma_xfer_t* xfer)

{

    (void)chan;

    (void)xfer;

    return VFS_ERR_NODEV;

}



static int bus_dma_ch32_wait(struct bus_dma_chan* chan, uint32_t timeout_ms)

{

    (void)chan;

    (void)timeout_ms;

    return VFS_OK;

}



static int bus_dma_ch32_abort(struct bus_dma_chan* chan)

{

    (void)chan;

    return VFS_OK;

}



static int bus_dma_ch32_busy(struct bus_dma_chan* chan)

{

    (void)chan;

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


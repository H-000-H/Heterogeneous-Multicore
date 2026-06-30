/* SPDX-License-Identifier: Apache-2.0 */
#include "dma.h"
#include "dma_internal.h"
#include "VFS.h"

const struct bus_dma_soc_ops g_bus_dma_soc_ops = {
    .init       = NULL,
    .force_stop = NULL,
    .request    = NULL,
    .release    = NULL,
    .submit     = NULL,
    .wait       = NULL,
    .abort      = NULL,
    .busy       = NULL,
};

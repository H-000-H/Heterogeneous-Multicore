#include "board_dma.h"
#include "VFS.h"

int board_dma_id_from_phandle(const struct device* dev, const char* prop, int* out_id)
{
    (void)dev;
    (void)prop;
    if (!out_id)
        return VFS_ERR_INVAL;
    *out_id = -1;
    return VFS_ERR_NODEV;
}

int board_dma_register_channels(void)
{
    return VFS_OK;
}

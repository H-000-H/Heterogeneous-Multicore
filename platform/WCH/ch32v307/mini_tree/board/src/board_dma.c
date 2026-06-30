/*
 * board_dma.c — 板级 DMA 通道注册实现 (stub)
 *
 * 本平台无独立 DMA 控制器, 提供空实现保持 board 层链接接口一致.
 * board_dma_id_from_phandle 返回 VFS_ERR_NODEV,
 * board_dma_register_channels 直接返回 VFS_OK.
 */
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

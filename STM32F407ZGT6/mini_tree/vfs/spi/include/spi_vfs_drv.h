#ifndef SPI_VFS_DRV_H
#define SPI_VFS_DRV_H

/**
 * SPI VFS 驱动内部头 — 仅 vfs/spi/{bus,master,slave}/ 下 .c 使用。
 * 包含 SPI_CMD_DEINIT，且不 poison；应用层请 include spi_vfs.h。
 */
#define SPI_VFS_PUBLIC_POISON_DEINIT 0
#include "spi_vfs.h"

#endif /* SPI_VFS_DRV_H */

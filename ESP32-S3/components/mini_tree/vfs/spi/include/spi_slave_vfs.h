#ifndef SPI_SLAVE_VFS_H
#define SPI_SLAVE_VFS_H

#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

int spi_slave_vfs_probe(struct device* dev);
int spi_slave_vfs_remove(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* SPI_SLAVE_VFS_H */

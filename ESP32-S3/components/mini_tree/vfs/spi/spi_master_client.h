#ifndef SPI_MASTER_CLIENT_H
#define SPI_MASTER_CLIENT_H

#include "device.h"

int spi_master_client_probe(struct device* dev);
int spi_master_client_remove(struct device* dev);

#endif /* SPI_MASTER_CLIENT_H */

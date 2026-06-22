#ifndef SPI_SLAVE_CLIENT_H
#define SPI_SLAVE_CLIENT_H

#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

int spi_slave_client_probe(struct device* dev);
int spi_slave_client_remove(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* SPI_SLAVE_CLIENT_H */

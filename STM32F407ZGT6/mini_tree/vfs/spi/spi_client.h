#ifndef SPI_CLIENT_H
#define SPI_CLIENT_H

#include "device.h"

#ifdef __cplusplus
extern "C" 
{
#endif

int spi_client_probe(struct device* dev);
int spi_client_remove(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* SPI_CLIENT_H */


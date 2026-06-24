#ifndef SPI_MASTER_CLIENT_H
#define SPI_MASTER_CLIENT_H

#include "device.h"
#include "hal_spi.h"
#include "osal.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spi_master_client
{
    struct file_operations ops;
    struct hal_spi_dev     dev;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

typedef struct spi_master_client spi_client;

int spi_master_client_probe(struct device* dev);
int spi_master_client_remove(struct device* dev);
int spi_master_client_bind(struct device* dev, struct spi_master_client* client,
                           uint8_t* mutex_storage, size_t mutex_storage_size,
                           int pool_idx);
void spi_master_client_unbind(struct device* dev, struct spi_master_client* client);
int spi_master_client_hw_open(struct spi_master_client* client);
void spi_master_client_hw_close(struct spi_master_client* client);
int spi_master_client_transfer(struct spi_master_client* client, const uint8_t* tx,
                               uint8_t* rx, size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SPI_MASTER_CLIENT_H */

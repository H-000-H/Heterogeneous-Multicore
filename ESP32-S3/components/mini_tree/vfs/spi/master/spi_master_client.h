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

/* SPI 总线客户端 (Linux struct spi_device + spi_sync 对应层) */
struct spi_master_client
{
    struct file_operations ops;
    struct hal_spi_ctx     ctx;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

int spi_master_client_probe(struct device* dev);
int spi_master_client_remove(struct device* dev);

/*
 * 功能驱动 (如 w25q64) 内嵌 client 时使用；pool_idx 须由调用方预先 claim。
 * 不设置 dev->ops / device_set_priv。
 */
int spi_master_client_bind(struct device* dev, struct spi_master_client* client,
                           uint8_t* mutex_storage, size_t mutex_storage_size,
                           int pool_idx);

void spi_master_client_unbind(struct device* dev, struct spi_master_client* client);

int spi_master_client_interface_attach(struct spi_master_client* client);
void spi_master_client_interface_detach(struct spi_master_client* client);

/* 全双工传输 (spi_sync)；调用方已持 dev_lc I/O 锁时调用 */
int spi_master_client_transfer(struct spi_master_client* client, const uint8_t* tx,
                               uint8_t* rx, size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SPI_MASTER_CLIENT_H */

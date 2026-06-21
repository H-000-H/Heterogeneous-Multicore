#ifndef HAL_SPI_H
#define HAL_SPI_H

#include "hal_spi_bus.h"
#include "hal_spi_bus_host.h"

#ifdef __cplusplus
extern "C" 
{
#endif

/* SPI interface 实例 (由 spi_master_client / spi_slave_client 等持有) */
struct hal_spi_ctx
{
    struct hal_spi_bus_host*    host;
    struct hal_spi_device_config cfg;
    int                         pool_idx;
    int                         attached;
};

void hal_spi_ctx_init(struct hal_spi_ctx* ctx, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);

void hal_spi_ctx_attach(struct hal_spi_ctx* ctx);
void hal_spi_ctx_detach(struct hal_spi_ctx* ctx);

/* 下半部: 等待已 queue 的从机事务完成, 可选取回 RX (trans_len 为实际接收字节数) */
int hal_spi_get_trans_result(struct hal_spi_ctx* ctx, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms);

/* Master 全双工传输 (内部持 xfer 会话) */
int hal_spi_transfer(struct hal_spi_ctx* ctx, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */


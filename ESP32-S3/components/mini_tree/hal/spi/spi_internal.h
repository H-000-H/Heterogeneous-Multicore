#ifndef SPI_INTERNAL_H
#define SPI_INTERNAL_H

#include "hal_spi.h"
#include <stddef.h>
#include <stdint.h>

struct hal_spi_hw;

struct hal_spi_hw* spi_dev_hw(const struct hal_spi_dev* dev);

int spi_controller_xfer(struct hal_spi_bus_host* host, struct hal_spi_hw* hw,
                        const uint8_t* tx, uint8_t* rx, size_t len);

int spi_controller_apply_dev_cfg(struct hal_spi_dev* dev);

int spi_slave_bus_reconfigure(struct hal_spi_bus_host* host,
                              const struct hal_spi_device_config* dev_cfg);

int spi_slave_controller_xfer(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                              const uint8_t* tx, uint8_t* rx, size_t len,
                              uint32_t timeout_ms);

int spi_slave_queue_tx_internal(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                const uint8_t* data, size_t len, uint32_t timeout_ms);

void spi_dev_hw_slot_init(int pool_idx, int is_master);

#endif /* SPI_INTERNAL_H */

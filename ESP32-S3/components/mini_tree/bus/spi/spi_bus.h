/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct spi_bus_client;

#define SPI_BUS_ROLE_MASTER 0
#define SPI_BUS_ROLE_SLAVE  1

struct spi_bus_client_config {
    int mode;
    int clock_speed_hz;
    int cs_pin;
    int queue_size;
};

int spi_bus_host_probe(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int spi_bus_host_remove(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int spi_bus_child_host_role(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int spi_bus_client_register(struct device* dev,
                             const struct spi_bus_client_config* cfg,
                             struct spi_bus_client** out)
    COMPAT_WARN_UNUSED_RESULT;
void spi_bus_client_unregister(struct device* dev);
int spi_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int spi_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int spi_bus_transfer(struct device* dev,
                      const uint8_t* tx, uint8_t* rx,
                      size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int spi_bus_slave_sync(struct device* dev,
                        const uint8_t* tx, uint8_t* rx,
                        size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int spi_bus_slave_queue_tx(struct device* dev,
                            const uint8_t* data, size_t len,
                            uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int spi_bus_slave_get_trans_result(struct device* dev,
                                    uint8_t* rx_data, size_t rx_cap,
                                    size_t* trans_len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* SPI_BUS_H */

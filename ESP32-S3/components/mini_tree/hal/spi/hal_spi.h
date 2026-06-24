#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal_gpio.h"
#include "compiler_compat.h"

struct osal_mutex;

#ifdef __cplusplus
extern "C"
{
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

struct hal_spi_device_config
{
    int mode;
    int clock_speed_hz;
    hal_pin_t cs_pin;
    int queue_size;
};

struct hal_spi_bus_config
{
    int host_id;
    hal_pin_t mosi;
    hal_pin_t miso;
    hal_pin_t sclk;
    int max_transfer_sz;
    int dma_chan;
    int bus_role;
};

struct hal_spi_bus_host
{
    struct hal_spi_bus_config       cfg;
    struct osal_mutex*              bus_mutex;
    int                             ref_count;
    int                             bus_ready;
    int                             hw_inited;
    struct hal_spi_device_config    active_cfg;
};

struct hal_spi_dev
{
    struct hal_spi_bus_host*        ctlr;
    struct hal_spi_device_config    cfg;
    int                             pool_idx;
    int                             hw_open;
};

typedef struct hal_spi_device_config spi_device_config;
typedef struct hal_spi_bus_config    spi_bus_config;
typedef struct hal_spi_bus_host      spi_controller;
typedef struct hal_spi_dev           spi_device;

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out) COMPAT_WARN_UNUSED_RESULT;

void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);
void hal_spi_dev_register(struct hal_spi_dev* dev);
void hal_spi_dev_unregister(struct hal_spi_dev* dev);

int hal_spi_dev_hw_open(struct hal_spi_dev* dev);
int hal_spi_dev_hw_close(struct hal_spi_dev* dev);

int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms);

static inline int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    return spi_sync(dev, tx, rx, len, timeout_ms);
}

int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms);

int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms);

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */

#ifndef HAL_SPI_BUS_H
#define HAL_SPI_BUS_H

#include <stdint.h>
#include <stddef.h>
#include "bus/bus.h"
#include "hal_pin.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

/* SPI 总线控制器配置 (引脚/host 级, 不含设备 CS/mode) */
struct hal_spi_bus_config
{
    int host_id;
    hal_pin_t mosi;
    hal_pin_t miso;
    hal_pin_t sclk;
    int max_transfer_sz;
    int dma_chan;
    int bus_role;   /* HAL_SPI_BUS_ROLE_SLAVE | HAL_SPI_BUS_ROLE_MASTER */
};

/* SPI 设备配置 */
struct hal_spi_device_config
{
    int mode;
    int clock_speed_hz;
    hal_pin_t cs_pin;
    int queue_size;
};

struct hal_spi_bus_host;

/* Slave 异步入队: 映射到 bus_ops.transfer_async */
static inline int hal_spi_bus_queue_tx(bus_device_t* bus, const uint8_t* data, size_t len)
{
    if (!bus || !bus->ops || !bus->ops->transfer_async || !data || len == 0)
        return -1;
    return bus->ops->transfer_async(bus, data, len, NULL, 0, NULL);
}

static inline int hal_spi_bus_supports_async_tx(const bus_device_t* bus)
{
    return bus && bus->ops && bus->ops->transfer_async;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_H */

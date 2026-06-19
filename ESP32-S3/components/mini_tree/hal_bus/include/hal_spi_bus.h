#ifndef HAL_SPI_BUS_H
#define HAL_SPI_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

/* SPI 总线配置 */
struct hal_spi_bus_config
{
    int host_id;
    int mosi;
    int miso;
    int sclk;
    int max_transfer_sz;
    int dma_chan;
    int bus_role;   /* HAL_SPI_BUS_ROLE_SLAVE | HAL_SPI_BUS_ROLE_MASTER */
};

/* SPI 设备配置 */
struct hal_spi_device_config
{
    int mode;
    int clock_speed_hz;
    int cs_pin;
    int queue_size;
};

/* 总线传输 vtable (生命周期由 hal_spi_bus_host 管理, 无 init/deinit) */
struct hal_spi_bus
{
    int (*write)(struct hal_spi_bus* bus, const uint8_t* data, size_t len);
    int (*write_top_half)(struct hal_spi_bus* bus, const uint8_t* data, size_t len);
    int (*read)(struct hal_spi_bus* bus, uint8_t* data, size_t len);
    void* _impl;
};

void hal_spi_bus_init_struct(struct hal_spi_bus* bus, int bus_role);

int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms);
int hal_spi_unlock_bus(int bus_id);
int hal_spi_assert_cs(int bus_id, int cs_line);
int hal_spi_deassert_cs(int bus_id, int cs_line);
void hal_spi_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_H */


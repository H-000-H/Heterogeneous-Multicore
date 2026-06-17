#ifndef HAL_SPI_BUS_HOST_H
#define HAL_SPI_BUS_HOST_H

#include "hal_spi_bus.h"

struct osal_mutex;
struct hal_spi_ctx;

#ifdef __cplusplus
extern "C" {
#endif

struct hal_spi_bus_host
{
    struct hal_spi_bus          bus;
    struct hal_spi_bus_config   cfg;
    struct osal_mutex*          bus_mutex;
    int                         ref_count;
    int                         bus_ready;
    int                         hw_inited;
    struct hal_spi_device_config active_cfg;
    struct hal_spi_ctx*         active_ctx;
};

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
struct hal_spi_bus_host* hal_spi_bus_host_get(int host_id);

int hal_spi_interface_attach(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg);
int hal_spi_interface_detach(struct hal_spi_bus_host* host);

int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg);

int hal_spi_xfer_begin(struct hal_spi_ctx* ctx, uint32_t timeout_ms);
int hal_spi_xfer_end(struct hal_spi_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_HOST_H */

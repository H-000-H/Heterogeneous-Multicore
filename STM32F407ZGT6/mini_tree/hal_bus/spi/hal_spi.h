#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "bus/bus.h"
#include "hal_gpio.h"
#include "compiler_compat.h"

struct osal_mutex;
struct hal_spi_dev;

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*SPI 角色与总线/设备配置*/
/*===========================================================================================================================================================*/
#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

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

struct hal_spi_device_config
{
    int mode;
    int clock_speed_hz;
    hal_pin_t cs_pin;
    int queue_size;
};
/*===========================================================================================================================================================*/

                                                            /*总线 Host 实体*/
/*===========================================================================================================================================================*/
struct hal_spi_bus_host
{
    bus_device_t                    dev;
    struct hal_spi_bus_config       cfg;
    struct osal_mutex*              bus_mutex;
    int                             ref_count;
    int                             bus_ready;
    int                             hw_inited;
    struct hal_spi_device_config    active_cfg;
    struct hal_spi_dev*             active_dev;   /* I/O 会话期间绑定的设备 */
};
/*===========================================================================================================================================================*/

                                                            /*SPI 设备实例 (VFS client 持有)*/
/*===========================================================================================================================================================*/
struct hal_spi_dev
{
    struct hal_spi_bus_host*        host;
    struct hal_spi_device_config    cfg;
    int                             pool_idx;
    int                             hw_open;      /* 已通过 hal_spi_dev_hw_open 绑定硬件 */
};
/*===========================================================================================================================================================*/

                                                            /*总线 inline 辅助*/
/*===========================================================================================================================================================*/
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

static inline bus_device_t* hal_spi_host_bus(struct hal_spi_bus_host* host)
{
    return host ? &host->dev : NULL;
}
/*===========================================================================================================================================================*/

                                                            /*Bus 层 — probe 生命周期*/
/*===========================================================================================================================================================*/
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*设备实例 — 初始化与路由注册*/
/*===========================================================================================================================================================*/
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);
void hal_spi_dev_register(struct hal_spi_dev* dev);
void hal_spi_dev_unregister(struct hal_spi_dev* dev);
/*===========================================================================================================================================================*/

                                                            /*设备实例 — 硬件 open/close*/
/*===========================================================================================================================================================*/
int hal_spi_dev_hw_open(struct hal_spi_dev* dev);
int hal_spi_dev_hw_close(struct hal_spi_dev* dev);
/*===========================================================================================================================================================*/

                                                            /*I/O 会话与传输*/
/*===========================================================================================================================================================*/
int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg);
int hal_spi_xfer_begin(struct hal_spi_dev* dev, uint32_t timeout_ms);
int hal_spi_xfer_end(struct hal_spi_dev* dev);

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms);
int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */

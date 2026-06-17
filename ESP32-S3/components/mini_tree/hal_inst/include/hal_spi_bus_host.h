#ifndef HAL_SPI_BUS_HOST_H
#define HAL_SPI_BUS_HOST_H

#include "hal_spi_bus.h"

struct osal_mutex;
struct hal_spi_ctx;

#ifdef __cplusplus
extern "C" {
#endif

/* SPI 总线控制器实体 (全局每 host_id 一份, 由 esp32,spi-host probe 初始化) */
struct hal_spi_bus_host
{
    struct hal_spi_bus              bus;
    struct hal_spi_bus_config       cfg;
    struct osal_mutex*              bus_mutex;
    int                             ref_count;     /* interface open 引用计数 */
    int                             bus_ready;     /* probe 已完成总线级准备 */
    int                             hw_inited;     /* 硬件已初始化 (ESP32 slave: spi_slave_initialize) */
    struct hal_spi_device_config    active_cfg;
    struct hal_spi_ctx*             active_ctx;    /* 当前 I/O 绑定的 interface */
};

/* Bus 层 — esp32,spi-host probe 时调用, 每个 host_id 全局一次 */
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
struct hal_spi_bus_host* hal_spi_bus_host_get(int host_id);

/* Interface 层 — esp32,spi-device open/close 调用, 不 init/deinit 总线控制器 */
int hal_spi_interface_attach(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg);
int hal_spi_interface_detach(struct hal_spi_bus_host* host);

/* I/O 前 runtime 切换 (master 多 CS; ESP32 slave 首次 attach 后多为 no-op) */
int hal_spi_bus_reconfigure(struct hal_spi_bus_host* host,
                            const struct hal_spi_device_config* dev_cfg);

/* I/O 会话: 持 bus 锁 + reconfigure + 绑定 active_ctx */
int hal_spi_xfer_begin(struct hal_spi_ctx* ctx, uint32_t timeout_ms);
int hal_spi_xfer_end(struct hal_spi_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_HOST_H */

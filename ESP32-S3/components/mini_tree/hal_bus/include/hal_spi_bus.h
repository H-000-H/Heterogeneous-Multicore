#ifndef HAL_SPI_BUS_H
#define HAL_SPI_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SPI 总线配置 */
struct hal_spi_bus_config
{
    int host_id;            /* SPI 控制器编号, 0 = SPI1 */
    int mosi;
    int miso;
    int sclk;
    int max_transfer_sz;    /* 最大传输字节 */
    int dma_chan;           /* DMA 通道, -1 = 自动 */
};

/* SPI 设备配置 (从机模式: clock_speed_hz 由主机决定, 本地忽略) */
struct hal_spi_device_config
{
    int mode;               /* SPI 模式 0-3 */
    int clock_speed_hz;     /* 主机时钟(Hz), 从机侧仅记录, 不参与初始化 */
    int cs_pin;             /* 片选输入引脚 */
    int queue_size;         /* 传输队列深度 */
};

/* 总线传输 vtable (生命周期由 hal_spi_bus_host 管理, 无 init/deinit) */
struct hal_spi_bus
{
    int (*write)(struct hal_spi_bus* bus, const uint8_t* data, size_t len);
    int (*write_top_half)(struct hal_spi_bus* bus, const uint8_t* data, size_t len);
    int (*read)(struct hal_spi_bus* bus, uint8_t* data, size_t len);
    void* _impl;
};

void hal_spi_bus_init_struct(struct hal_spi_bus* bus);

/* 总线级互斥锁 (防止多设备共线时序踩踏) */
int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms);
int hal_spi_unlock_bus(int bus_id);

/* 片选控制 (由 HAL 接管, 确保多设备分时访问) */
int hal_spi_assert_cs(int bus_id, int cs_line);
int hal_spi_deassert_cs(int bus_id, int cs_line);

/* 安全停机: 复位所有 SPI 外设 (含 DMA 引擎) */
void hal_spi_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_H */

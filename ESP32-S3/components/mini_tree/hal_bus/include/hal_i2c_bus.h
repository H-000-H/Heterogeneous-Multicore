#ifndef HAL_I2C_BUS_H
#define HAL_I2C_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* I2C 总线配置 */
struct hal_i2c_config
{
    int sda_pin;
    int scl_pin;
    uint32_t clock_hz;      /* I2C 时钟频率(Hz), 如 100000 = 标准模式 */
    int port;               /* I2C 控制器编号, 0 = I2C0 */
};

struct hal_i2c_bus
{
    int (*init)(struct hal_i2c_bus* bus, const struct hal_i2c_config* cfg);
    int (*write)(struct hal_i2c_bus* bus, uint8_t addr, const uint8_t* data, size_t len, uint32_t time_out);
    int (*read)(struct hal_i2c_bus* bus, uint8_t addr, uint8_t* data, size_t len, uint32_t time_out);
    int (*write_read)(struct hal_i2c_bus* bus, uint8_t addr,
                      const uint8_t* wdata, size_t wlen,
                      uint8_t* rdata, size_t rlen, uint32_t time_out);
    int (*bus_recover)(struct hal_i2c_bus* bus);
    int (*deinit)(struct hal_i2c_bus* bus);
    void* _impl;
};

void hal_i2c_bus_init_struct(struct hal_i2c_bus* bus);
void hal_i2c_force_stop(void);

/* 总线级互斥锁 (防止多设备共线时序踩踏) */
int hal_i2c_lock_bus(int port, uint32_t timeout_ms);
int hal_i2c_unlock_bus(int port);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_BUS_H */


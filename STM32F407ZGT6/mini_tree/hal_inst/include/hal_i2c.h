#ifndef HAL_I2C_H
#define HAL_I2C_H

#include "hal_i2c_bus.h"

struct device;

#ifdef __cplusplus
extern "C" {
#endif

/* 从 struct device 获取 I2C 总线实例 (实现位于 i2c_vfs) */
struct hal_i2c_bus* device_get_i2c_bus(struct device* dev);

/* ioctl 兼容层 (已弃用, 新代码请使用 hal_i2c_bus 强类型 API) */
#define I2C_CMD_INIT        0x20
#define I2C_CMD_WRITE       0x21
#define I2C_CMD_READ        0x22
#define I2C_CMD_WRITE_READ  0x23
#define I2C_CMD_DEINIT      0x24

struct i2c_rw_arg
{
    uint8_t addr;
    uint8_t* data;
    size_t len;
    uint32_t timeout;
};

struct i2c_wr_arg
{
    uint8_t addr;
    const uint8_t* wdata;
    size_t wlen;
    uint8_t* rdata;
    size_t rlen;
    uint32_t timeout;
};

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */

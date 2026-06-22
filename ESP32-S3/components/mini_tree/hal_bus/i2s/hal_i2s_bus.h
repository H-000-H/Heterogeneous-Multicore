#ifndef HAL_I2S_BUS_H
#define HAL_I2S_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* I2S 总线配置 */
struct hal_i2s_config
{
    int ws_pin;
    int bclk_pin;
    int dout_pin;
    int din_pin;            /* -1 = 仅输出 */
    int sample_rate;        /* 采样率(Hz), 如 44100 */
    int bits_per_sample;    /* 位深: 16 或 24 */
    int channel_format;     /* 声道: 0 = 单声道, 1 = 立体声 */
};

struct hal_i2s_bus
{
    int (*init)(struct hal_i2s_bus* bus, const struct hal_i2s_config* cfg);
    int (*write)(struct hal_i2s_bus* bus, const int16_t* samples, size_t bytes,
                 size_t* written, uint32_t timeout_ms);
    int (*read)(struct hal_i2s_bus* bus, int16_t* samples, size_t bytes,
                size_t* bytes_read, uint32_t timeout_ms);
    int (*deinit)(struct hal_i2s_bus* bus);
    void* _impl;
};

void hal_i2s_bus_init_struct(struct hal_i2s_bus* bus);

/* 安全停机: 复位所有 I2S 外设 (含 DMA 引擎) */
void hal_i2s_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2S_BUS_H */


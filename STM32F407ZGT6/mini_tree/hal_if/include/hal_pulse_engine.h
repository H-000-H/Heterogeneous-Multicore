#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * 协议脉冲引擎 HAL — WS2812 / 单总线时序外设抽象
 *
 * 驱动层只调用此接口；ESP-IDF RMT 实现在 hal_pulse_engine_esp32s3.c。
 */

#define HAL_PULSE_ENGINE_MAX  4

struct hal_pulse_ws2812_hw

{
    int gpio;
    uint32_t rmt_resolution_hz;
    uint32_t rmt_mem_block;
    uint32_t rmt_queue_depth;
    uint32_t t0h_ticks;
    uint32_t t0l_ticks;
    uint32_t t1h_ticks;
    uint32_t t1l_ticks;
    uint32_t reset_ticks;
};

#ifdef __cplusplus
extern "C" 
{
#endif

int  hal_pulse_ws2812_open(int engine_id, const struct hal_pulse_ws2812_hw* hw);
int  hal_pulse_ws2812_send(int engine_id, const uint8_t* data, size_t len,
                           uint32_t timeout_ms);
void hal_pulse_ws2812_close(int engine_id);

#ifdef __cplusplus
}
#endif


/*
 * ws2812_drv.h — WS2812 LED 用户接口
 *
 * write(): len==0 为 no-op (返回 0); 否则 len 须为 bytes-per-led 整数倍
 *           且不超过 num-leds 帧长, 否则 VFS_ERR_INVAL.
 * open()/close(): 引用计数 — 仅首次 open 与末次 close 刷新全零帧.
 */
#ifndef WS2812_DRV_H
#define WS2812_DRV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define WS2812_CMD_SET_COLOR       1  /* arg: struct ws2812_color* */
#define WS2812_CMD_SET_BRIGHTNESS  2  /* arg: uint8_t* */
#define WS2812_CMD_OFF             3  /* arg: NULL */

#ifndef WS2812_DRV_TX_BUF_MAX
#define WS2812_DRV_TX_BUF_MAX  (64U * 4U)
#endif

struct ws2812_color
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

#ifdef __cplusplus
}
#endif

#endif /* WS2812_DRV_H */

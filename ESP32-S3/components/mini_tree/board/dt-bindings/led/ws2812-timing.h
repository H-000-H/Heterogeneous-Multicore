/* WS2812 / RMT 时序常量 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 10 MHz RMT 分辨率下, 800 kHz WS2812 典型 tick 值.
 * 换协议或分辨率时只改此文件; 板级 gpio/num-leds 在 &ws2812 { } 中配置.
 *
 * 注意: 仅供 dtc-lite 预处理, 勿加 #ifndef guard (会破坏宏展开).
 */

#define WS2812_RMT_RESOLUTION_HZ  10000000
#define WS2812_RMT_MEM_BLOCK      64
#define WS2812_RMT_QUEUE_DEPTH    4

#define WS2812_T0H_TICKS          4
#define WS2812_T0L_TICKS          9
#define WS2812_T1H_TICKS          8
#define WS2812_T1L_TICKS          5
#define WS2812_RESET_TICKS        500

#define WS2812_DEFAULT_TIMEOUT_MS 100
#define WS2812_DEFAULT_BRIGHTNESS 32
#define WS2812_BYTES_PER_LED      3

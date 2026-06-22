/* WS2812 / RMT 时序常量 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 * 10 MHz RMT 分辨率下 800 kHz WS2812 典型 tick; 勿加 #ifndef guard.
 */
#define DTS_WS2812_RMT_RESOLUTION_HZ  10000000
#define DTS_WS2812_RMT_MEM_BLOCK      64
#define DTS_WS2812_RMT_QUEUE_DEPTH    4
#define DTS_WS2812_T0H_TICKS          4
#define DTS_WS2812_T0L_TICKS          9
#define DTS_WS2812_T1H_TICKS          8
#define DTS_WS2812_T1L_TICKS          5
#define DTS_WS2812_RESET_TICKS        500
#define DTS_WS2812_DEFAULT_TIMEOUT_MS 100
#define DTS_WS2812_DEFAULT_BRIGHTNESS 32
#define DTS_WS2812_BYTES_PER_LED      3

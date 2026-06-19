#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* UART 配置 */
struct hal_uart_config

{
    int     uart_id;    /* UART 控制器编号, 0 = UART0 */
    int     tx_pin;
    int     rx_pin;
    int     rts_pin;    /* -1 = 未使用 */
    int     cts_pin;    /* -1 = 未使用 */
    int     baud_rate;
    int     data_bits;  /* 5、6、7、8 */
    int     stop_bits;  /* 1 或 2 */
    int     parity;     /* 0 = 无, 1 = 奇校验, 2 = 偶校验 */
};

/* UART 接收回调 */
typedef void (*hal_uart_rx_callback_t)(struct hal_uart* uart, uint8_t data, void* user_data);

struct hal_uart
{
    int (*init)(struct hal_uart* uart, const struct hal_uart_config* cfg);
    int (*write)(struct hal_uart* uart, const uint8_t* data, size_t len);
    int (*read)(struct hal_uart* uart, uint8_t* data, size_t len, uint32_t timeout_ms);
    int (*set_rx_callback)(struct hal_uart* uart, hal_uart_rx_callback_t cb, void* user_data);
    int (*deinit)(struct hal_uart* uart);
    void* _impl;
};

void hal_uart_init_struct(struct hal_uart* uart);
void hal_uart_force_stop(void);

/* ioctl 兼容层 */
#define UART_CMD_READ       0x30
#define UART_CMD_DEINIT     0x31
#define UART_CMD_SET_BAUD   0x32

struct uart_read_arg

{
    uint8_t* data;
    size_t len;
    uint32_t timeout_ms;
};

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */


#ifndef HAL_UART_H
#define HAL_UART_H

#include "compiler_compat.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif
typedef enum 
{
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN,
    UART_PARITY_ODD
} uart_parity_t;/*uart校验位*/

typedef enum 
{
    UART_STOP_BITS_1 = 0,
    UART_STOP_BITS_1_5,
    UART_STOP_BITS_2
} uart_stop_bits_t;/*停止位*/

typedef enum 
{
    UART_DATA_BITS_5 = 5,
    UART_DATA_BITS_6 = 6,
    UART_DATA_BITS_7 = 7,
    UART_DATA_BITS_8 = 8
} uart_data_bits_t;/*数据位枚举*/

struct uart_config_t
{
    uint32_t         baud_rate;    // 波特率 (如 115200)
    uart_data_bits_t data_bits;    // 数据位
    uart_parity_t    parity;       // 校验位
    uart_stop_bits_t stop_bits;    // 停止位
};

/* UART 接收回调 */
typedef void (*hal_uart_rx_callback_t)(struct hal_uart* uart, uint8_t data, void* user_data);

struct hal_uart_bus
{
    int (*init)(struct hal_uart* uart, const struct hal_uart_config* cfg);
    int (*write)(struct hal_uart* uart, const uint8_t* data, size_t len);
    int (*read)(struct hal_uart* uart, uint8_t* data, size_t len, uint32_t timeout_ms);
    int (*set_rx_callback)(struct hal_uart* uart, hal_uart_rx_callback_t cb, void* user_data);
    int (*deinit)(struct hal_uart* uart);
    void* _impl;
};

/*uart上下文结构体每个soc不同*/

int  hal_uart_force_stop(void)COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */


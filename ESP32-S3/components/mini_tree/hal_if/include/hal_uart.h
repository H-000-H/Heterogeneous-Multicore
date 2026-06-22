#ifndef HAL_UART_H
#define HAL_UART_H

#include "compiler_compat.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* UART 配置 */
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
struct hal_uart_ctx
{

};
int  hal_uart_force_stop(void)COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */


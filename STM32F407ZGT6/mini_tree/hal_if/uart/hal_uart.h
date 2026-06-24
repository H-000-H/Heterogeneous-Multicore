#ifndef HAL_UART_H
#define HAL_UART_H

#include "compiler_compat.h"
#include "hal_gpio.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_EVEN,
    HAL_UART_PARITY_ODD
} hal_uart_parity_t;

typedef enum
{
    HAL_UART_STOP_BITS_1 = 0,
    HAL_UART_STOP_BITS_1_5,
    HAL_UART_STOP_BITS_2
} hal_uart_stop_bits_t;

typedef enum
{
    HAL_UART_DATA_BITS_5 = 5,
    HAL_UART_DATA_BITS_6 = 6,
    HAL_UART_DATA_BITS_7 = 7,
    HAL_UART_DATA_BITS_8 = 8
} hal_uart_data_bits_t;

struct hal_uart_config_t
{
    uint32_t             baud_rate;
    hal_uart_data_bits_t data_bits;
    hal_uart_parity_t    parity;
    hal_uart_stop_bits_t stop_bits;
    hal_pin_t            tx_io;
    hal_pin_t            rx_io;
    int                  uart_host;
};

struct osal_mutex;

struct hal_uart_dev
{
    struct hal_uart_config_t cfg;
    int                      hw_open;
    int                      pool_idx;
    struct osal_mutex*       hal_mutex;
    bool                     hw_inited;
    volatile uint8_t         status;
};

typedef enum
{
    UART_STATE_UNINIT = 0,
    UART_STATE_READY,
    UART_STATE_BUSY,
    UART_STATE_ERROR
} uart_status_t;

typedef void (*hal_uart_rx_irq_callback_t)(struct hal_uart_config_t* pdev, void* user_data,
                                           void** callback_data);

struct hal_uart_bus
{
    int (*open)(const struct hal_uart_config_t* cfg);
    int (*close)(const struct hal_uart_config_t* cfg);
    int (*read)(struct hal_uart_dev* pdev, uint8_t* data, size_t len);
    int (*write)(struct hal_uart_dev* pdev, const uint8_t* data, size_t len);
    int (*transmit)(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx, size_t rx_len,
                    size_t tx_len);
    int (*deinit)(const struct hal_uart_config_t* cfg);
    void* _impl;
};

const struct hal_uart_bus* hal_uart_bus_get(void);

int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms);
int hal_uart_xfer_end(struct hal_uart_dev* pdev);
int hal_uart_force_stop(void) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART HAL 层 — 硬件抽象接口 (ESP32-S3, vtable 模式)
 *
 * 与 STM32/CH32 hal_uart.h 结构对齐, 采用 vtable 模式。
 * 职责: 寄存器配置与传输执行, 不含锁/DMA 策略。
 *
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */
#ifndef HAL_UART_H
#define HAL_UART_H

/*
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */

#include "compiler_compat.h"
#include "hal_gpio.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*UART 枚举*/
/*===========================================================================================================================================================*/
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
/*===========================================================================================================================================================*/

                                                            /*UART 配置结构*/
/*===========================================================================================================================================================*/
struct hal_uart_config_t
{
    uint32_t         baud_rate;    // 波特率 (如 115200)
    hal_uart_data_bits_t data_bits;
    hal_uart_parity_t    parity;
    hal_uart_stop_bits_t stop_bits;
    hal_pin_t        tx_io    ;
    hal_pin_t        rx_io    ;
    int              uart_host;
};

struct hal_uart_dev
{
    struct hal_uart_config_t        cfg;
    int                             hw_open;
    int                             pool_idx;
    bool                            hw_inited;
    QueueHandle_t                   uart_queue;
    volatile uint8_t                status;
};

typedef enum 
{
    UART_STATE_UNINIT = 0,
    UART_STATE_READY,
    UART_STATE_BUSY,
    UART_STATE_ERROR
} uart_status_t;
/*===========================================================================================================================================================*/

                                                            /*回调类型与总线实体*/
/*===========================================================================================================================================================*/
/*无论什么情况如果有数据来了不允许在中断里面直接处理数据必须调用回调进中断下半部去解决问题*/
typedef void (*hal_uart_rx_irq_callback_t)(struct hal_uart_config_t* pdev, void* user_data,void**callback_data);

struct hal_uart_bus
{
    int (*open)(const struct hal_uart_config_t* cfg);
    int (*close)(const struct hal_uart_config_t* cfg);
    int (*read)(struct hal_uart_dev *pdev,uint8_t*data,size_t len);
    int (*write)(struct hal_uart_dev*pdev,const uint8_t*data,size_t len);
    int (*transmit)(struct hal_uart_dev*pdev,uint8_t*rx,uint8_t*tx,size_t rx_len,size_t tx_len);
    int (*deinit)(const struct hal_uart_config_t* cfg);
    void* _impl;
};

/*uart上下文结构体每个soc不同*/
/*===========================================================================================================================================================*/
int hal_uart_xfer_begin(struct hal_uart_dev*pdev, uint32_t timeout_ms);
int hal_uart_xfer_end(struct hal_uart_dev*pdev);

                                                            /*平台总线 vtable*/
/*===========================================================================================================================================================*/
const struct hal_uart_bus* hal_uart_bus_get(void);
/*===========================================================================================================================================================*/

                                                            /*强制停止 API*/
/*===========================================================================================================================================================*/
int  hal_uart_force_stopCOMPAT_IGNORE_RESULT(COMPAT_WARN_UNUSED_RESULT);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */

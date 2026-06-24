#ifndef  VFS_UART_H
#define  VFS_UART_H
#include "hal_uart.h"
#include "device.h"
#include "osal.h"
#include <stddef.h>
#include <stdint.h>
#if __cplusplus
extern "C"
{
#endif
#define UART_CMD_BASE               COMPAT_MAGIC(UART)
#define UART_CMD_TRANSFER           UART_CMD_BASE+0X01

struct vfs_uart_prive
{
    struct file_operations ops;
    struct hal_uart_dev    uart_dev;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
    struct hal_uart_bus    bus;
};

struct uart_transfer_arg
{
    const uint8_t* tx;
    uint8_t*       rx;
    size_t         tx_len;
    size_t         rx_len;
};
#if __cplusplus
}
#endif
#endif
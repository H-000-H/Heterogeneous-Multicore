/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART VFS 驱动 — 公共头
 */
#ifndef UART_VFS_H
#define UART_VFS_H

#include <stdint.h>
#include <stddef.h>
#include "device.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_CMD_BASE     COMPAT_MAGIC(UART)
#define UART_CMD_TRANSFER UART_CMD_BASE + 0x01

struct uart_transfer_arg {
    const uint8_t* tx;
    uint8_t*       rx;
    size_t         tx_len;
    size_t         rx_len;
};

int uart_vfs_probe(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int uart_vfs_remove(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* UART_VFS_H */

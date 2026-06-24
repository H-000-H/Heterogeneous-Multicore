/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART Bus Framework — 总线层接口
 */
#ifndef UART_BUS_H
#define UART_BUS_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;

int uart_bus_host_probe(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int uart_bus_host_remove(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

int uart_bus_device_register(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
void uart_bus_device_unregister(struct device* dev);

int uart_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int uart_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

int uart_bus_write(struct device* dev,
                    const uint8_t* data, size_t len,
                    uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
int uart_bus_read(struct device* dev,
                   uint8_t* data, size_t len,
                   uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* UART_BUS_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bus Framework — 总线客户端抽象
 *
 * 挂载在总线上的子设备（如 SPI Flash、UART 模组）作为 bus_client
 * 注册，由总线控制器统一调度。
 */
#ifndef BUS_CLIENT_H
#define BUS_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct bus_controller;
struct bus_client;

/* 客户端操作表 */
struct bus_client_ops {
    /* 被 attach 到控制器时的回调 */
    int (*attach)(struct bus_client* cli, struct bus_controller* ctlr);
    int (*detach)(struct bus_client* cli);
};

/* 注册/注销总线客户端 */
int  bus_client_register(struct device* dev,
                          const struct bus_client_ops* ops,
                          void* priv) COMPAT_WARN_UNUSED_RESULT;

void bus_client_unregister(struct device* dev);

/* 获取客户端所属控制器及私有数据 */
struct bus_controller* bus_client_controller(struct bus_client* cli);
void* bus_client_priv(struct bus_client* cli);
struct device* bus_client_device(struct bus_client* cli);

/* 通过设备节点查找客户端 */
struct bus_client* bus_client_get_by_device(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* BUS_CLIENT_H */

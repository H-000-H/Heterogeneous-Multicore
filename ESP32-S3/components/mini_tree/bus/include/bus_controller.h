/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bus Framework — 总线控制器（Controller / Host）抽象
 *
 * 一个物理总线控制器（如 SPI1、UART4）在 probe 阶段注册为一个
 * bus_controller，向上提供统一的总线操作接口 bus_controller_ops。
 */
#ifndef BUS_CONTROLLER_H
#define BUS_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct bus_controller;
struct bus_client;

/* 总线控制器操作表 */
struct bus_controller_ops {
    /* 执行一次总线传输（SPI 全双工 / I2C 收发 / UART 读写等） */
    int (*transfer)(struct bus_controller* ctlr,
                    struct bus_client*     cli,
                    const void*            tx,
                    void*                  rx,
                    size_t                 len,
                    uint32_t               timeout_ms);

    /* 总线级控制命令 */
    int (*ioctl)(struct bus_controller* ctlr, int cmd, void* arg, size_t arg_len);

    /* 资源申请/释放（如 DMA 通道、GPIO） */
    int (*alloc_resource)(struct bus_controller* ctlr, struct device* res_dev);
    int (*release_resource)(struct bus_controller* ctlr);
};

/* 总线类型标识 */
enum bus_controller_type {
    BUS_CONTROLLER_SPI = 0,
    BUS_CONTROLLER_UART,
    BUS_CONTROLLER_I2C,
    BUS_CONTROLLER_CAN,
    BUS_CONTROLLER_USB,
    BUS_CONTROLLER_I2S,
    BUS_CONTROLLER_PCIE,
    BUS_CONTROLLER_COUNT,
};

/* 注册/注销总线控制器 */
int  bus_controller_register(struct device* dev,
                              enum bus_controller_type type,
                              const struct bus_controller_ops* ops,
                              void* priv) COMPAT_WARN_UNUSED_RESULT;

void bus_controller_unregister(struct device* dev);

/* 通过设备节点查找控制器 */
struct bus_controller* bus_controller_get_by_device(struct device* dev);
struct bus_controller* bus_controller_get_by_parent(struct device* child_dev);

/* 访问控制器私有数据 */
void* bus_controller_priv(struct bus_controller* ctlr);
enum bus_controller_type bus_controller_type_get(struct bus_controller* ctlr);
struct device* bus_controller_device(struct bus_controller* ctlr);

#ifdef __cplusplus
}
#endif

#endif /* BUS_CONTROLLER_H */

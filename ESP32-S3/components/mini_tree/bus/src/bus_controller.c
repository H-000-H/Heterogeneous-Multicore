/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bus Framework — 总线控制器核心实现
 */
#include "bus_controller.h"
#include "device.h"
#include "board_devtable.h"
#include "VFS.h"
#include "compiler_compat.h"

#include <string.h>

#ifndef BUS_CONTROLLER_MAX
#define BUS_CONTROLLER_MAX DEV_ID_COUNT
#endif

struct bus_controller {
    struct device*                 dev;
    enum bus_controller_type       type;
    const struct bus_controller_ops* ops;
    void*                          priv;
    uint8_t                        in_use;
};

static struct bus_controller s_controllers[BUS_CONTROLLER_MAX];

static int device_to_id(const struct device* dev)
{
    if (!dev || !dev->node)
        return -1;
    return (int)board_dev_find(device_get_name(dev));
}

int bus_controller_register(struct device* dev,
                             enum bus_controller_type type,
                             const struct bus_controller_ops* ops,
                             void* priv)
{
    int id;

    if (!dev || !ops || type >= BUS_CONTROLLER_COUNT)
        return VFS_ERR_INVAL;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CONTROLLER_MAX)
        return VFS_ERR_INVAL;

    if (s_controllers[id].in_use)
        return VFS_ERR_BUSY;

    s_controllers[id].dev  = dev;
    s_controllers[id].type = type;
    s_controllers[id].ops  = ops;
    s_controllers[id].priv = priv;
    s_controllers[id].in_use = 1;

    return VFS_OK;
}

void bus_controller_unregister(struct device* dev)
{
    int id;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CONTROLLER_MAX)
        return;

    memset(&s_controllers[id], 0, sizeof(s_controllers[id]));
}

struct bus_controller* bus_controller_get_by_device(struct device* dev)
{
    int id;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CONTROLLER_MAX || !s_controllers[id].in_use)
        return NULL;

    return &s_controllers[id];
}

struct bus_controller* bus_controller_get_by_parent(struct device* child_dev)
{
    struct device* parent;

    if (!child_dev)
        return NULL;

    parent = device_get_parent(child_dev);
    if (!parent)
        return NULL;

    return bus_controller_get_by_device(parent);
}

void* bus_controller_priv(struct bus_controller* ctlr)
{
    return ctlr ? ctlr->priv : NULL;
}

enum bus_controller_type bus_controller_type_get(struct bus_controller* ctlr)
{
    return ctlr ? ctlr->type : BUS_CONTROLLER_COUNT;
}

struct device* bus_controller_device(struct bus_controller* ctlr)
{
    return ctlr ? ctlr->dev : NULL;
}

int bus_controller_transfer(struct bus_controller* ctlr,
                             struct bus_client* cli,
                             const void* tx, void* rx,
                             size_t len, uint32_t timeout_ms)
{
    if (!ctlr || !ctlr->ops || !ctlr->ops->transfer)
        return VFS_ERR_INVAL;

    return ctlr->ops->transfer(ctlr, cli, tx, rx, len, timeout_ms);
}

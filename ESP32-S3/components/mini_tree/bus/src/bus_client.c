/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bus Framework — 总线客户端核心实现
 */
#include "bus_client.h"
#include "bus_controller.h"
#include "device.h"
#include "board_devtable.h"
#include "VFS.h"
#include "compiler_compat.h"

#include <string.h>

#ifndef BUS_CLIENT_MAX
#define BUS_CLIENT_MAX DEV_ID_COUNT
#endif

struct bus_client {
    struct device*                dev;
    struct bus_controller*        ctlr;
    const struct bus_client_ops*  ops;
    void*                         priv;
    uint8_t                       in_use;
};

static struct bus_client s_clients[BUS_CLIENT_MAX];

static int device_to_id(const struct device* dev)
{
    if (!dev || !dev->node)
        return -1;
    return (int)board_dev_find(device_get_name(dev));
}

int bus_client_register(struct device* dev,
                         const struct bus_client_ops* ops,
                         void* priv)
{
    int id;

    if (!dev || !ops)
        return VFS_ERR_INVAL;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CLIENT_MAX)
        return VFS_ERR_INVAL;

    if (s_clients[id].in_use)
        return VFS_ERR_BUSY;

    s_clients[id].dev  = dev;
    s_clients[id].ops  = ops;
    s_clients[id].priv = priv;
    s_clients[id].in_use = 1;

    return VFS_OK;
}

void bus_client_unregister(struct device* dev)
{
    int id;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CLIENT_MAX)
        return;

    memset(&s_clients[id], 0, sizeof(s_clients[id]));
}

struct bus_client* bus_client_get_by_device(struct device* dev)
{
    int id;

    id = device_to_id(dev);
    if (id < 0 || id >= BUS_CLIENT_MAX || !s_clients[id].in_use)
        return NULL;

    return &s_clients[id];
}

struct bus_controller* bus_client_controller(struct bus_client* cli)
{
    return cli ? cli->ctlr : NULL;
}

void* bus_client_priv(struct bus_client* cli)
{
    return cli ? cli->priv : NULL;
}

struct device* bus_client_device(struct bus_client* cli)
{
    return cli ? cli->dev : NULL;
}

int bus_client_attach(struct bus_client* cli, struct bus_controller* ctlr)
{
    int ret = VFS_OK;

    if (!cli || !ctlr)
        return VFS_ERR_INVAL;

    if (cli->ops && cli->ops->attach)
        ret = cli->ops->attach(cli, ctlr);

    if (ret == VFS_OK)
        cli->ctlr = ctlr;

    return ret;
}

#include "bus.h"
#include "device.h"

#include <stddef.h>

static struct bus_controller s_controllers[DEV_ID_COUNT];
static struct bus_client     s_clients[DEV_ID_COUNT];
static uint8_t               s_controller_used[DEV_ID_COUNT];
static uint8_t               s_client_used[DEV_ID_COUNT];

static device_id_t device_to_id(const struct device* dev)
{
    if (!dev || !dev->node)
        return (device_id_t)-1;
    return board_dev_find(device_get_name(dev));
}

int bus_controller_bind(struct device* dev, const struct bus_type* type, void* hw_priv)
{
    device_id_t id;

    if (!dev || !type)
        return -1;

    id = device_to_id(dev);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return -1;

    s_controllers[id].dev     = dev;
    s_controllers[id].type    = type;
    s_controllers[id].hw_priv = hw_priv;
    s_controller_used[id]     = 1;
    return 0;
}

struct bus_controller* bus_controller_of(const struct device* dev)
{
    struct device* parent;
    device_id_t    id;

    parent = device_get_parent(dev);
    if (!parent)
        return NULL;

    id = device_to_id(parent);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT || !s_controller_used[id])
        return NULL;

    return &s_controllers[id];
}

int bus_client_bind(struct device* child, struct device* controller, void* client_priv)
{
    device_id_t id;

    if (!child || !controller)
        return -1;

    id = device_to_id(child);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return -1;

    s_clients[id].dev         = child;
    s_clients[id].controller  = controller;
    s_clients[id].client_priv = client_priv;
    s_client_used[id]         = 1;
    return 0;
}

void* bus_client_priv(const struct device* child)
{
    device_id_t id;

    if (!child)
        return NULL;

    id = device_to_id(child);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT || !s_client_used[id])
        return NULL;

    return s_clients[id].client_priv;
}

void bus_controller_unbind(struct device* dev)
{
    device_id_t id;

    if (!dev)
        return;

    id = device_to_id(dev);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return;

    s_controller_used[id] = 0;
    s_controllers[id].dev = NULL;
    s_controllers[id].type = NULL;
    s_controllers[id].hw_priv = NULL;
}

void bus_client_unbind(const struct device* child)
{
    device_id_t id;

    if (!child)
        return;

    id = device_to_id(child);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return;

    s_client_used[id] = 0;
    s_clients[id].dev = NULL;
    s_clients[id].controller = NULL;
    s_clients[id].client_priv = NULL;
}

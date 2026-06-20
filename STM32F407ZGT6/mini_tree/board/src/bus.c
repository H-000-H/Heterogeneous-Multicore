#include "bus.h"
#include "device.h"
#include "VFS.h"

#include <stddef.h>
#include "compiler_compat_poison.h"

// 控制器数组：每个控制器对应一个设备
static struct bus_controller s_controllers[DEV_ID_COUNT];
// 客户端数组：每个子设备对应一个客户端
static struct bus_client     s_clients[DEV_ID_COUNT];
// 两个位图表：标记哪些槽位已用
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
        return VFS_ERR_INVAL;

    id = device_to_id(dev);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    s_controllers[id].dev     = dev;
    s_controllers[id].type    = type;
    s_controllers[id].hw_priv = hw_priv;
    s_controller_used[id]     = 1;
    return VFS_OK;
}

/*子设备反推查找控制器*/
int bus_controller_of(const struct device* dev, struct bus_controller** out)
{
    struct device* parent;
    device_id_t    id;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (!dev)
        return VFS_ERR_INVAL;

    parent = device_get_parent(dev);
    if (!parent)
        return VFS_ERR_NODEV;

    id = device_to_id(parent);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT || !s_controller_used[id])
        return VFS_ERR_NODEV;

    *out = &s_controllers[id];
    return VFS_OK;
}

/* 绑定子设备到控制器*/
int bus_client_bind(struct device* child, struct device* controller, void* client_priv)
{
    device_id_t id;

    if (!child || !controller)
        return VFS_ERR_INVAL;

    id = device_to_id(child);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    s_clients[id].dev         = child;
    s_clients[id].controller  = controller;
    s_clients[id].client_priv = client_priv;
    s_client_used[id]         = 1;
    return VFS_OK;
}

/*拿客户端私有数据*/
int bus_client_priv(const struct device* child, void** out)
{
    device_id_t id;

    if (!out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (!child)
        return VFS_ERR_INVAL;

    id = device_to_id(child);
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT || !s_client_used[id])
        return VFS_ERR_NODEV;

    *out = s_clients[id].client_priv;
    return VFS_OK;
}

/*解绑控制器*/
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

/*解绑客户端*/
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

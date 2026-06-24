#ifndef VFS_GPIO_H
#define VFS_GPIO_H

#include "VFS.h"
#include "device.h"
#include "hal_gpio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_CMD_BASE         COMPAT_MAGIC(GPIO)
#define GPIO_CMD_TOGGLE       GPIO_CMD_BASE+0x01
#define GPIO_CMD_SET_LEVEL    GPIO_CMD_BASE+0x02
#define GPIO_CMD_GET_LEVEL    GPIO_CMD_BASE+0x03

struct vfs_gpio_arg
{
    int level;
    hal_pin_t pin;
};

static int inline vfs_gpio_set_level(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(vfs_arg->pin, vfs_arg->level);
}

static int inline vfs_gpio_get_level(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_get_level(vfs_arg->pin, &vfs_arg->level);
}

static int inline vfs_gpio_toggle(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))
        return PTR_ERR(vfs_arg);
    if (!hal_pin_is_valid(vfs_arg->pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_toggle(vfs_arg->pin);
}

#ifdef __cplusplus
}
#endif

#endif /* VFS_GPIO_H */

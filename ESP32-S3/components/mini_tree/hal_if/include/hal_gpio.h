#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <driver/gpio.h>
#include "compiler_compat.h"
#include "VFS.h"
#include "hal_pin.h"
#include "hal_pin_map.h"

#ifdef __cplusplus
extern "C" 
{
#endif

#define HAL_GPIO_HIGH_LEVEL 1
#define HAL_GPIO_LOW_LEVEL  0

/* 数值与 board/dt-bindings/gpio/gpio-ctl.h 一致 */
typedef enum
{
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_INPUT_OUTPUT,
    HAL_GPIO_MODE_OPEN_DRAIN,
} hal_gpio_mode_t;

typedef enum
{
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP,
    HAL_GPIO_PULL_DOWN,
} hal_gpio_pull_t;

/*兼容如同GPIOA GPIOX这种的port如stm32 gd32这种*/
#ifdef COMPAT_GPIO_PORT
typedef enum
{
    GPIO_PORT = 0, /* 这里是由使用哪个芯片自己替换*/
} hal_gpio_port_t;
#endif

/* 从 hal_pin_t / DTS 打包值解出 port 与 pin  */
static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_unpack_pin(uint16_t *gpio_port,
                                                                uint16_t *gpio_pin,
                                                                uint32_t dts_pin)
{
    return hal_pin_unpack(gpio_port, gpio_pin, (hal_pin_t)dts_pin);
}

struct hal_gpio_config
{
    int port;
    int pin;
    int mode;
    int pull;
};

static inline hal_pin_t hal_gpio_config_pin(const struct hal_gpio_config *cfg)
{
    int port = (cfg && cfg->port >= 0) ? cfg->port : 0;
    int pin  = cfg ? cfg->pin : -1;
    return hal_pin_from_parts(port, pin);
}

int hal_gpio_set_level(hal_pin_t pin, int level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_get_level(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_init(const struct hal_gpio_config *cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_deinit(const struct hal_gpio_config *cfg) COMPAT_WARN_UNUSED_RESULT;

/* 快速路径函数(直呼 HAL 实现, 绕过 ioctl) */
static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_set_level(hal_pin_t pin, int level)
{
    gpio_set_level((gpio_num_t)hal_pin_map_hw_gpio(pin), level);
    return VFS_OK;
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_get_level(hal_pin_t pin, int *level_out)
{
    if (!level_out)
        return VFS_ERR_INVAL;

    *level_out = gpio_get_level((gpio_num_t)hal_pin_map_hw_gpio(pin));
    return VFS_OK;
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_toggle(hal_pin_t pin)
{
    int current_level = 0;
    hal_gpio_fast_get_level(pin, &current_level);
    return hal_gpio_fast_set_level(pin, !current_level);
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */

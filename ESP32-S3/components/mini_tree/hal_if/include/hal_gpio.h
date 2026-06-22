#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <driver/gpio.h>
#include "compiler_compat.h"
#include "VFS.h"
#ifdef __cplusplus
extern "C" 
{
#endif

/* ── 跨平台引脚抽象 ──
 *
 * hal_pin_t 为 32-bit 复合引脚标识:
 *   [31:16] 端口号 (Port), 由平台实现定义
 *   [15: 0] 引脚号 (Pin),  0 开始
 *
 * 使用方式:
 *   hal_pin_t pin = HAL_MAKE_PIN(port, pin_num);
 *   本人对gpio的vfs是包装加了生命周期等资源管理相关你要用vfs多几个函数跳转但是正常用gpio快速通道
 *   别去用vfs。直接调用vfs-gpiofast通道这只是vfs的一个函数包装。别在搞高频用虚拟文件把自己玩死了
 */
typedef uint32_t hal_pin_t;
#define HAL_GPIO_HIGH_LEVEL 1
#define HAL_GPIO_LOW_LEVEL  0
#define HAL_PIN_PORT_SHIFT 16
#define HAL_PIN_NUM_MASK   0xFFFFU
#define HAL_MAKE_PIN(port, num)  (((hal_pin_t)(port) << HAL_PIN_PORT_SHIFT) | ((hal_pin_t)(num) & HAL_PIN_NUM_MASK))
#define HAL_PIN_PORT(pin)        ((int)((pin) >> HAL_PIN_PORT_SHIFT))
#define HAL_PIN_NUM(pin)         ((int)((pin) & HAL_PIN_NUM_MASK))

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
static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_unpack_pin(uint16_t *gpio_port,uint16_t *gpio_pin,uint32_t dts_pin)
{
    if (gpio_port == NULL || gpio_pin == NULL)
    {
        return VFS_ERR_INVAL;
    }
    *gpio_port = (uint16_t)HAL_PIN_PORT(dts_pin);
    *gpio_pin  = (uint16_t)HAL_PIN_NUM(dts_pin);
    return VFS_OK;
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
    return HAL_MAKE_PIN(port, pin);
}

int hal_gpio_set_level(hal_pin_t pin, int level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_get_level(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_init(const struct hal_gpio_config *cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_deinit(const struct hal_gpio_config *cfg) COMPAT_WARN_UNUSED_RESULT;

/* 快速路径函数(直呼 HAL 实现, 绕过 ioctl) */
static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_set_level(hal_pin_t pin, int level)
{
    gpio_set_level((gpio_num_t)HAL_PIN_NUM(pin), level);
    return VFS_OK;
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_get_level(hal_pin_t pin, int *level_out)
{
    if (!level_out)
        return VFS_ERR_INVAL;

    *level_out = gpio_get_level((gpio_num_t)HAL_PIN_NUM(pin));
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

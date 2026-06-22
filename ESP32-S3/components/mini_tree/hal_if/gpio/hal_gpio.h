#ifndef HAL_GPIO_H
#define HAL_GPIO_H
#include <stdint.h>
#include <driver/gpio.h>
#include "compiler_compat.h"
#include "VFS.h"
#include "hal_pin.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HAL_GPIO_HIGH_LEVEL 1
#define HAL_GPIO_LOW_LEVEL  0

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

struct hal_gpio_mode_cfg
{
    int mode;
    int pull;
};

/* 裸调 fast path: 直呼 SoC GPIO, 无额外校验; VFS 在 dev_lc 持锁后使用 */
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
    if (hal_gpio_fast_get_level(pin, &current_level) != VFS_OK)
        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(pin, !current_level);
}

/* 安全包装: 校验 pin 后转调 fast (供非 VFS / 无锁上下文) */
int hal_gpio_set_level(hal_pin_t pin, int level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_get_level(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_read_level(hal_pin_t pin, int *level_out) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_toggle(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_init(hal_pin_t pin, const struct hal_gpio_mode_cfg *cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_deinit(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_write_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_read_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t *level_out) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */

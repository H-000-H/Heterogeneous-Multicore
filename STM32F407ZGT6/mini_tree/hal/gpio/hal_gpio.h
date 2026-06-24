#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include "compiler_compat.h"
#include "VFS.h"

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*虚拟引脚实体与宏*/
/*===========================================================================================================================================================*/
#define HAL_GPIO_PORT_DEFAULT 0
#define HAL_PIN_INVALID_NUM   UINT16_MAX

typedef struct hal_pin
{
    uint16_t v[2];   /* v[0]=port_idx, v[1]=pin_idx */
} hal_pin_t;

#define HAL_PIN_PORT(p) ((int)(p).v[0])
#define HAL_PIN_NUM(p)  ((int)(p).v[1])
#define HAL_MAKE_PIN(port, num) hal_pin_make((int)(port), (uint16_t)(num))
/*===========================================================================================================================================================*/

                                                            /*inline 构造与校验*/
/*===========================================================================================================================================================*/
static inline hal_pin_t hal_pin_make(int port, uint16_t pin)
{
    hal_pin_t p = { .v = { (uint16_t)port, pin } };
    return p;
}

static inline hal_pin_t hal_pin_invalid(void)
{
    hal_pin_t p = { .v = { HAL_GPIO_PORT_DEFAULT, HAL_PIN_INVALID_NUM } };
    return p;
}

static inline int hal_pin_is_valid(hal_pin_t p)
{
    return p.v[1] != HAL_PIN_INVALID_NUM;
}

static inline int hal_pin_equal(hal_pin_t a, hal_pin_t b)
{
    return a.v[0] == b.v[0] && a.v[1] == b.v[1];
}
/*===========================================================================================================================================================*/

                                                            /*DTS/硬件映射 API*/
/*===========================================================================================================================================================*/
int hal_gpio_dts_resolve(uint32_t dts_port, uint32_t dts_pin, int *hw_gpio_out) COMPAT_WARN_UNUSED_RESULT;
int hal_pin_map_hw_gpio(hal_pin_t pin);
/*===========================================================================================================================================================*/

                                                            /*GPIO 模式枚举*/
/*===========================================================================================================================================================*/
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
/*===========================================================================================================================================================*/

                                                            /*模式配置结构*/
/*===========================================================================================================================================================*/
struct hal_gpio_mode_cfg
{
    int mode;
    int pull;
};
/*===========================================================================================================================================================*/

#include "stm32f4xx_hal.h"

#define HAL_GPIO_PORT_COUNT 7
#define HAL_GPIO_PIN_COUNT  16

_Static_assert(HAL_GPIO_PORT_COUNT > 0, "hal gpio: port count");
_Static_assert(HAL_GPIO_PIN_COUNT > 0, "hal gpio: pin count");
_Static_assert(HAL_PIN_INVALID_NUM > HAL_GPIO_PIN_COUNT, "hal gpio: invalid pin sentinel");

extern GPIO_TypeDef *const g_stm32_port_lut[HAL_GPIO_PORT_COUNT];
extern const uint16_t      g_stm32_pin_lut[HAL_GPIO_PIN_COUNT];

                                                            /*fast path inline*/
/*===========================================================================================================================================================*/
/* 裸调 fast path: 直呼 SoC GPIO, 无额外校验; VFS 在 dev_lc 持锁后使用 */
static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_set_level(hal_pin_t pin, int level)
{
    uint32_t port_idx = (uint32_t)HAL_PIN_PORT(pin);
    uint32_t pin_idx  = (uint32_t)HAL_PIN_NUM(pin);

    HAL_GPIO_WritePin(g_stm32_port_lut[port_idx], g_stm32_pin_lut[pin_idx],level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return VFS_OK;
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_get_level(hal_pin_t pin, int *level_out)
{
    uint32_t port_idx = (uint32_t)HAL_PIN_PORT(pin);
    uint32_t pin_idx  = (uint32_t)HAL_PIN_NUM(pin);

    if (!level_out)
        return VFS_ERR_INVAL;
    *level_out = (HAL_GPIO_ReadPin(g_stm32_port_lut[port_idx], g_stm32_pin_lut[pin_idx]) ==GPIO_PIN_SET)? 1: 0;
    return VFS_OK;
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_gpio_fast_toggle(hal_pin_t pin)
{
    uint32_t port_idx = (uint32_t)HAL_PIN_PORT(pin);
    uint32_t pin_idx  = (uint32_t)HAL_PIN_NUM(pin);

    HAL_GPIO_TogglePin(g_stm32_port_lut[port_idx], g_stm32_pin_lut[pin_idx]);
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                            /*安全包装 API*/
/*===========================================================================================================================================================*/
int hal_gpio_set_level(hal_pin_t pin, int level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_get_level(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_read_level(hal_pin_t pin, int *level_out) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_toggle(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_init(hal_pin_t pin, const struct hal_gpio_mode_cfg *cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_deinit(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_write_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t level) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_read_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t *level_out) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */

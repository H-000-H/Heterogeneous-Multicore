#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "stm32f4xx_hal.h"

/* =========================================================================
 * 物理端口查找表 (解包 v[0])
 * ========================================================================= */
GPIO_TypeDef *const g_stm32_port_lut[HAL_GPIO_PORT_COUNT] = {
    [0] = GPIOA,
    [1] = GPIOB,
    [2] = GPIOC,
    [3] = GPIOD,
    [4] = GPIOE,
    [5] = GPIOF,
    [6] = GPIOG,
};
_Static_assert(sizeof(g_stm32_port_lut) / sizeof(g_stm32_port_lut[0]) == HAL_GPIO_PORT_COUNT,"hal gpio: port lut size");

/* =========================================================================
 * 物理引脚掩码查找表 (解包 v[1])
 * ========================================================================= */
const uint16_t g_stm32_pin_lut[HAL_GPIO_PIN_COUNT] = {
    [0]  = GPIO_PIN_0,  [1]  = GPIO_PIN_1,  [2]  = GPIO_PIN_2,  [3]  = GPIO_PIN_3,
    [4]  = GPIO_PIN_4,  [5]  = GPIO_PIN_5,  [6]  = GPIO_PIN_6,  [7]  = GPIO_PIN_7,
    [8]  = GPIO_PIN_8,  [9]  = GPIO_PIN_9,  [10] = GPIO_PIN_10, [11] = GPIO_PIN_11,
    [12] = GPIO_PIN_12, [13] = GPIO_PIN_13, [14] = GPIO_PIN_14, [15] = GPIO_PIN_15,
};
_Static_assert(sizeof(g_stm32_pin_lut) / sizeof(g_stm32_pin_lut[0]) == HAL_GPIO_PIN_COUNT,"hal gpio: pin lut size");

/* =========================================================================
 * 端口时钟掩码查找表
 * ========================================================================= */
static const uint32_t g_stm32_clock_lut[HAL_GPIO_PORT_COUNT] = {
    [0] = RCC_AHB1ENR_GPIOAEN,
    [1] = RCC_AHB1ENR_GPIOBEN,
    [2] = RCC_AHB1ENR_GPIOCEN,
    [3] = RCC_AHB1ENR_GPIODEN,
    [4] = RCC_AHB1ENR_GPIOEEN,
    [5] = RCC_AHB1ENR_GPIOFEN,
    [6] = RCC_AHB1ENR_GPIOGEN,
};
_Static_assert(sizeof(g_stm32_clock_lut) / sizeof(g_stm32_clock_lut[0]) == HAL_GPIO_PORT_COUNT,"hal gpio: clock lut size");

static inline int hal_gpio_dts_bounds_ok(uint32_t dts_port, uint32_t dts_pin)
{
    return (dts_port < HAL_GPIO_PORT_COUNT) && (dts_pin < HAL_GPIO_PIN_COUNT);
}

int hal_gpio_dts_resolve(uint32_t dts_port, uint32_t dts_pin, int *hw_gpio_out)
{
    if (!hw_gpio_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

#if COMPAT_CFG_ENABLED(HAL_PIN_MAP_LINEAR)
    *hw_gpio_out = (int)((dts_port << 4) | dts_pin);
#else
    *hw_gpio_out = (int)dts_pin;
#endif
    return VFS_OK;
}

static inline int hal_gpio_pin_usable(hal_pin_t pin)
{
    return hal_pin_is_valid(pin) && 
           hal_gpio_dts_bounds_ok((uint32_t)HAL_PIN_PORT(pin), (uint32_t)HAL_PIN_NUM(pin));
}

static void stm32_gpio_enable_clock(int port_idx)
{
    if (port_idx >= 0 && (size_t)port_idx < HAL_GPIO_PORT_COUNT && g_stm32_clock_lut[port_idx])
        SET_BIT(RCC->AHB1ENR, g_stm32_clock_lut[port_idx]);
}

static void stm32_gpio_fill_init(GPIO_InitTypeDef *init, int mode, int pull)
{
    init->Pull  = GPIO_NOPULL;
    init->Speed = GPIO_SPEED_FREQ_HIGH;

    switch ((hal_gpio_mode_t)mode)
    {
    case HAL_GPIO_MODE_OUTPUT:
        init->Mode = GPIO_MODE_OUTPUT_PP;
        break;
    case HAL_GPIO_MODE_OPEN_DRAIN:
        init->Mode = GPIO_MODE_OUTPUT_OD;
        break;
    case HAL_GPIO_MODE_INPUT_OUTPUT:
        init->Mode = GPIO_MODE_OUTPUT_PP;
        break;
    case HAL_GPIO_MODE_INPUT:
    default:
        init->Mode = GPIO_MODE_INPUT;
        break;
    }

    if (pull == HAL_GPIO_PULL_UP)
        init->Pull = GPIO_PULLUP;
    else if (pull == HAL_GPIO_PULL_DOWN)
        init->Pull = GPIO_PULLDOWN;
}

int hal_gpio_write_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t level)
{
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    HAL_GPIO_WritePin(g_stm32_port_lut[dts_port], g_stm32_pin_lut[dts_pin],level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return VFS_OK;
}

int hal_gpio_read_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t *level_out)
{
    if (!level_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    *level_out = (uint8_t)(HAL_GPIO_ReadPin(g_stm32_port_lut[dts_port], g_stm32_pin_lut[dts_pin]) ==GPIO_PIN_SET);
    return VFS_OK;
}

/* =========================================================================
 * 标准业务接口
 * ========================================================================= */
int hal_gpio_set_level(hal_pin_t pin, int level)
{
    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(pin, level);
}

int hal_gpio_read_level(hal_pin_t pin, int *level_out)
{
    if (!level_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_get_level(pin, level_out);
}

/**
 * @brief 获取引脚电平
 * @note  注意：若引脚非法或未初始化，此函数默认返回 0。
 */
int hal_gpio_get_level(hal_pin_t pin)
{
    int level = 0;

    if (hal_gpio_read_level(pin, &level) != VFS_OK)
        return 0;
    return level;
}

int hal_gpio_toggle(hal_pin_t pin)
{
    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;
    return hal_gpio_fast_toggle(pin);
}

int hal_gpio_init(hal_pin_t pin, const struct hal_gpio_mode_cfg *cfg)
{
    int              port_idx = HAL_PIN_PORT(pin);
    int              pin_idx  = HAL_PIN_NUM(pin);
    GPIO_InitTypeDef init     = {0};

    if (!cfg || !hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;

    stm32_gpio_enable_clock(port_idx);
    init.Pin = g_stm32_pin_lut[pin_idx];
    stm32_gpio_fill_init(&init, cfg->mode, cfg->pull);
    HAL_GPIO_Init(g_stm32_port_lut[port_idx], &init);
    return VFS_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    int port_idx = HAL_PIN_PORT(pin);
    int pin_idx  = HAL_PIN_NUM(pin);

    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;

    HAL_GPIO_DeInit(g_stm32_port_lut[port_idx], g_stm32_pin_lut[pin_idx]);
    return VFS_OK;
}

int hal_pin_map_hw_gpio(hal_pin_t pin)
{
    int hw;

    if (!hal_pin_is_valid(pin))
        return -1;
    if (hal_gpio_dts_resolve((uint32_t)HAL_PIN_PORT(pin), (uint32_t)HAL_PIN_NUM(pin), &hw) !=
        VFS_OK)
        return -1;
    return hw;
}
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GPIO HAL — CH32V307 实现
 *
 * 适配 ESP32 hal_gpio.h 结构体与 API, 保留 CH32 寄存器操作。
 * 平台私有: 端口/引脚查找表、DTS bounds 校验、raw 读写。
 *
 * 寄存器约定:
 *   - fast_get_level 读 INDR (输入数据寄存器, 反映真实引脚电平)
 *   - fast_set_level 写 BSHR/BCR (原子置位/复位)
 *   - fast_toggle   写 OUTDR ^= mask (非原子, VFS 持锁下安全)
 */
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "ch32v30x.h"

/* =========================================================================
 * 1. 物理端口查找表 (解包 v[0])
 * ========================================================================= */
GPIO_TypeDef *const g_ch32_port_lut[HAL_GPIO_PORT_COUNT] = {
    [0] = GPIOA,
    [1] = GPIOB,
    [2] = GPIOC,
    [3] = GPIOD,
    [4] = GPIOE,
};
_Static_assert(sizeof(g_ch32_port_lut) / sizeof(g_ch32_port_lut[0]) == HAL_GPIO_PORT_COUNT,
              "hal gpio: port lut size");

/* =========================================================================
 * 2. 物理引脚掩码查找表 (解包 v[1])
 * ========================================================================= */
const uint16_t g_ch32_pin_lut[HAL_GPIO_PIN_COUNT] = {
    [0]  = GPIO_Pin_0,  [1]  = GPIO_Pin_1,  [2]  = GPIO_Pin_2,  [3]  = GPIO_Pin_3,
    [4]  = GPIO_Pin_4,  [5]  = GPIO_Pin_5,  [6]  = GPIO_Pin_6,  [7]  = GPIO_Pin_7,
    [8]  = GPIO_Pin_8,  [9]  = GPIO_Pin_9,  [10] = GPIO_Pin_10, [11] = GPIO_Pin_11,
    [12] = GPIO_Pin_12, [13] = GPIO_Pin_13, [14] = GPIO_Pin_14, [15] = GPIO_Pin_15,
};
_Static_assert(sizeof(g_ch32_pin_lut) / sizeof(g_ch32_pin_lut[0]) == HAL_GPIO_PIN_COUNT,
              "hal gpio: pin lut size");

/* =========================================================================
 * 3. 端口时钟掩码查找表
 * ========================================================================= */
static const uint32_t g_ch32_clock_lut[HAL_GPIO_PORT_COUNT] = {
    [0] = RCC_APB2Periph_GPIOA,
    [1] = RCC_APB2Periph_GPIOB,
    [2] = RCC_APB2Periph_GPIOC,
    [3] = RCC_APB2Periph_GPIOD,
    [4] = RCC_APB2Periph_GPIOE,
};
_Static_assert(sizeof(g_ch32_clock_lut) / sizeof(g_ch32_clock_lut[0]) == HAL_GPIO_PORT_COUNT,
              "hal gpio: clock lut size");

static int hal_gpio_dts_bounds_ok(uint32_t dts_port, uint32_t dts_pin)
{
    return dts_port < HAL_GPIO_PORT_COUNT && dts_pin < HAL_GPIO_PIN_COUNT;
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

static int hal_gpio_pin_usable(hal_pin_t pin)
{
    return hal_pin_is_valid(pin) &&
           hal_gpio_dts_bounds_ok((uint32_t)HAL_PIN_PORT(pin), (uint32_t)HAL_PIN_NUM(pin));
}

static void ch32_gpio_enable_clock(int port_idx)
{
    if (port_idx >= 0 && (size_t)port_idx < HAL_GPIO_PORT_COUNT && g_ch32_clock_lut[port_idx])
        RCC_APB2PeriphClockCmd(g_ch32_clock_lut[port_idx], ENABLE);
}

static GPIOMode_TypeDef ch32_gpio_map_mode(int mode, int pull)
{
    switch ((hal_gpio_mode_t)mode)
    {
    case HAL_GPIO_MODE_OUTPUT:
        return GPIO_Mode_Out_PP;
    case HAL_GPIO_MODE_OPEN_DRAIN:
        return GPIO_Mode_Out_OD;
    case HAL_GPIO_MODE_INPUT_OUTPUT:
        return GPIO_Mode_Out_PP;
    case HAL_GPIO_MODE_INPUT:
    default:
        if (pull == HAL_GPIO_PULL_UP)
            return GPIO_Mode_IPU;
        if (pull == HAL_GPIO_PULL_DOWN)
            return GPIO_Mode_IPD;
        return GPIO_Mode_IN_FLOATING;
    }
}

int hal_gpio_write_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t level)
{
    GPIO_TypeDef *port;

    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    port = g_ch32_port_lut[dts_port];
    if (level)
        port->BSHR = g_ch32_pin_lut[dts_pin];
    else
        port->BCR = g_ch32_pin_lut[dts_pin];
    return VFS_OK;
}

int hal_gpio_read_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t *level_out)
{
    GPIO_TypeDef *port;

    if (!level_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    port       = g_ch32_port_lut[dts_port];
    *level_out = (uint8_t)((port->INDR & g_ch32_pin_lut[dts_pin]) ? 1 : 0);
    return VFS_OK;
}

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

    ch32_gpio_enable_clock(port_idx);
    init.GPIO_Pin   = g_ch32_pin_lut[pin_idx];
    init.GPIO_Speed = GPIO_Speed_50MHz;
    init.GPIO_Mode  = ch32_gpio_map_mode(cfg->mode, cfg->pull);
    GPIO_Init(g_ch32_port_lut[port_idx], &init);
    return VFS_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    int              port_idx = HAL_PIN_PORT(pin);
    int              pin_idx  = HAL_PIN_NUM(pin);
    GPIO_InitTypeDef init     = {0};

    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;

    init.GPIO_Pin  = g_ch32_pin_lut[pin_idx];
    init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(g_ch32_port_lut[port_idx], &init);
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

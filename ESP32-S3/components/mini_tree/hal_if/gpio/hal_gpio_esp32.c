#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

/* =========================================================================
 * DTS 数字 → SoC gpio_num_t (数组下标 = DTS gpio-pin)
 * ESP32 无 Port 表: 单逻辑端口 DTS_GPIOZERO=0, pin 即 SoC 编号.
 * ========================================================================= */
static const gpio_num_t g_pin_lut[] =
{
    [0]  = 0,  [1]  = 1,  [2]  = 2,  [3]  = 3,  [4]  = 4,
    [5]  = 5,  [6]  = 6,  [7]  = 7,  [8]  = 8,  [9]  = 9,
    [10] = 10, [11] = 11, [12] = 12, [13] = 13, [14] = 14,
    [15] = 15, [16] = 16, [17] = 17, [18] = 18, [19] = 19,
    [20] = 20, [21] = 21, [22] = 22, [23] = 23, [24] = 24,
    [25] = 25, [26] = 26, [27] = 27, [28] = 28, [29] = 29,
    [30] = 30, [31] = 31, [32] = 32, [33] = 33, [34] = 34,
    [35] = 35, [36] = 36, [37] = 37, [38] = 38, [39] = 39,
    [40] = 40, [41] = 41, [42] = 42, [43] = 43, [44] = 44,
    [45] = 45, [46] = 46, [47] = 47, [48] = 48,
};
#define PIN_LUT_SIZE (sizeof(g_pin_lut) / sizeof(g_pin_lut[0]))

static int hal_gpio_dts_bounds_ok(uint32_t dts_port, uint32_t dts_pin)
{
    (void)dts_port; /* ESP32 单端口, 不查 Port 表 */
    return dts_pin < PIN_LUT_SIZE;
}

int hal_gpio_dts_resolve(uint32_t dts_port, uint32_t dts_pin, int *hw_gpio_out)
{
    if (!hw_gpio_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    *hw_gpio_out = (int)g_pin_lut[dts_pin];
    return VFS_OK;
}

int hal_gpio_write_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t level)
{
    gpio_num_t hw;

    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    hw = g_pin_lut[dts_pin];
    gpio_set_level(hw, level ? 1 : 0);
    return VFS_OK;
}

int hal_gpio_read_raw_dts(uint32_t dts_port, uint32_t dts_pin, uint8_t *level_out)
{
    gpio_num_t hw;

    if (!level_out)
        return VFS_ERR_INVAL;
    if (!hal_gpio_dts_bounds_ok(dts_port, dts_pin))
        return VFS_ERR_INVAL;

    hw = g_pin_lut[dts_pin];
    *level_out = (uint8_t)(gpio_get_level(hw) ? 1 : 0);
    return VFS_OK;
}

static gpio_mode_t hal_gpio_map_mode(int mode)
{
    switch ((hal_gpio_mode_t)mode)
    {
    case HAL_GPIO_MODE_INPUT:        return GPIO_MODE_INPUT;
    case HAL_GPIO_MODE_OUTPUT:       return GPIO_MODE_OUTPUT;
    case HAL_GPIO_MODE_INPUT_OUTPUT: return GPIO_MODE_INPUT_OUTPUT;
    case HAL_GPIO_MODE_OPEN_DRAIN:   return GPIO_MODE_OUTPUT_OD;
    default:                         return GPIO_MODE_DISABLE;
    }
}

static int hal_gpio_pin_usable(hal_pin_t pin)
{
    return hal_pin_is_valid(pin) &&
           hal_gpio_dts_bounds_ok((uint32_t)HAL_PIN_PORT(pin), (uint32_t)HAL_PIN_NUM(pin));
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
    gpio_num_t pin_num;
    esp_err_t ret;

    if (!cfg || !hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;

    pin_num = g_pin_lut[HAL_PIN_NUM(pin)];
    gpio_reset_pin(pin_num);
    ret = gpio_config(&(gpio_config_t){
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = hal_gpio_map_mode(cfg->mode),
        .pin_bit_mask = (1ULL << pin_num),
        .pull_down_en = (cfg->pull == HAL_GPIO_PULL_DOWN) ? 1 : 0,
        .pull_up_en   = (cfg->pull == HAL_GPIO_PULL_UP) ? 1 : 0,
    });
    if (ret != ESP_OK)
        return ret;
    return VFS_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    if (!hal_gpio_pin_usable(pin))
        return VFS_ERR_INVAL;
    gpio_reset_pin(g_pin_lut[HAL_PIN_NUM(pin)]);
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

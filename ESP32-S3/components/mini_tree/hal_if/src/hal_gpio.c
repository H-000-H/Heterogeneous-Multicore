#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

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

int hal_gpio_set_level(hal_pin_t pin, int level)
{
    return hal_gpio_fast_set_level(pin, level);
}

int hal_gpio_get_level(hal_pin_t pin)
{
    int level = 0;

    if (hal_gpio_fast_get_level(pin, &level) != VFS_OK)
        return VFS_OK;
    return level;
}

int hal_gpio_init(const struct hal_gpio_config *cfg)
{
    hal_pin_t pin;
    gpio_num_t pin_num;
    esp_err_t ret;

    if (!cfg || cfg->pin < 0)
        return VFS_ERR_INVAL;

    pin     = hal_gpio_config_pin(cfg);
    pin_num = (gpio_num_t)HAL_PIN_NUM(pin);
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

int hal_gpio_deinit(const struct hal_gpio_config *cfg)
{
    if (!cfg || cfg->pin < 0)
        return VFS_ERR_INVAL;
    gpio_reset_pin((gpio_num_t)HAL_PIN_NUM(hal_gpio_config_pin(cfg)));
    return VFS_OK;
}

#include "hal_pin_map.h"

int hal_pin_map_hw_gpio(hal_pin_t pin)
{
    /* ESP32-S3: 扁平 GPIO 编号, port 恒为 0 */
    return HAL_PIN_NUM(pin);
}

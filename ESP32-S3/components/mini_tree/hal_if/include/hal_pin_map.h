#ifndef HAL_PIN_MAP_H
#define HAL_PIN_MAP_H

#include "hal_pin.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 逻辑引脚 → 平台 GPIO 硬件编号 (ESP32: SoC GPIO 号; STM32: 由 port 查表) */
int hal_pin_map_hw_gpio(hal_pin_t pin) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_PIN_MAP_H */

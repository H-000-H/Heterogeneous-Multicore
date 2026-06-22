#ifndef HAL_PIN_PROBE_H
#define HAL_PIN_PROBE_H

#include "hal_pin.h"
#include "compiler_compat.h"

struct device;

#ifdef __cplusplus
extern "C"
{
#endif

/* 从设备树属性读取 port + pin 并打包为 hal_pin_t; port_key 可省略 (默认 0) */
int hal_pin_probe(const struct device* dev, const char* port_key, const char* pin_key,
                  hal_pin_t* out) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_PIN_PROBE_H */

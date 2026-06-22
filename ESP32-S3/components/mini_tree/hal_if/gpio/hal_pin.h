#ifndef HAL_PIN_H
#define HAL_PIN_H

#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_GPIO_PORT_DEFAULT 0
#define HAL_PIN_INVALID_NUM   UINT16_MAX

/* * ── 跨平台虚拟逻辑引脚 ──
 * 无论哪个平台，都不要把原生指针或原生掩码丢进来！
 * v[0] (port): 虚拟端口号。如 0=PORTA, 1=PORTB, 2=PORTC... (ESP32固定为0)
 * v[1] (pin):  虚拟引脚号。如 0=PIN0, 1=PIN1, 5=PIN5...
 */
typedef struct hal_pin 
{
    uint16_t v[2];   /* v[0]=port_idx, v[1]=pin_idx */
} hal_pin_t;

#define HAL_PIN_PORT(p) ((int)(p).v[0])
#define HAL_PIN_NUM(p)  ((int)(p).v[1])

#define HAL_MAKE_PIN(port, num) hal_pin_make((int)(port), (uint16_t)(num))

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

/* DTS port/pin → SoC gpio_num (ESP32: port 忽略, pin 查 LUT) */
int hal_gpio_dts_resolve(uint32_t dts_port, uint32_t dts_pin, int *hw_gpio_out) COMPAT_WARN_UNUSED_RESULT;

/* hal_pin_t → SoC gpio_num; 无效 pin 返回 -1 */
int hal_pin_map_hw_gpio(hal_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PIN_H */
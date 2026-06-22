#ifndef HAL_PIN_H
#define HAL_PIN_H

#include <stdint.h>
#include "compiler_compat.h"
#include "VFS.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ── 跨平台逻辑引脚 (port + pin 打包) ──
 *
 * hal_pin_t 为 32-bit 复合标识:
 *   [31:16] 端口号 (Port), STM32/GD32 为 GPIOA..; ESP32 固定为 0
 *   [15: 0] 引脚号 (Pin),  端口内或 SoC 编号, 0 开始
 *
 * DTS 使用 <port-key> + <pin-key> 对 (如 gpio-port/gpio-pin, mosi-port/mosi-pin).
 * 平台 HW 编号由 hal_pin_map_hw_gpio() 完成映射.
 */
typedef uint32_t hal_pin_t;

#define HAL_PIN_PORT_SHIFT 16
#define HAL_PIN_NUM_MASK   0xFFFFU
#define HAL_MAKE_PIN(port, num) \
    (((hal_pin_t)(port) << HAL_PIN_PORT_SHIFT) | ((hal_pin_t)(num) & HAL_PIN_NUM_MASK))
#define HAL_PIN_PORT(pin) ((int)((pin) >> HAL_PIN_PORT_SHIFT))
#define HAL_PIN_NUM(pin)  ((int)((pin) & HAL_PIN_NUM_MASK))

static inline hal_pin_t hal_pin_from_parts(int port, int pin)
{
    int p = (port >= 0) ? port : 0;
    return HAL_MAKE_PIN(p, pin);
}

static inline int COMPAT_WARN_UNUSED_RESULT hal_pin_unpack(uint16_t* port_out,
                                                           uint16_t* pin_out,
                                                           hal_pin_t pin)
{
    if (!port_out || !pin_out)
        return VFS_ERR_INVAL;

    *port_out = (uint16_t)HAL_PIN_PORT(pin);
    *pin_out  = (uint16_t)HAL_PIN_NUM(pin);
    return VFS_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_PIN_H */

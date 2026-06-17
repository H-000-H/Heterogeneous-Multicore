#ifndef HAL_USB_H
#define HAL_USB_H

#include "hal_usb_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 兼容别名: 旧代码中的 hal_usb_dev 即 USB 控制器 */
typedef struct hal_usb_bus hal_usb_dev;

#define hal_usb_init_struct  hal_usb_bus_init_struct

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_H */

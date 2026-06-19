#ifndef HAL_CAN_H
#define HAL_CAN_H

#include "hal_can_bus.h"

#ifdef __cplusplus
extern "C" 
{
#endif

/* 兼容别名: 旧代码中的 hal_can 即总线控制器 */
typedef struct hal_can_bus hal_can;

#define hal_can_init_struct  hal_can_bus_init_struct

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */


#ifndef HAL_PCIE_H
#define HAL_PCIE_H

#include "hal_pcie_bus.h"

#ifdef __cplusplus
extern "C" 
{
#endif

typedef struct hal_pcie_bus hal_pcie;

#define hal_pcie_init_struct  hal_pcie_bus_init_struct

#ifdef __cplusplus
}
#endif

#endif /* HAL_PCIE_H */


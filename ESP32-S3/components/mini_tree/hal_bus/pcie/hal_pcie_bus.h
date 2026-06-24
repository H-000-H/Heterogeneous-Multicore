#ifndef HAL_PCIE_BUS_H
#define HAL_PCIE_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*链路配置*/
/*===========================================================================================================================================================*/
/* PCIe 链路宽度 */
typedef enum
{
    HAL_PCIE_LANE_X1 = 1,
    HAL_PCIE_LANE_X2 = 2,
    HAL_PCIE_LANE_X4 = 4,
    HAL_PCIE_LANE_X8 = 8,
    HAL_PCIE_LANE_X16 = 16,
} hal_pcie_lane_width_t;

/* Root Port / 控制器配置 */
struct hal_pcie_config
{
    int                     port_id;
    hal_pcie_lane_width_t   lane_width;
    int                     ref_clk_pin;    /* -1 = SoC 内置 */
};
/*===========================================================================================================================================================*/

                                                            /*总线实体与 API*/
/*===========================================================================================================================================================*/
struct hal_pcie_bus
{
    int (*init)(struct hal_pcie_bus* pcie, const struct hal_pcie_config* cfg);
    int (*deinit)(struct hal_pcie_bus* pcie);
    int (*link_up)(struct hal_pcie_bus* pcie);
    void* _impl;
};

void hal_pcie_bus_init_struct(struct hal_pcie_bus* pcie);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_pcie_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_PCIE_BUS_H */

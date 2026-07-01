/**
 * SPDX-License-Identifier: Apache-2.0
 * @brief STM32 GPIO 硬件直投实现
 * @note 设计理念：尽量使用ll库函数，减少代码量，提高代码可读性，减少错误率，不是不得已不使用寄存器操作
 */
 #include "hal_gpio.h"
 #include "VFS.h"
 #include "compiler_compat.h"
 #include "stm32f4xx_hal.h"
 #include "stm32f4xx_ll_gpio.h"
 #include "stm32f4xx_ll_bus.h"
 
 int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level)
 {
     if (!pdev)
         return VFS_ERR_INVAL;
 
     GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)pdev->port;
     // 零分支映射：通过位移运算同时兼容高电平(BSRR低16位)与低电平(BSRR高16位)
     LL_GPIO_WriteReg(GPIOx, BSRR, pdev->pin << ((level == 0) << 4U));
 
     return VFS_OK;
 }
 
 int hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int *level_out)
 {
     if (!pdev || !level_out)
         return VFS_ERR_INVAL;
 
     // 归一化为标准的 1 或 0 返回给上层
     *level_out = (LL_GPIO_IsInputPinSet((GPIO_TypeDef*)pdev->port, pdev->pin) != 0U);
     return VFS_OK;
 }
 
 int hal_gpio_fast_toggle(hal_gpio_dev_t* pdev)
 {
     if (!pdev)
         return VFS_ERR_INVAL;
 
     LL_GPIO_TogglePin((GPIO_TypeDef*)pdev->port, pdev->pin);
     return VFS_OK;
 }
 
 /* =========================================================================
  * 纯硬件直投初始化与运行时控制 API
  * ========================================================================= */
 
 int hal_gpio_init(hal_gpio_dev_t* pdev)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     // 开启对应的硬件时钟
     LL_AHB1_GRP1_EnableClock(pdev->clk_rcc_bit);
 
     GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)pdev->port;
     LL_GPIO_InitTypeDef GPIO_InitStruct;
     LL_GPIO_StructInit(&GPIO_InitStruct);
 
     GPIO_InitStruct.Pin        = pdev->pin;
     GPIO_InitStruct.Mode       = pdev->cfg.mode;
     GPIO_InitStruct.Pull       = pdev->cfg.pull;
     GPIO_InitStruct.Speed      = pdev->cfg.speed;
     GPIO_InitStruct.OutputType = pdev->cfg.output_type;
     GPIO_InitStruct.Alternate  = pdev->cfg.af;
 
    if (LL_GPIO_Init(GPIOx, &GPIO_InitStruct) != SUCCESS)
         return VFS_ERR_INVAL;
    pdev->is_used = true;
    return VFS_OK;
 }
 
 int hal_gpio_deinit(hal_gpio_dev_t* pdev)
 {
    if (!pdev || !pdev->is_used)
        return VFS_ERR_INVAL;
    GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)pdev->port;
    LL_GPIO_SetPinMode(GPIOx, pdev->pin, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(GPIOx, pdev->pin, LL_GPIO_PULL_NO);
    pdev->is_used = false;
     return VFS_OK;
 }
 
int hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode)
{
    if (!pdev || !pdev->is_used)
        return VFS_ERR_INVAL;
 
    LL_GPIO_SetPinMode((GPIO_TypeDef*)pdev->port, pdev->pin, mode);
    return VFS_OK;
}
 
 int hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t *mode)
 {
     if (!pdev || !pdev->is_used || !mode)
         return VFS_ERR_INVAL;
 
     *mode = LL_GPIO_GetPinMode((GPIO_TypeDef*)pdev->port, pdev->pin);
     return VFS_OK;
 }
 
 int hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     LL_GPIO_SetPinPull((GPIO_TypeDef*)pdev->port, pdev->pin, pull);
     return VFS_OK;
 }
 
 int hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t *pull)
 {
     if (!pdev || !pdev->is_used || !pull)
         return VFS_ERR_INVAL;
 
     *pull = LL_GPIO_GetPinPull((GPIO_TypeDef*)pdev->port, pdev->pin);
     return VFS_OK;
 }
 
 int hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     LL_GPIO_SetPinSpeed((GPIO_TypeDef*)pdev->port, pdev->pin, speed);
     return VFS_OK;
 }
 
 int hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t *speed)
 {
     if (!pdev || !pdev->is_used || !speed)
         return VFS_ERR_INVAL;
 
     *speed = LL_GPIO_GetPinSpeed((GPIO_TypeDef*)pdev->port, pdev->pin);
     return VFS_OK;
 }
 
 int hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     LL_GPIO_SetPinOutputType((GPIO_TypeDef*)pdev->port, pdev->pin, output_type);
     return VFS_OK;
 }
 
 int hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t *output_type)
 {
     if (!pdev || !pdev->is_used || !output_type)
         return VFS_ERR_INVAL;
 
     *output_type = LL_GPIO_GetPinOutputType((GPIO_TypeDef*)pdev->port, pdev->pin);
     return VFS_OK;
 }
 
 int hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)pdev->port;
     uint32_t pin_pos = (uint32_t)COMPAT_CTZ(pdev->pin);
     uint32_t shift = (pin_pos & 0x07U) * 4U;
 
     // 清空目标引脚所在的4位AFR空间，并写入新的AF值
     GPIOx->AFR[pin_pos >> 3U] = (GPIOx->AFR[pin_pos >> 3U] & ~(0x0FU << shift)) | (af << shift);
 
     return VFS_OK;
 }
 
 int hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t *af)
 {
     if (!pdev || !pdev->is_used || !af)
         return VFS_ERR_INVAL;
 
     GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)pdev->port;
     uint32_t pin_pos = (uint32_t)COMPAT_CTZ(pdev->pin);
 
     // 零分支直读：右移并清空高位，提取出目标引脚对应的 4-bit AF 寄存器值
     *af = (GPIOx->AFR[pin_pos >> 3U] >> ((pin_pos & 0x07U) * 4U)) & 0x0FU;
 
     return VFS_OK;
 }
 
 int hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af)
 {
     if (!pdev || !pdev->is_used)
         return VFS_ERR_INVAL;
 
     // 1. 直投配置 AFR 寄存器值
     if (hal_gpio_set_af(pdev, af) != VFS_OK)
         return VFS_ERR_INVAL;
     
     // 2. 将引脚工作模式切换为复用模式
     LL_GPIO_SetPinMode((GPIO_TypeDef*)pdev->port, pdev->pin, LL_GPIO_MODE_ALTERNATE);
     
     return VFS_OK;
 }
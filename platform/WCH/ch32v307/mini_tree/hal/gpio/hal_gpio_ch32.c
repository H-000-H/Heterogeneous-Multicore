/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO HAL — WCH CH32V307 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给标准外设库。
 * - DTSI 直接提供厂商宏值: gpio-port = <GPIOA_BASE>, gpio-pin = <GPIO_Pin_0>,
 *   gpio-clk  = <RCC_APB2Periph_GPIOA>,
 *   gpio-mode = <GPIO_Mode_Out_PP>  (GPIOMode_TypeDef, mode+pull 编码在一起)
 * - hal_gpio_obj_t 嵌入 VFS (VFS 管生命周期), HAL 无池管理
 * - mode 直接透传给 GPIO_InitTypeDef.GPIO_Mode, 无 switch 翻译, 无自定义枚举
 */
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "ch32v30x.h"

/* =========================================================================
 * 纯硬件直投初始化
 * ========================================================================= */
/**
 * @brief GPIO 硬件直投初始化: DTSI 厂商宏值零翻译透传给标准外设库
 * @param obj GPIO 对象指针 (由 VFS probe 填值)
 * @param cfg 模式配置 (mode 直接承载 GPIOMode_TypeDef, mode+pull 编码在一起)
 * @return 成功返回 VFS_OK, obj/cfg 为空或未激活返回 VFS_ERR_INVAL
 */
int hal_gpio_init(hal_gpio_obj_t* obj, const struct hal_gpio_mode_cfg *cfg)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef*    GPIOx;

    if (!obj || !cfg || !obj->is_used)
        return VFS_ERR_INVAL;

    GPIOx = (GPIO_TypeDef*)obj->port;
    RCC_APB2PeriphClockCmd(obj->clk_periph, ENABLE);

    /* WCH 的 GPIOMode_TypeDef 已把 mode+pull 编码在一起, 直接透传 */
    init.GPIO_Pin   = obj->pin;
    init.GPIO_Speed = GPIO_Speed_50MHz;
    init.GPIO_Mode  = (GPIOMode_TypeDef)cfg->mode;
    GPIO_Init(GPIOx, &init);

    return VFS_OK;
}

/**
 * @brief GPIO 物理级释放: 重置为浮空输入, 等效去初始化
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空或未激活返回 VFS_ERR_INVAL
 */
int hal_gpio_deinit(hal_gpio_obj_t* obj)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef*    GPIOx;

    if (!obj || !obj->is_used)
        return VFS_ERR_INVAL;

    GPIOx = (GPIO_TypeDef*)obj->port;
    /* 物理级释放: 直接重置为浮空输入, 等效去初始化 */
    init.GPIO_Pin  = obj->pin;
    init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOx, &init);
    return VFS_OK;
}

/* =========================================================================
 * Raw 原始接口: 直接数字强转物理硬刷, 不耗费池子
 * ========================================================================= */
/**
 * @brief Raw 原始接口: 直接强转 DTSI 数字为 GPIO 基地址, 配合 BSHR/BCR 寄存器硬刷输出
 * @param dts_port_base DTSI 提供的 GPIO 端口基地址 (如 GPIOA_BASE)
 * @param dts_pin_mask  DTSI 提供的引脚掩码 (如 GPIO_Pin_0)
 * @param level         目标电平 (非零置位, 0 复位)
 * @return 成功返回 VFS_OK
 */
int hal_gpio_write_raw_dts(uint32_t dts_port_base, uint32_t dts_pin_mask, uint8_t level)
{
    /* MCU 地址空间平坦, 直接强转基地址配合寄存器硬刷 */
    GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)dts_port_base;
    if (level)
        GPIOx->BSHR = (uint16_t)dts_pin_mask;
    else
        GPIOx->BCR  = (uint16_t)dts_pin_mask;
    return VFS_OK;
}

/* =========================================================================
 * Fast path: 直接刷寄存器, 零分支零查表 (从 hal_gpio.h 迁出, 头中立化)
 * ========================================================================= */
/**
 * @brief 快路径: 原子设置 GPIO 输出电平 (BSHR 原子置位 / BCR 原子复位)
 * @param obj   GPIO 对象指针
 * @param level 目标电平 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_set_level(hal_gpio_obj_t* obj, int level)
{
    GPIO_TypeDef* GPIOx;

    if (!obj)
        return VFS_ERR_INVAL;
    GPIOx = (GPIO_TypeDef*)obj->port;
    /* BSHR 低16位原子置位, BCR 原子复位 */
    if (level)
        GPIOx->BSHR = obj->pin;
    else
        GPIOx->BCR  = obj->pin;
    return VFS_OK;
}

/**
 * @brief 快路径: 读取 GPIO 当前输入电平 (直接读 INDR 寄存器)
 * @param obj       GPIO 对象指针
 * @param level_out 用于回传电平的指针 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 或 level_out 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_get_level(hal_gpio_obj_t* obj, int *level_out)
{
    GPIO_TypeDef* GPIOx;

    if (!obj || !level_out)
        return VFS_ERR_INVAL;
    GPIOx = (GPIO_TypeDef*)obj->port;
    *level_out = (GPIOx->INDR & obj->pin) ? 1 : 0;
    return VFS_OK;
}

/**
 * @brief 快路径: 翻转 GPIO 输出电平 (异或 OUTDR 寄存器)
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_toggle(hal_gpio_obj_t* obj)
{
    GPIO_TypeDef* GPIOx;

    if (!obj)
        return VFS_ERR_INVAL;
    GPIOx = (GPIO_TypeDef*)obj->port;
    GPIOx->OUTDR ^= obj->pin;
    return VFS_OK;
}

/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO HAL — STM32F4 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 LL 库。
 * - DTSI 直接提供厂商宏值: gpio-port = <GPIOA_BASE>, gpio-pin = <GPIO_PIN_5>,
 *   gpio-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>,
 *   gpio-mode = <LL_GPIO_MODE_OUTPUT>, gpio-pull = <LL_GPIO_PULL_NO>
 * - hal_gpio_obj_t 嵌入 VFS (VFS 管生命周期), HAL 无池管理
 * - mode/pull 直接透传给 LL 库, 无 switch 翻译, 无自定义枚举
 */
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

/* =========================================================================
 * fast path 实现 (从 hal_gpio.h 移出, 直接刷寄存器, 零分支零查表)
 * ========================================================================= */
/**
 * @brief 快路径: 原子设置 GPIO 输出电平 (直接刷 BSRR 寄存器)
 * @param obj   GPIO 对象指针
 * @param level 目标电平 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_set_level(hal_gpio_obj_t* obj, int level)
{
    if (!obj)
        return VFS_ERR_INVAL;
    /* level=1: (!1)<<4 = 0,  掩码不变,    BSRR 低16位 → 原子置位
     * level=0: (!0)<<4 = 16, 掩码左移16位, BSRR 高16位 → 原子复位
     * 无条件跳转, 编译后单条 STR 指令 */
    GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)obj->port;
    GPIOx->BSRR = (uint32_t)obj->pin << ((!level) << 4U);
    return VFS_OK;
}

/**
 * @brief 快路径: 读取 GPIO 当前输入电平 (直接读 IDR 寄存器)
 * @param obj       GPIO 对象指针
 * @param level_out 用于回传电平的指针 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 或 level_out 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_get_level(hal_gpio_obj_t* obj, int *level_out)
{
    if (!obj || !level_out)
        return VFS_ERR_INVAL;
    GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)obj->port;
    *level_out = (GPIOx->IDR & obj->pin) ? 1 : 0;
    return VFS_OK;
}

/**
 * @brief 快路径: 翻转 GPIO 输出电平 (异或 ODR 寄存器)
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_toggle(hal_gpio_obj_t* obj)
{
    if (!obj)
        return VFS_ERR_INVAL;
    GPIO_TypeDef* GPIOx = (GPIO_TypeDef*)obj->port;
    GPIOx->ODR ^= obj->pin;
    return VFS_OK;
}

/* =========================================================================
 * 纯硬件直投初始化
 * ========================================================================= */
/**
 * @brief GPIO 硬件直投初始化: DTSI 厂商宏值零翻译透传给 LL 库
 * @param obj GPIO 对象指针 (由 VFS probe 填值)
 * @param cfg 模式配置 (mode/pull 直接承载 LL_GPIO_MODE_* / LL_GPIO_PULL_* 宏值)
 * @return 成功返回 VFS_OK, obj/cfg 为空或未激活返回 VFS_ERR_INVAL
 */
int hal_gpio_init(hal_gpio_obj_t* obj, const struct hal_gpio_mode_cfg *cfg)
{
    GPIO_TypeDef* GPIOx;

    if (!obj || !cfg || !obj->is_used)
        return VFS_ERR_INVAL;

    GPIOx = (GPIO_TypeDef*)obj->port;
    LL_AHB1_GRP1_EnableClock(obj->clk_periph);

    LL_GPIO_SetPinMode(GPIOx, obj->pin, cfg->mode);
    LL_GPIO_SetPinPull(GPIOx, obj->pin, cfg->pull);

    /* 输出属性默认高速推挽 (后续可根据 DTS 业务扩展) */
    LL_GPIO_SetPinOutputType(GPIOx, obj->pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOx, obj->pin, LL_GPIO_SPEED_FREQ_HIGH);

    return VFS_OK;
}

/**
 * @brief GPIO 物理级释放: 重置为模拟模式 + 无上下拉, 等效去初始化
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空或未激活返回 VFS_ERR_INVAL
 */
int hal_gpio_deinit(hal_gpio_obj_t* obj)
{
    GPIO_TypeDef* GPIOx;

    if (!obj || !obj->is_used)
        return VFS_ERR_INVAL;

    GPIOx = (GPIO_TypeDef*)obj->port;
    /* 物理级释放: 直接重置为模拟模式 + 无上下拉, 等效去初始化 */
    LL_GPIO_SetPinMode(GPIOx, obj->pin, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(GPIOx, obj->pin, LL_GPIO_PULL_NO);
    return VFS_OK;
}

/* =========================================================================
 * Raw 原始接口: 直接数字强转物理硬刷, 不耗费池子
 * ========================================================================= */
/**
 * @brief Raw 原始接口: 直接强转 DTSI 数字为 GPIO 基地址, 配合 LL 库硬刷输出
 * @param dts_port_base DTSI 提供的 GPIO 端口基地址 (如 GPIOA_BASE)
 * @param dts_pin_mask  DTSI 提供的引脚掩码 (如 GPIO_PIN_5)
 * @param level         目标电平 (非零置位, 0 复位)
 * @return 成功返回 VFS_OK
 */
int hal_gpio_write_raw_dts(uint32_t dts_port_base, uint32_t dts_pin_mask, uint8_t level)
{
    /* MCU 地址空间平坦, 直接强转基地址配合 LL 库硬刷 */
    if (level)
        LL_GPIO_SetOutputPin((GPIO_TypeDef *)dts_port_base, (uint16_t)dts_pin_mask);
    else
        LL_GPIO_ResetOutputPin((GPIO_TypeDef *)dts_port_base, (uint16_t)dts_pin_mask);

    return VFS_OK;
}

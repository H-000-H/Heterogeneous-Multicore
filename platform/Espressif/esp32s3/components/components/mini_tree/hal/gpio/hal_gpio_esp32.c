/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO HAL — ESP32-S3 实现
 *
 * DTSI 直接提供 ESP-IDF 枚举值: gpio-port=0/gpio-pin/gpio-clk=0/gpio-mode/gpio-pull,
 * HAL 原样透传给 gpio_config() + gpio_set_pull_mode(), 零翻译零查表。
 * ESP32 适配统一头: port=0/clk_periph=0 (无基地址/时钟概念), pin 承载 SoC GPIO 编号,
 * gpio_config() 内部处理时钟。
 */
#include "hal_gpio.h"
#include "VFS.h"
#include "compiler_compat.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

/**
 * @brief GPIO 硬件直投初始化: DTSI ESP-IDF 枚举值零翻译透传给 gpio_config + gpio_set_pull_mode
 * @param obj GPIO 对象指针 (由 VFS probe 填值)
 * @param cfg 模式配置 (mode/pull 直接承载 gpio_mode_t / gpio_pull_mode_t 枚举值)
 * @return 成功返回 VFS_OK, obj/cfg 为空或未激活返回 VFS_ERR_INVAL, gpio_config 失败返回 VFS_ERR_IO
 */
int hal_gpio_init(hal_gpio_obj_t* obj, const struct hal_gpio_mode_cfg *cfg)
{
    gpio_config_t io_conf;
    esp_err_t ret;

    if (!obj || !cfg || !obj->is_used)
        return VFS_ERR_INVAL;

    gpio_reset_pin((gpio_num_t)obj->pin);
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = (gpio_mode_t)cfg->mode;
    io_conf.pin_bit_mask = (1ULL << obj->pin);
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
        return VFS_ERR_IO;

    gpio_set_pull_mode((gpio_num_t)obj->pin, (gpio_pull_mode_t)cfg->pull);
    return VFS_OK;
}

/**
 * @brief GPIO 反初始化: 调 gpio_reset_pin 复位为默认状态
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空或未激活返回 VFS_ERR_INVAL
 */
int hal_gpio_deinit(hal_gpio_obj_t* obj)
{
    if (!obj || !obj->is_used)
        return VFS_ERR_INVAL;
    gpio_reset_pin((gpio_num_t)obj->pin);
    return VFS_OK;
}

/* =========================================================================
 * Raw 原始接口: 直接 SoC GPIO 编号硬刷, 不耗费池子
 * ========================================================================= */
/**
 * @brief Raw 原始接口: 直接强转 DTSI 数字为 gpio_num_t 调 gpio_set_level 硬刷输出
 * @param dts_port_base ESP32 = SoC GPIO 编号 (STM32/WCH = GPIO 基地址)
 * @param dts_pin_mask  ESP32 忽略 (STM32/WCH = GPIO_PIN_x)
 * @param level         目标电平 (非零置位, 0 复位)
 * @return 成功返回 VFS_OK
 */
int hal_gpio_write_raw_dts(uint32_t dts_port_base, uint32_t dts_pin_mask, uint8_t level)
{
    COMPAT_IGNORE_RESULT(dts_pin_mask);
    gpio_set_level((gpio_num_t)dts_port_base, level);
    return VFS_OK;
}

/* =========================================================================
 * fast path (从头中立化版本 hal_gpio.h 移入此处, vendor 强转在本 .c 内部完成)
 * ========================================================================= */
/**
 * @brief 快路径: 设置 GPIO 输出电平 (直调 ESP-IDF gpio_set_level)
 * @param obj   GPIO 对象指针
 * @param level 目标电平 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_set_level(hal_gpio_obj_t* obj, int level)
{
    if (!obj)
        return VFS_ERR_INVAL;
    gpio_set_level((gpio_num_t)obj->pin, level);
    return VFS_OK;
}

/**
 * @brief 快路径: 读取 GPIO 当前输入电平 (直调 ESP-IDF gpio_get_level)
 * @param obj       GPIO 对象指针
 * @param level_out 用于回传电平的指针 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 或 level_out 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_get_level(hal_gpio_obj_t* obj, int *level_out)
{
    if (!obj || !level_out)
        return VFS_ERR_INVAL;
    *level_out = gpio_get_level((gpio_num_t)obj->pin);
    return VFS_OK;
}

/**
 * @brief 快路径: 翻转 GPIO 输出电平 (读当前电平后写反值)
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_toggle(hal_gpio_obj_t* obj)
{
    if (!obj)
        return VFS_ERR_INVAL;
    int cur = gpio_get_level((gpio_num_t)obj->pin);
    gpio_set_level((gpio_num_t)obj->pin, !cur);
    return VFS_OK;
}

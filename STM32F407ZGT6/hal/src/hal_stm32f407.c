/*
 * hal_stm32f407.c — STM32F407 mini_tree HAL 移植
 *
 * 映射 hal_if 接口到 CMSIS 寄存器 / STM32Cube 外设。
 * 无 WS2812 / pulse engine — 本板未使用该硬件。
 */
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_cpu.h"
#include "hal_wdt.h"
#include "hal_flash.h"
#include "hal_platform_safety.h"

#include "stm32f4xx.h"

#include <stddef.h>
#include <string.h>

#ifndef STM32F407_APP_FLASH_BASE
#define STM32F407_APP_FLASH_BASE 0x08000000U
#endif

#ifndef STM32F407_APP_FLASH_SIZE
#define STM32F407_APP_FLASH_SIZE (1024U * 1024U)
#endif

static GPIO_TypeDef* stm32_gpio_port(int port)
{
    switch (port)
    {
    case 0: return GPIOA;
    case 1: return GPIOB;
    case 2: return GPIOC;
    case 3: return GPIOD;
    case 4: return GPIOE;
    default: return NULL;
    }
}

int hal_gpio_set_level(hal_pin_t pin, int level)
{
    GPIO_TypeDef* port = stm32_gpio_port(HAL_PIN_PORT(pin));
    if (!port) return -1;

    const uint32_t bit = (1U << HAL_PIN_NUM(pin));
    if (level)
        port->BSRR = bit;
    else
        port->BSRR = bit << 16U;
    return 0;
}

int hal_gpio_get_level(hal_pin_t pin)
{
    GPIO_TypeDef* port = stm32_gpio_port(HAL_PIN_PORT(pin));
    if (!port) return 0;

    const uint32_t bit = (1U << HAL_PIN_NUM(pin));
    return (port->IDR & bit) ? 1 : 0;
}

void hal_pwm_init_struct(struct hal_pwm_channel* pwm)
{
    if (pwm)
    {
        pwm->init     = NULL;
        pwm->set_duty = NULL;
        pwm->get_duty = NULL;
        pwm->deinit   = NULL;
        pwm->_impl    = NULL;
    }
}

void hal_pwm_force_stop_all(void)
{
    /* 板级 PWM 紧急停止: 后续可接入 TIM 通道批量拉低 */
}

void hal_cpu_emergency_stop_all_cores(void)
{
    __disable_irq();
    while (1)
    {
        __NOP();
    }
}

bool hal_wdt_init_rtc(uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* IWDG 需 LSI 就绪; 最小桩返回 false, 上层跳过 RTC WDT */
    return false;
}

void hal_wdt_feed_rtc(void) {}
void hal_wdt_rtc_set_long_timeout(void) {}
void hal_wdt_rtc_restore_timeout(void) {}

bool hal_wdt_init_twdt(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return false;
}

bool hal_wdt_subscribe(void* task_handle)
{
    (void)task_handle;
    return false;
}

bool hal_wdt_unsubscribe(void* task_handle)
{
    (void)task_handle;
    return false;
}

void hal_wdt_feed_twdt(void) {}

bool hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{
    if (!buf || len == 0) return false;
    if (addr < STM32F407_APP_FLASH_BASE) return false;
    if ((addr + len) > (STM32F407_APP_FLASH_BASE + STM32F407_APP_FLASH_SIZE)) return false;

    memcpy(buf, (const void*)addr, len);
    return true;
}

uint32_t hal_flash_get_app_addr(void)
{
    return STM32F407_APP_FLASH_BASE;
}

uint32_t hal_flash_get_app_size(void)
{
    return STM32F407_APP_FLASH_SIZE;
}

void hal_platform_critical_hardware_lock(void)
{
    hal_pwm_force_stop_all();
}

void hal_platform_nmi_emergency_stamp(void)
{
    /* 可写入 RTC 备份寄存器; 最小桩为空 */
}

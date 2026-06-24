/*
 * hal_ch32v307.c — CH32V307 mini_tree HAL 移植
 *
 * 映射 hal_if 接口到 WCH 外设库 / 寄存器。
 * 无 WS2812 / pulse engine — 本板未使用该硬件。
 */
#include "hal_pwm.h"
#include "hal_cpu.h"
#include "hal_wdt.h"
#include "hal_flash.h"
#include "hal_dma.h"
#include "hal_platform_safety.h"
#include <stdbool.h>
#include "ch32v30x.h"

#include <stddef.h>
#include <string.h>
#include "compiler_compat_poison.h"

#ifndef CH32V307_APP_FLASH_BASE
#define CH32V307_APP_FLASH_BASE 0x00000000U
#endif

#ifndef CH32V307_APP_FLASH_SIZE
#define CH32V307_APP_FLASH_SIZE (288U * 1024U)
#endif

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
}

void hal_cpu_emergency_stop_all_cores(void)
{
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    while (1)
    {
        __asm__ volatile("nop");
    }
}

bool hal_wdt_init_rtc(uint32_t timeout_ms)
{
    (void)timeout_ms;
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
    if (addr < CH32V307_APP_FLASH_BASE) return false;
    if ((addr + len) > (CH32V307_APP_FLASH_BASE + CH32V307_APP_FLASH_SIZE)) return false;

    memcpy(buf, (const void*)addr, len);
    return true;
}

uint32_t hal_flash_get_app_addr(void)
{
    return CH32V307_APP_FLASH_BASE;
}

uint32_t hal_flash_get_app_size(void)
{
    return CH32V307_APP_FLASH_SIZE;
}

void hal_platform_critical_hardware_lock(void)
{
    hal_pwm_force_stop_all();
    hal_dma_force_stop();
}

void hal_platform_nmi_emergency_stamp(void)
{
}

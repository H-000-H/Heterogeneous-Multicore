/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CPU HAL 层 — 硬件抽象接口 (ESP32-S3, Xtensa LX7 双核)
 *
 * 职责: CPU 紧急停止、AMP 启动、ISR 检测、中断控制。
 * 与 STM32/CH32 hal_cpu.h API 对齐, 内部使用 ESP-IDF ROM API。
 *
 * 平台差异:
 *   - ESP32-S3: 双核 Xtensa, 通过 Xthal_* API 控制中断
 *   - ARM:      单核 Cortex-M, 通过 MRS/MSR 访问 IPSR/PRIMASK
 */
#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*CPU 紧急停止与 AMP API*/
/*===========================================================================================================================================================*/
/* CPU 紧急停止 (IEC 61508 §7.4.3.4 / ISO 26262 第6部分)
 *
 * 单核模式: 仅关当前核心中断
 * 双核模式: 关中断 + 跨核暂停 (挂起对端核心)
 */
void hal_cpu_emergency_stop_all_cores(void);

void hal_cpu_secondary_startup(void);
void hal_cpu_baremetal_entry(void);
int hal_cpu_get_id(void);
/*===========================================================================================================================================================*/

                                                            /*ISR 检测 inline*/
/*===========================================================================================================================================================*/
static inline int hal_is_in_isr(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)
    int ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr;
#elif defined(__riscv)
    int mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    return mcause;
#else
    return 0;
#endif
}

#ifndef DEBUG
#define HAL_ASSERT_NOT_ISR()  ((void)0)
#else
#include "compiler_compat.h"
#define HAL_ASSERT_NOT_ISR()                                             \
    do {                                                                 \
        if (hal_is_in_isr()) {                                           \
            COMPAT_TRAP();                                               \
        }                                                                \
    } while (0)
#endif
/*===========================================================================================================================================================*/

                                                            /*NVIC 中断控制 inline*/
/*===========================================================================================================================================================*/
#define HAL_NVIC_ISER_BASE   0xE000E100UL
#define HAL_NVIC_ICER_BASE   0xE000E180UL
#define HAL_NVIC_IPR_BASE    0xE000E400UL

static inline void hal_irq_enable(int irq_num)
{
    uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
    uint32_t bit = 1UL << (irq_num & 0x1F);
    *(volatile uint32_t*)(HAL_NVIC_ISER_BASE + reg) = bit;
}

static inline void hal_irq_disable(int irq_num)
{
    uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
    uint32_t bit = 1UL << (irq_num & 0x1F);
    *(volatile uint32_t*)(HAL_NVIC_ICER_BASE + reg) = bit;
}

static inline void hal_irq_set_priority(int irq_num, int priority)
{
    *(volatile uint8_t*)(HAL_NVIC_IPR_BASE + (uint32_t)irq_num) = (uint8_t)(priority & 0xFF);
}

static inline int hal_irq_get_priority(int irq_num)
{
    return *(volatile uint8_t*)(HAL_NVIC_IPR_BASE + (uint32_t)irq_num);
}
/*===========================================================================================================================================================*/

                                                            /*全局中断屏蔽 inline*/
/*===========================================================================================================================================================*/
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)

static inline uint32_t hal_irq_disable_all(void)
{
    uint32_t mask;
    __asm__ volatile("mrs %0, primask\n\t"
                     "cpsid i"
                     : "=r"(mask));
    return mask;
}

static inline void hal_irq_restore(uint32_t mask)
{
    __asm__ volatile("msr primask, %0" : : "r"(mask));
}

#else

static inline uint32_t hal_irq_disable_all(void) { uint32_t m; __asm__ volatile("" : "=r"(m)); return m; }
static inline void hal_irq_restore(uint32_t mask) { COMPAT_IGNORE_RESULT(mask); }

#endif
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_H */

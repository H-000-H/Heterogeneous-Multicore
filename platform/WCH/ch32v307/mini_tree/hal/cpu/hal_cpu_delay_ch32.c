/*
 * CPU 延时 — CH32 SysTick/RISC-V 周期计数实现
 *
 * us/ms 延时委托 WCH Delay_Init/Delay_Us/Delay_Ms (基于 SysTick)
 * hal_delay_cycles 使用 rdcycle 指令读取 RISC-V 机器周期计数
 */
#include "hal_cpu_delay.h"
#include "debug.h"

void hal_delay_init(void)
{
    Delay_Init();
}

void hal_delay_us(uint32_t us)
{
    if (us == 0U)
        return;
    Delay_Us(us);
}

void hal_delay_ms(uint32_t ms)
{
    if (ms == 0U)
        return;
    Delay_Ms(ms);
}

void hal_delay_cycles(uint32_t cycles)
{
    if (cycles == 0U)
        return;

    uint32_t start;
    __asm__ volatile("rdcycle %0" : "=r"(start));
    uint32_t now;
    do
    {
        __asm__ volatile("rdcycle %0" : "=r"(now));
    } while ((now - start) < cycles);
}

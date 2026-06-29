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

/*
 * CPU 延时 — STM32 DWT/SysTick 实现
 *
 * Cortex-M3+ 使用 DWT->CYCCNT 周期计数, M0 退化为 SysTick 倒计数
 * hal_delay_cycles 直读 DWT, hal_delay_ms 复用 hal_delay_us
 */
#include "hal_cpu_delay.h"
#include "stm32f4xx.h"

static uint32_t hal_delay_us_to_ticks(uint32_t us)
{
    return (uint32_t)((uint64_t)us * SystemCoreClock / 1000000UL);
}

void hal_delay_init(void)
{
#if defined(__CORTEX_M) && (__CORTEX_M >= 3U)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

void hal_delay_us(uint32_t us)
{
    if (us == 0U)
        return;

#if defined(__CORTEX_M) && (__CORTEX_M >= 3U)
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = hal_delay_us_to_ticks(us);
    while ((DWT->CYCCNT - start) < ticks)
    {
    }
#elif defined(__CORTEX_M)
    uint32_t ticks = hal_delay_us_to_ticks(us);
    uint32_t start = SysTick->VAL;
    int32_t remaining = (int32_t)ticks;

    while (remaining > 0)
    {
        uint32_t now = SysTick->VAL;
        int32_t elapsed = (int32_t)(start - now);
        if (elapsed < 0)
            elapsed += (int32_t)SysTick->LOAD + 1;
        remaining -= elapsed;
        start = now;
    }
#else
    volatile uint32_t n = hal_delay_us_to_ticks(us) >> 2;
    while (n--)
    {
        __NOP();
    }
#endif
}

void hal_delay_cycles(uint32_t cycles)
{
    if (cycles == 0U)
        return;

#if defined(__CORTEX_M) && (__CORTEX_M >= 3U)
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles)
    {
    }
#else
    volatile uint32_t n = cycles >> 1;
    if (n == 0U)
        n = 1U;
    while (n--)
    {
        __NOP();
    }
#endif
}

void hal_delay_ms(uint32_t ms)
{
    while (ms--)
        hal_delay_us(1000U);
}

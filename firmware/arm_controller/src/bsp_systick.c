#include "bsp_systick.h"

volatile uint32_t g_tick = 0;

/* 按指定周期(us)配置 SysTick，中断中累加 g_tick */
void systick_init_us(uint32_t period_us)
{
    if (period_us == 0U) return;

    uint64_t ticks64 = ((uint64_t)SystemCoreClock * period_us) / 1000000ULL;
    if (ticks64 < 1ULL) return;
    if (ticks64 > 16777216ULL) return;   /* SysTick 重装值为 24 位 */

    SysTick_Config((uint32_t)ticks64);
}

void SysTick_Handler(void)
{
    g_tick++;
}

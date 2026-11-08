/*
 * 软件 RS485 物理层：DWT 周期计数延时、GPIO 配置与直接寄存器读写、
 * 逐位发送/轮询接收，以及 IRQ13 边沿触发接收和 GPT 定时扫描接收两条中断路径。
 */
#include "rs485_internal.h"

#include "bsp_pin_cfg.h"
#include "hal_data.h"

#include <rtdevice.h>
#include <rthw.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 直接读端口输入寄存器，ISR 中逐位采样需要尽量短的路径 */
static rt_uint8_t rs485_app_rx_get(void)
{
    return ((R_PORT0->PCNTR2 & RS485_APP_RX_IRQ_MASK) != 0U) ? PIN_HIGH : PIN_LOW;
}

void rs485_app_cycle_counter_init(void)
{
    if ((DWT->CTRL & (1UL << DWT_CTRL_NOCYCCNT_Pos)) == 0U)
    {
        CoreDebug->DEMCR |= (1UL << CoreDebug_DEMCR_TRCENA_Pos);
        DWT->CTRL |= (1UL << DWT_CTRL_CYCCNTENA_Pos);
    }
}

static void rs485_app_cycle_wait_until(uint32_t target)
{
    while ((int32_t)(DWT->CYCCNT - target) < 0)
    {
    }
}

static void rs485_app_cycle_delay(uint32_t cycles)
{
    rs485_app_cycle_wait_until(DWT->CYCCNT + cycles);
}

static uint32_t rs485_app_us_to_cycles(uint32_t us)
{
    return (uint32_t)(((uint64_t)SystemCoreClock * us + 999999ULL) / 1000000ULL);
}

void rs485_app_cycle_delay_us(uint32_t us)
{
    rs485_app_cycle_delay(rs485_app_us_to_cycles(us));
}

static int rs485_app_timer_ensure_open(void)
{
    fsp_err_t err;

    if (g_rs485_timer_opened == RT_TRUE)
    {
        return RT_EOK;
    }

    err = R_GPT_Open(&g_rs485_rx_timer_ctrl, &g_rs485_rx_timer_cfg);
    if ((err != FSP_SUCCESS) && (err != FSP_ERR_ALREADY_OPEN))
    {
        return -RT_ERROR;
    }

    (void)R_GPT_Stop(&g_rs485_rx_timer_ctrl);
    g_rs485_timer_opened = RT_TRUE;
    return RT_EOK;
}

static void rs485_app_timer_start_scan_period(void)
{
    (void)R_GPT_Stop(&g_rs485_rx_timer_ctrl);
    (void)R_GPT_PeriodSet(&g_rs485_rx_timer_ctrl, g_rs485_gpt_scan_counts);
    (void)R_GPT_Reset(&g_rs485_rx_timer_ctrl);
    (void)R_GPT_Start(&g_rs485_rx_timer_ctrl);
}

int rs485_app_timer_rx_start(uint32_t timeout_ms)
{
    uint64_t scan_count;

    if (rs485_app_ensure_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (rs485_app_timer_ensure_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (timeout_ms == 0U)
    {
        timeout_ms = RS485_APP_TIMER_RX_DEFAULT_MS;
    }
    if (timeout_ms > RS485_APP_TIMER_RX_MAX_MS)
    {
        timeout_ms = RS485_APP_TIMER_RX_MAX_MS;
    }

    if (g_rs485_timer_rx_running == RT_TRUE)
    {
        return RT_EOK;
    }

    scan_count = (((uint64_t)timeout_ms * 1000ULL) + RS485_APP_TIMER_RX_SCAN_US - 1ULL) /
                 RS485_APP_TIMER_RX_SCAN_US;
    if (scan_count == 0ULL)
    {
        scan_count = 1ULL;
    }
    if (scan_count > 0xFFFFFFFFULL)
    {
        scan_count = 0xFFFFFFFFULL;
    }

    g_rs485_timer_rx_remaining_scans = (uint32_t)scan_count;
    g_rs485_timer_rx_in_byte = RT_FALSE;
    g_rs485_timer_rx_running = RT_TRUE;
    g_rs485_stats.rx_timer_windows++;

    rs485_app_timer_start_scan_period();
    return RT_EOK;
}

void rs485_app_timer_rx_stop(void)
{
    g_rs485_timer_rx_running = RT_FALSE;
    g_rs485_timer_rx_in_byte = RT_FALSE;
    g_rs485_timer_rx_remaining_scans = 0U;

    if (g_rs485_timer_opened == RT_TRUE)
    {
        (void)R_GPT_Stop(&g_rs485_rx_timer_ctrl);
    }
}

static int rs485_app_irq_ensure_open(void)
{
    fsp_err_t err;

    if (g_rs485_irq_opened == RT_TRUE)
    {
        return RT_EOK;
    }

    err = R_ICU_ExternalIrqOpen(&g_external_irq13_ctrl, &g_external_irq13_cfg);
    if ((err != FSP_SUCCESS) && (err != FSP_ERR_ALREADY_OPEN))
    {
        return -RT_ERROR;
    }

    g_rs485_irq_opened = RT_TRUE;
    return RT_EOK;
}

int rs485_app_irq_rx_start(void)
{
    fsp_err_t err;

    if (rs485_app_irq_ensure_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    g_rs485_irq_rx_enabled = RT_TRUE;
    if (g_rs485_irq_enabled == RT_TRUE)
    {
        return RT_EOK;
    }

    err = R_ICU_ExternalIrqEnable(&g_external_irq13_ctrl);
    if (err != FSP_SUCCESS)
    {
        return -RT_ERROR;
    }

    g_rs485_irq_enabled = RT_TRUE;
    return RT_EOK;
}

void rs485_app_irq_rx_stop(void)
{
    g_rs485_irq_rx_enabled = RT_FALSE;

    if ((g_rs485_irq_opened == RT_TRUE) && (g_rs485_irq_enabled == RT_TRUE))
    {
        (void)R_ICU_ExternalIrqDisable(&g_external_irq13_ctrl);
        g_rs485_irq_enabled = RT_FALSE;
    }
}

static rt_bool_t rs485_app_irq_wait_next_start(uint32_t search_cycle,
                                               uint32_t timeout_cycles,
                                               uint32_t *next_start_cycle)
{
    uint32_t deadline = search_cycle + timeout_cycles;

    rs485_app_cycle_wait_until(search_cycle);
    while ((int32_t)(DWT->CYCCNT - deadline) < 0)
    {
        if (rs485_app_rx_get() == PIN_LOW)
        {
            if (next_start_cycle != RT_NULL)
            {
                *next_start_cycle = DWT->CYCCNT;
            }
            return RT_TRUE;
        }
    }

    return RT_FALSE;
}

void external_irq13(external_irq_callback_args_t *p_args)
{
    rt_uint8_t value = 0U;
    rt_base_t level;
    uint32_t start_cycle;
    uint32_t sample_cycle;
    uint32_t stop_sample_cycle;
    uint32_t next_start_cycle;
    uint32_t i;
    uint32_t burst_count = 0U;
    uint32_t max_burst;

    RT_UNUSED(p_args);

    if ((g_rs485_irq_rx_enabled != RT_TRUE) ||
        (g_rs485_tx_busy == RT_TRUE) ||
        (g_rs485_irq_rx_busy == RT_TRUE))
    {
        g_rs485_stats.rx_irq_ignored++;
        return;
    }

    if (rs485_app_rx_get() != PIN_LOW)
    {
        g_rs485_stats.rx_irq_ignored++;
        return;
    }

    g_rs485_irq_rx_busy = RT_TRUE;
    g_rs485_stats.rx_irq_edges++;
    (void)R_ICU_ExternalIrqDisable(&g_external_irq13_ctrl);

    max_burst = g_rs485_irq_rx_max_burst;
    if (max_burst == 0U)
    {
        max_burst = 1U;
    }

    level = rt_hw_interrupt_disable();
    start_cycle = DWT->CYCCNT;

    while (burst_count < max_burst)
    {
        rs485_app_cycle_wait_until(start_cycle + (g_rs485_bit_cycles / 2U));
        if (rs485_app_rx_get() != PIN_LOW)
        {
            g_rs485_stats.rx_errors++;
            break;
        }

        value = 0U;
        sample_cycle = start_cycle + g_rs485_bit_cycles + (g_rs485_bit_cycles / 2U);
        for (i = 0U; i < 8U; i++)
        {
            rs485_app_cycle_wait_until(sample_cycle);
            if (rs485_app_rx_get() != PIN_LOW)
            {
                value |= (rt_uint8_t)(1U << i);
            }
            sample_cycle += g_rs485_bit_cycles;
        }

        stop_sample_cycle = start_cycle + (g_rs485_bit_cycles * 9U) + (g_rs485_bit_cycles / 2U);
        rs485_app_cycle_wait_until(stop_sample_cycle);
        if (rs485_app_rx_get() == PIN_LOW)
        {
            g_rs485_stats.rx_errors++;
            break;
        }

        if (rs485_app_ring_write_byte_from_isr(value) > 0U)
        {
            g_rs485_stats.rx_irq_bytes++;
        }
        burst_count++;

        if (burst_count >= max_burst)
        {
            break;
        }

        if (rs485_app_irq_wait_next_start(stop_sample_cycle,
                                          g_rs485_bit_cycles * RS485_APP_IRQ_RX_NEXT_START_BITS,
                                          &next_start_cycle) != RT_TRUE)
        {
            break;
        }

        start_cycle = next_start_cycle;
    }

    if (burst_count > 0U)
    {
        g_rs485_stats.rx_irq_bursts++;
        if (burst_count > g_rs485_stats.rx_irq_max_burst)
        {
            g_rs485_stats.rx_irq_max_burst = burst_count;
        }
    }

    rt_hw_interrupt_enable(level);
    g_rs485_irq_rx_busy = RT_FALSE;
    if ((g_rs485_irq_rx_enabled == RT_TRUE) && (g_rs485_tx_busy != RT_TRUE))
    {
        (void)R_ICU_ExternalIrqEnable(&g_external_irq13_ctrl);
        g_rs485_irq_enabled = RT_TRUE;
    }
    else
    {
        g_rs485_irq_enabled = RT_FALSE;
    }
}

void rs485_app_gpio_config(void)
{
    R_BSP_PinAccessEnable();
    R_BSP_PinCfg(RS485_APP_TX_PIN,
                 IOPORT_CFG_DRIVE_HS_HIGH |
                 IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                 IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_BSP_PinCfg(RS485_APP_RX_IRQ_PIN,
                 IOPORT_CFG_IRQ_ENABLE |
                 IOPORT_CFG_PORT_DIRECTION_INPUT |
                 IOPORT_CFG_PULLUP_ENABLE);
    R_BSP_PinCfg(RS485_APP_DE_PIN,
                 IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                 IOPORT_CFG_PORT_OUTPUT_LOW);
    R_BSP_PinAccessDisable();
}

void rs485_app_tx_set(rt_uint8_t level)
{
    R_PORT8->PCNTR3 = (level != PIN_LOW) ? RS485_APP_TX_MASK : (RS485_APP_TX_MASK << 16);
}


void rs485_app_de_set(rt_uint8_t level)
{
    R_PORT0->PCNTR3 = (level != PIN_LOW) ? RS485_APP_DE_MASK : (RS485_APP_DE_MASK << 16);
}

void rs485_app_softuart_tx_byte(rt_uint8_t value)
{
    rt_base_t level;
    uint32_t target;
    uint32_t i;

    level = rt_hw_interrupt_disable();

    target = DWT->CYCCNT + g_rs485_bit_cycles;
    rs485_app_tx_set(PIN_LOW);
    rs485_app_cycle_wait_until(target);

    for (i = 0U; i < 8U; i++)
    {
        target += g_rs485_bit_cycles;
        rs485_app_tx_set((value & (1U << i)) ? PIN_HIGH : PIN_LOW);
        rs485_app_cycle_wait_until(target);
    }

    target += g_rs485_bit_cycles;
    rs485_app_tx_set(PIN_HIGH);
    rs485_app_cycle_wait_until(target);

    rt_hw_interrupt_enable(level);
}

int rs485_app_softuart_rx_byte(rt_uint8_t *out_byte, uint32_t timeout_ms)
{
    rt_tick_t start_tick = rt_tick_get();
    rt_uint8_t value = 0U;
    rt_base_t level;
    uint32_t start_cycle;
    uint32_t sample_cycle;
    uint32_t i;

    if (out_byte == RT_NULL)
    {
        return 0;
    }

    while ((timeout_ms == 0U) ||
           (rs485_app_ticks_to_ms(rt_tick_get() - start_tick) < timeout_ms))
    {
        if (rs485_app_rx_get() == PIN_LOW)
        {
            start_cycle = DWT->CYCCNT;
            level = rt_hw_interrupt_disable();

            rs485_app_cycle_wait_until(start_cycle + (g_rs485_bit_cycles / 2U));
            if (rs485_app_rx_get() != PIN_LOW)
            {
                g_rs485_stats.rx_errors++;
                rt_hw_interrupt_enable(level);
                return 0;
            }

            sample_cycle = start_cycle + g_rs485_bit_cycles + (g_rs485_bit_cycles / 2U);
            for (i = 0U; i < 8U; i++)
            {
                rs485_app_cycle_wait_until(sample_cycle);
                if (rs485_app_rx_get() != PIN_LOW)
                {
                    value |= (rt_uint8_t)(1U << i);
                }
                sample_cycle += g_rs485_bit_cycles;
            }

            /* 等待进入停止位，避免下一次调用把数据位 bit7 误判为新的起始位 */
            rs485_app_cycle_wait_until(start_cycle + (g_rs485_bit_cycles * 9U) + (g_rs485_bit_cycles / 4U));
            if (rs485_app_rx_get() == PIN_LOW)
            {
                g_rs485_stats.rx_errors++;
                rt_hw_interrupt_enable(level);
                return 0;
            }

            rt_hw_interrupt_enable(level);

            *out_byte = value;
            return 1;
        }

        if (timeout_ms == 0U)
        {
            break;
        }
    }

    return 0;
}

void rs485_app_rx_timer_cb(timer_callback_args_t *p_args)
{
    typedef enum
    {
        RS485_TIMER_RX_SCAN = 0,
        RS485_TIMER_RX_CONFIRM_START,
        RS485_TIMER_RX_DATA,
        RS485_TIMER_RX_STOP,
    } rs485_timer_rx_state_t;

    static rs485_timer_rx_state_t state = RS485_TIMER_RX_SCAN;
    static rt_uint8_t value = 0U;
    static rt_uint8_t bit_index = 0U;
    static rt_uint8_t low_samples = 0U;
    static rt_uint16_t tick_acc_q8 = 0U;
    static rt_uint16_t ticks_to_sample = 0U;

    if ((p_args == RT_NULL) || (p_args->event != TIMER_EVENT_CYCLE_END))
    {
        return;
    }

    if (g_rs485_timer_rx_running != RT_TRUE)
    {
        (void)R_GPT_Stop(&g_rs485_rx_timer_ctrl);
        state = RS485_TIMER_RX_SCAN;
        g_rs485_timer_rx_in_byte = RT_FALSE;
        return;
    }

    g_rs485_timer_irq_rounds++;

    if (g_rs485_timer_rx_remaining_scans > 0U)
    {
        g_rs485_timer_rx_remaining_scans--;
    }

    if ((g_rs485_timer_rx_remaining_scans == 0U) && (g_rs485_timer_rx_in_byte != RT_TRUE))
    {
        g_rs485_timer_rx_running = RT_FALSE;
        g_rs485_timer_rx_in_byte = RT_FALSE;
        g_rs485_stats.rx_timer_timeouts++;
        (void)R_GPT_Stop(&g_rs485_rx_timer_ctrl);
        state = RS485_TIMER_RX_SCAN;
        return;
    }

    if (g_rs485_tx_busy == RT_TRUE)
    {
        state = RS485_TIMER_RX_SCAN;
        g_rs485_timer_rx_in_byte = RT_FALSE;
        return;
    }

    switch (state)
    {
    case RS485_TIMER_RX_SCAN:
        if (rs485_app_rx_get() == PIN_LOW)
        {
            low_samples = 1U;
            g_rs485_timer_rx_in_byte = RT_TRUE;
            state = RS485_TIMER_RX_CONFIRM_START;
        }
        break;

    case RS485_TIMER_RX_CONFIRM_START:
        if (rs485_app_rx_get() == PIN_LOW)
        {
            low_samples++;
            if (low_samples >= 2U)
            {
                value = 0U;
                bit_index = 0U;
                ticks_to_sample = g_rs485_timer_first_sample_ticks;
                tick_acc_q8 = 0U;
                g_rs485_stats.rx_timer_edges++;
                state = RS485_TIMER_RX_DATA;
            }
        }
        else
        {
            g_rs485_stats.rx_errors++;
            g_rs485_timer_rx_in_byte = RT_FALSE;
            state = RS485_TIMER_RX_SCAN;
        }
        break;

    case RS485_TIMER_RX_DATA:
        if (ticks_to_sample > 1U)
        {
            ticks_to_sample--;
            break;
        }

        if (rs485_app_rx_get() != PIN_LOW)
        {
            value |= (rt_uint8_t)(1U << bit_index);
        }

        tick_acc_q8 = (uint16_t)(tick_acc_q8 + g_rs485_timer_bit_ticks_q8);
        ticks_to_sample = (rt_uint16_t)(tick_acc_q8 >> 8);
        tick_acc_q8 &= 0xFFU;
        if (ticks_to_sample == 0U)
        {
            ticks_to_sample = 1U;
        }

        bit_index++;
        if (bit_index >= 8U)
        {
            state = RS485_TIMER_RX_STOP;
        }
        break;

    case RS485_TIMER_RX_STOP:
        if (ticks_to_sample > 1U)
        {
            ticks_to_sample--;
            break;
        }

        if (rs485_app_rx_get() != PIN_LOW)
        {
            (void)rs485_app_ring_write_byte_from_isr(value);
        }
        else
        {
            g_rs485_stats.rx_errors++;
        }

        g_rs485_timer_rx_in_byte = RT_FALSE;
        state = RS485_TIMER_RX_SCAN;
        break;

    default:
        g_rs485_timer_rx_in_byte = RT_FALSE;
        state = RS485_TIMER_RX_SCAN;
        break;
    }
}

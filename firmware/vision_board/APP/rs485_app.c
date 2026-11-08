/*
 * 软件 RS485：对外收发接口、接收环形缓冲区、波特率参数计算，
 * 以及后台接收服务线程与转发监视线程。
 * 物理层位时序见 rs485_phy.c，msh 调试命令见 rs485_cmd.c。
 */
#include "rs485_internal.h"

#include "bsp_pin_cfg.h"
#include "hal_data.h"

#include <rtdevice.h>
#include <rthw.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static rt_bool_t s_rs485_ready = RT_FALSE;
uint32_t g_rs485_baud = RS485_APP_DEFAULT_BAUD;
static uint32_t s_rs485_bit_us = 9U;
uint32_t g_rs485_bit_cycles = 0U;
uint32_t g_rs485_irq_rx_max_burst = RS485_APP_IRQ_RX_BURST_HIGH;
uint32_t g_rs485_gpt_scan_counts = 480U;
static uint32_t s_rs485_gpt_bit_counts = 1042U;
uint16_t g_rs485_timer_bit_ticks_q8 = 556U;
uint16_t g_rs485_timer_first_sample_ticks = 2U;
volatile rt_bool_t g_rs485_tx_busy = RT_FALSE;
volatile rt_bool_t g_rs485_service_running = RT_FALSE;
static rt_thread_t s_rs485_service_thread = RT_NULL;
uint32_t g_rs485_service_poll_ms = RS485_APP_SERVICE_DEFAULT_POLL_MS;
uint32_t g_rs485_service_sleep_ms = RS485_APP_SERVICE_DEFAULT_SLEEP_MS;
volatile rt_bool_t g_rs485_forward_running = RT_FALSE;
static rt_thread_t s_rs485_forward_thread = RT_NULL;
static rt_uint8_t s_rs485_rx_ring[RS485_APP_RX_RING_SIZE];
static volatile rt_uint16_t s_rs485_rx_head = 0U;
static volatile rt_uint16_t s_rs485_rx_tail = 0U;
volatile rt_uint16_t g_rs485_rx_count = 0U;
volatile rt_bool_t g_rs485_timer_opened = RT_FALSE;
volatile rt_bool_t g_rs485_timer_rx_running = RT_FALSE;
volatile rt_bool_t g_rs485_timer_rx_in_byte = RT_FALSE;
volatile uint32_t g_rs485_timer_rx_remaining_scans = 0U;
volatile uint32_t g_rs485_timer_irq_rounds = 0U;
volatile rt_bool_t g_rs485_irq_opened = RT_FALSE;
volatile rt_bool_t g_rs485_irq_enabled = RT_FALSE;
volatile rt_bool_t g_rs485_irq_rx_enabled = RT_FALSE;
volatile rt_bool_t g_rs485_irq_rx_busy = RT_FALSE;

rs485_app_stats_t g_rs485_stats;

uint32_t rs485_app_ticks_to_ms(rt_tick_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) / RT_TICK_PER_SECOND);
}

uint32_t rs485_app_parse_u32(const char *text, uint32_t default_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return default_value;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return default_value;
    }

    return (uint32_t)value;
}

void rs485_app_stats_clear(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    memset(&g_rs485_stats, 0, sizeof(g_rs485_stats));
    s_rs485_rx_head = 0U;
    s_rs485_rx_tail = 0U;
    g_rs485_rx_count = 0U;

    rt_hw_interrupt_enable(level);
}

void rs485_app_clear_rx(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    s_rs485_rx_head = 0U;
    s_rs485_rx_tail = 0U;
    g_rs485_rx_count = 0U;

    rt_hw_interrupt_enable(level);
}

rt_size_t rs485_app_ring_write_byte_from_isr(rt_uint8_t byte)
{
    if (g_rs485_rx_count < RS485_APP_RX_RING_SIZE)
    {
        s_rs485_rx_ring[s_rs485_rx_head] = byte;
        s_rs485_rx_head = (rt_uint16_t)((s_rs485_rx_head + 1U) % RS485_APP_RX_RING_SIZE);
        g_rs485_rx_count++;
        g_rs485_stats.rx_bytes++;
        return 1U;
    }

    g_rs485_stats.rx_overflows++;
    return 0U;
}

static rt_size_t rs485_app_ring_read(void *buffer, rt_size_t size)
{
    rt_uint8_t *bytes = (rt_uint8_t *)buffer;
    rt_size_t count = 0U;
    rt_base_t level;

    if ((bytes == RT_NULL) || (size == 0U))
    {
        return 0U;
    }

    level = rt_hw_interrupt_disable();
    while ((count < size) && (g_rs485_rx_count > 0U))
    {
        bytes[count++] = s_rs485_rx_ring[s_rs485_rx_tail];
        s_rs485_rx_tail = (rt_uint16_t)((s_rs485_rx_tail + 1U) % RS485_APP_RX_RING_SIZE);
        g_rs485_rx_count--;
    }
    rt_hw_interrupt_enable(level);

    return count;
}

rt_uint16_t rs485_app_ring_count(void)
{
    rt_uint16_t count;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    count = g_rs485_rx_count;
    rt_hw_interrupt_enable(level);

    return count;
}

static void rs485_app_update_baud(uint32_t baud)
{
    uint64_t timer_counts_per_second;
    uint64_t bit_counts;
    uint32_t bit_ticks_q8;
    uint32_t first_sample_q8;

    if ((baud < RS485_APP_MIN_BAUD) || (baud > RS485_APP_MAX_BAUD))
    {
        baud = RS485_APP_DEFAULT_BAUD;
    }

    g_rs485_baud = baud;
    s_rs485_bit_us = (1000000U + (baud / 2U)) / baud;
    if (s_rs485_bit_us == 0U)
    {
        s_rs485_bit_us = 1U;
    }

    g_rs485_bit_cycles = (uint32_t)(((uint64_t)SystemCoreClock + (baud / 2U)) / baud);
    if (g_rs485_bit_cycles == 0U)
    {
        g_rs485_bit_cycles = 1U;
    }

    g_rs485_gpt_scan_counts = g_rs485_rx_timer_cfg.period_counts / 2U;
    if (g_rs485_gpt_scan_counts == 0U)
    {
        g_rs485_gpt_scan_counts = 1U;
    }

    timer_counts_per_second = ((uint64_t)g_rs485_gpt_scan_counts * 1000000ULL) /
                              RS485_APP_TIMER_RX_SCAN_US;
    bit_counts = (timer_counts_per_second + (baud / 2U)) / baud;
    if (bit_counts < 2ULL)
    {
        bit_counts = 2ULL;
    }
    if (bit_counts > 0xFFFFFFFFULL)
    {
        bit_counts = 0xFFFFFFFFULL;
    }

    s_rs485_gpt_bit_counts = (uint32_t)bit_counts;
    bit_ticks_q8 = (uint32_t)((1000000ULL * 256ULL + ((uint64_t)baud * RS485_APP_TIMER_RX_SCAN_US / 2ULL)) /
                              ((uint64_t)baud * RS485_APP_TIMER_RX_SCAN_US));
    if (bit_ticks_q8 > 0xFFFFU)
    {
        bit_ticks_q8 = 0xFFFFU;
    }

    g_rs485_timer_bit_ticks_q8 = (uint16_t)bit_ticks_q8;
    if (g_rs485_timer_bit_ticks_q8 < 256U)
    {
        g_rs485_timer_bit_ticks_q8 = 256U;
    }

    /*
     * 首个低电平采样最多可能晚一个扫描周期；在额外的起始位确认节拍之后，
     * 再延时到数据位 bit0 的中心采样。
     */
    first_sample_q8 = ((uint32_t)g_rs485_timer_bit_ticks_q8 * 3U) / 2U;
    if (first_sample_q8 > 384U)
    {
        first_sample_q8 -= 384U;
    }
    else
    {
        first_sample_q8 = 256U;
    }

    g_rs485_timer_first_sample_ticks = (uint16_t)((first_sample_q8 + 128U) / 256U);
    if (g_rs485_timer_first_sample_ticks < 1U)
    {
        g_rs485_timer_first_sample_ticks = 1U;
    }

    if (baud >= RS485_APP_IRQ_RX_BURST_BAUD_HIGH)
    {
        g_rs485_irq_rx_max_burst = RS485_APP_IRQ_RX_BURST_HIGH;
    }
    else if (baud >= RS485_APP_IRQ_RX_BURST_BAUD_MID)
    {
        g_rs485_irq_rx_max_burst = RS485_APP_IRQ_RX_BURST_MID;
    }
    else
    {
        g_rs485_irq_rx_max_burst = RS485_APP_IRQ_RX_BURST_LOW;
    }
}


const char *rs485_app_device_name(void)
{
    return RS485_APP_DEFAULT_DEVICE;
}

int rs485_app_open(const char *dev_name)
{
    uint32_t baud = RS485_APP_DEFAULT_BAUD;

    if ((dev_name != RT_NULL) && (dev_name[0] != '\0') && (strcmp(dev_name, RS485_APP_DEFAULT_DEVICE) != 0))
    {
        baud = rs485_app_parse_u32(dev_name, RS485_APP_DEFAULT_BAUD);
    }

    rs485_app_irq_rx_stop();
    rs485_app_timer_rx_stop();
    rs485_app_update_baud(baud);
    rs485_app_cycle_counter_init();
    rs485_app_gpio_config();

    rs485_app_tx_set(PIN_HIGH);
    rs485_app_de_set(PIN_LOW);

    s_rs485_ready = RT_TRUE;
    if (rs485_app_irq_rx_start() != RT_EOK)
    {
        s_rs485_ready = RT_FALSE;
        return -RT_ERROR;
    }

    return RT_EOK;
}

int rs485_app_ensure_open(void)
{
    if (s_rs485_ready == RT_TRUE)
    {
        return RT_EOK;
    }

    return rs485_app_open(RS485_APP_DEFAULT_DEVICE);
}

rt_size_t rs485_app_send(const void *data, rt_size_t size)
{
    const rt_uint8_t *bytes = (const rt_uint8_t *)data;
    rt_size_t i;

    if ((bytes == RT_NULL) || (size == 0U) || (rs485_app_ensure_open() != RT_EOK))
    {
        return 0U;
    }

    rs485_app_timer_rx_stop();
    rs485_app_irq_rx_stop();

    g_rs485_tx_busy = RT_TRUE;
    rs485_app_de_set(PIN_HIGH);
    rs485_app_cycle_delay_us(RS485_APP_DE_ASSERT_DELAY_US);

    for (i = 0U; i < size; i++)
    {
        rs485_app_softuart_tx_byte(bytes[i]);
    }

    rs485_app_cycle_delay_us(RS485_APP_DE_RELEASE_DELAY_US);
    rs485_app_de_set(PIN_LOW);
    g_rs485_tx_busy = RT_FALSE;

    g_rs485_stats.tx_bytes += (uint32_t)size;
    g_rs485_stats.tx_frames++;

    (void)rs485_app_irq_rx_start();

    return size;
}

rt_size_t rs485_app_read(void *buffer, rt_size_t size)
{
    rt_uint8_t *bytes = (rt_uint8_t *)buffer;
    rt_size_t count = 0U;

    if ((bytes == RT_NULL) || (size == 0U) || (rs485_app_ensure_open() != RT_EOK))
    {
        return 0U;
    }

    if ((g_rs485_irq_rx_enabled == RT_TRUE) ||
        (g_rs485_service_running == RT_TRUE) ||
        (g_rs485_timer_rx_running == RT_TRUE) ||
        (rs485_app_ring_count() > 0U))
    {
        return rs485_app_ring_read(buffer, size);
    }

    while (count < size)
    {
        if (rs485_app_softuart_rx_byte(&bytes[count], 1U) == 0)
        {
            break;
        }
        count++;
    }

    return count;
}

int rs485_app_start_rx_window(uint32_t timeout_ms)
{
    RT_UNUSED(timeout_ms);

    return rs485_app_irq_rx_start();
}

static void rs485_app_service_entry(void *parameter)
{
    uint32_t timeout_ms;

    RT_UNUSED(parameter);

    while (g_rs485_service_running == RT_TRUE)
    {
        if (rs485_app_ensure_open() == RT_EOK)
        {
            g_rs485_stats.rx_poll_rounds++;
            timeout_ms = (g_rs485_service_poll_ms == 0U) ? RS485_APP_TIMER_RX_DEFAULT_MS : g_rs485_service_poll_ms;
            (void)rs485_app_irq_rx_start();
            rt_thread_mdelay(timeout_ms);
        }

        if (g_rs485_service_sleep_ms > 0U)
        {
            rt_thread_mdelay(g_rs485_service_sleep_ms);
        }
        else
        {
            rt_thread_yield();
        }
    }

    rs485_app_timer_rx_stop();
    s_rs485_service_thread = RT_NULL;
}

int rs485_app_service_start(uint32_t poll_ms, uint32_t sleep_ms)
{
    rt_thread_t thread;

    if (rs485_app_ensure_open() != RT_EOK)
    {
        return -1;
    }

    g_rs485_service_poll_ms = (poll_ms == 0U) ? RS485_APP_SERVICE_DEFAULT_POLL_MS : poll_ms;
    g_rs485_service_sleep_ms = sleep_ms;

    /* 已在运行，或刚请求停止的旧线程尚未退出：只更新参数，避免同时存在两个服务线程 */
    if ((g_rs485_service_running == RT_TRUE) || (s_rs485_service_thread != RT_NULL))
    {
        g_rs485_service_running = RT_TRUE;
        return 0;
    }

    g_rs485_service_running = RT_TRUE;

    thread = rt_thread_create("rs485svc",
                              rs485_app_service_entry,
                              RT_NULL,
                              RS485_APP_SERVICE_STACK_SIZE,
                              RS485_APP_SERVICE_PRIORITY,
                              RS485_APP_SERVICE_TIMESLICE);
    if (thread == RT_NULL)
    {
        g_rs485_service_running = RT_FALSE;
        return -1;
    }

    s_rs485_service_thread = thread;
    rt_thread_startup(thread);
    return 0;
}

void rs485_app_service_stop(void)
{
    g_rs485_service_running = RT_FALSE;
}

static void rs485_app_forward_print(const rt_uint8_t *buffer, rt_size_t size)
{
    rt_size_t i;

    rt_kprintf("[rs485] fwd %u byte(s): |", (unsigned int)size);
    for (i = 0U; i < size; i++)
    {
        rt_uint8_t ch = buffer[i];
        rt_kprintf("%c", ((ch >= 0x20U) && (ch < 0x7FU)) ? ch : '.');
    }
    rt_kprintf("|\n");
}

static void rs485_app_forward_entry(void *parameter)
{
    rt_uint8_t buffer[RS485_APP_READ_CHUNK_SIZE];

    RT_UNUSED(parameter);

    while (g_rs485_forward_running == RT_TRUE)
    {
        rt_size_t got = rs485_app_read(buffer, sizeof(buffer));
        if (got > 0U)
        {
            rs485_app_forward_print(buffer, got);
            rt_thread_yield();
        }
        else
        {
            rt_thread_mdelay(RS485_APP_FORWARD_INTERVAL_MS);
        }
    }

    rs485_app_timer_rx_stop();
    s_rs485_forward_thread = RT_NULL;
}

int rs485_app_forward_start(void)
{
    rt_thread_t thread;

    if (rs485_app_ensure_open() != RT_EOK)
    {
        return -1;
    }

    if (rs485_app_service_start(RS485_APP_SERVICE_DEFAULT_POLL_MS,
                                RS485_APP_SERVICE_DEFAULT_SLEEP_MS) != 0)
    {
        return -1;
    }

    if (s_rs485_forward_thread != RT_NULL)
    {
        g_rs485_forward_running = RT_TRUE;
        return 0;
    }

    g_rs485_forward_running = RT_TRUE;
    thread = rt_thread_create("rs485fwd",
                              rs485_app_forward_entry,
                              RT_NULL,
                              RS485_APP_FORWARD_STACK_SIZE,
                              RS485_APP_FORWARD_PRIORITY,
                              RS485_APP_FORWARD_TIMESLICE);
    if (thread == RT_NULL)
    {
        g_rs485_forward_running = RT_FALSE;
        return -1;
    }

    s_rs485_forward_thread = thread;
    rt_thread_startup(thread);
    return 0;
}

void rs485_app_forward_stop(void)
{
    g_rs485_forward_running = RT_FALSE;
}

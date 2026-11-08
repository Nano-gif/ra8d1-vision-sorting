/*
 * 软件 RS485 msh 调试命令：打开/收发/统计/清零/转发监视/接收路径启停。
 */
#include "rs485_internal.h"

#include "bsp_pin_cfg.h"
#include "hal_data.h"

#include <rtdevice.h>
#include <rthw.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void rs485_app_print_block(const rt_uint8_t *buffer, rt_size_t size)
{
    rt_size_t i;

    rt_kprintf("[rs485] rx %u byte(s):", (unsigned int)size);
    for (i = 0U; i < size; i++)
    {
        rt_kprintf(" %02x", (unsigned int)buffer[i]);
    }

    rt_kprintf(" |");
    for (i = 0U; i < size; i++)
    {
        rt_uint8_t ch = buffer[i];
        rt_kprintf("%c", ((ch >= 0x20U) && (ch < 0x7FU)) ? ch : '.');
    }
    rt_kprintf("|\n");
}

static int rs485_open_cmd(int argc, char **argv)
{
    const char *baud_arg = (argc >= 2) ? argv[1] : RS485_APP_DEFAULT_DEVICE;
    int ret = rs485_app_open(baud_arg);

    if (ret != RT_EOK)
    {
        rt_kprintf("[rs485] open soft485 failed: %d\n", ret);
        return -1;
    }

    rt_kprintf("[rs485] opened soft485 gpio uart\n");
    rt_kprintf("[rs485] TX=P801 RX=P009(IRQ13-DS) DE=P007 baud=%u 8N1\n", (unsigned int)g_rs485_baud);
    rt_kprintf("[rs485] tx/rx timing=DWT bit_cycles=%u, irq_rx=%s, legacy GPT5 scan=%u counts\n",
               (unsigned int)g_rs485_bit_cycles,
               (g_rs485_irq_rx_enabled == RT_TRUE) ? "on" : "off",
               (unsigned int)g_rs485_gpt_scan_counts);
    rt_kprintf("[rs485] SCI2 is reserved for W5500 hardware SPI, no uart2 used\n");
    return 0;
}

static int rs485_send_cmd(int argc, char **argv)
{
    const char *text;
    rt_size_t size;
    rt_size_t sent;

    if (argc < 2)
    {
        rt_kprintf("[rs485] usage: rs485_send <text>\n");
        return -1;
    }

    text = argv[1];
    size = (rt_size_t)strlen(text);
    sent = rs485_app_send(text, size);

    rt_kprintf("[rs485] tx dev=%s len=%u sent=%u\n",
               rs485_app_device_name(),
               (unsigned int)size,
               (unsigned int)sent);

    return (sent == size) ? 0 : -1;
}

static int rs485_read_cmd(int argc, char **argv)
{
    uint32_t timeout_ms = RS485_APP_DEFAULT_TIMEOUT_MS;
    rt_tick_t start_tick;
    rt_size_t total = 0U;
    rt_size_t block_size = 0U;
    rt_uint8_t buffer[RS485_APP_READ_CHUNK_SIZE];

    if (argc >= 2)
    {
        timeout_ms = rs485_app_parse_u32(argv[1], RS485_APP_DEFAULT_TIMEOUT_MS);
    }

    if (rs485_app_ensure_open() != RT_EOK)
    {
        rt_kprintf("[rs485] open soft485 failed\n");
        return -1;
    }

    if ((g_rs485_service_running == RT_TRUE) || (g_rs485_irq_rx_enabled == RT_TRUE))
    {
        start_tick = rt_tick_get();
        while ((timeout_ms == 0U) ||
               (rs485_app_ticks_to_ms(rt_tick_get() - start_tick) < timeout_ms))
        {
            block_size = rs485_app_read(buffer, sizeof(buffer));
            if (block_size > 0U)
            {
                rs485_app_print_block(buffer, block_size);
                total += block_size;
            }
            else
            {
                rt_thread_mdelay(1);
            }

            if (timeout_ms == 0U)
            {
                break;
            }
        }

        rt_kprintf("[rs485] read ring done dev=%s total=%u timeout=%u ms\n",
                   rs485_app_device_name(),
                   (unsigned int)total,
                   (unsigned int)timeout_ms);
        return 0;
    }

    start_tick = rt_tick_get();
    while ((timeout_ms == 0U) ||
           (rs485_app_ticks_to_ms(rt_tick_get() - start_tick) < timeout_ms))
    {
        rt_uint8_t byte;

        if (rs485_app_softuart_rx_byte(&byte, 1U) != 0)
        {
            buffer[block_size++] = byte;
            total++;

            if (block_size >= sizeof(buffer))
            {
                rs485_app_print_block(buffer, block_size);
                block_size = 0U;
            }
        }
        else if (block_size > 0U)
        {
            rs485_app_print_block(buffer, block_size);
            block_size = 0U;
        }

        if (timeout_ms == 0U)
        {
            break;
        }
    }

    if (block_size > 0U)
    {
        rs485_app_print_block(buffer, block_size);
    }

    rt_kprintf("[rs485] read done dev=%s total=%u timeout=%u ms\n",
               rs485_app_device_name(),
               (unsigned int)total,
               (unsigned int)timeout_ms);

    return 0;
}

static int rs485_service_start_cmd(int argc, char **argv)
{
    uint32_t poll_ms = RS485_APP_SERVICE_DEFAULT_POLL_MS;
    uint32_t sleep_ms = RS485_APP_SERVICE_DEFAULT_SLEEP_MS;

    if (argc >= 2)
    {
        poll_ms = rs485_app_parse_u32(argv[1], RS485_APP_SERVICE_DEFAULT_POLL_MS);
    }
    if (argc >= 3)
    {
        sleep_ms = rs485_app_parse_u32(argv[2], RS485_APP_SERVICE_DEFAULT_SLEEP_MS);
    }

    if (rs485_app_service_start(poll_ms, sleep_ms) != 0)
    {
        rt_kprintf("[rs485] service start failed\n");
        return -1;
    }

    rt_kprintf("[rs485] service running poll=%u ms sleep=%u ms priority=%u ring=%u\n",
               (unsigned int)g_rs485_service_poll_ms,
               (unsigned int)g_rs485_service_sleep_ms,
               (unsigned int)RS485_APP_SERVICE_PRIORITY,
               (unsigned int)RS485_APP_RX_RING_SIZE);
    return 0;
}

static int rs485_service_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rs485_app_service_stop();
    rt_kprintf("[rs485] service stopping\n");
    return 0;
}

static int rs485_stat_cmd(int argc, char **argv)
{
    rs485_app_stats_t stats;
    rt_uint16_t ring_count;
    rt_base_t level;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    level = rt_hw_interrupt_disable();
    stats = g_rs485_stats;
    ring_count = g_rs485_rx_count;
    rt_hw_interrupt_enable(level);

    rt_kprintf("[rs485] service=%s poll=%u sleep=%u ring=%u/%u\n",
               (g_rs485_service_running == RT_TRUE) ? "on" : "off",
               (unsigned int)g_rs485_service_poll_ms,
               (unsigned int)g_rs485_service_sleep_ms,
               (unsigned int)ring_count,
               (unsigned int)RS485_APP_RX_RING_SIZE);
    rt_kprintf("[rs485] forward=%s priority=%u interval=%u ms\n",
               (g_rs485_forward_running == RT_TRUE) ? "on" : "off",
               (unsigned int)RS485_APP_FORWARD_PRIORITY,
               (unsigned int)RS485_APP_FORWARD_INTERVAL_MS);
    rt_kprintf("[rs485] irq_rx=%s opened=%s busy=%s edges=%u bytes=%u ignored=%u bursts=%u max=%u cfg=%u\n",
               (g_rs485_irq_rx_enabled == RT_TRUE) ? "on" : "off",
               (g_rs485_irq_opened == RT_TRUE) ? "yes" : "no",
               (g_rs485_irq_rx_busy == RT_TRUE) ? "yes" : "no",
               (unsigned int)stats.rx_irq_edges,
               (unsigned int)stats.rx_irq_bytes,
               (unsigned int)stats.rx_irq_ignored,
               (unsigned int)stats.rx_irq_bursts,
               (unsigned int)stats.rx_irq_max_burst,
               (unsigned int)g_rs485_irq_rx_max_burst);
    rt_kprintf("[rs485] rx_bytes=%u rx_errors=%u rx_overflows=%u poll_rounds=%u\n",
               (unsigned int)stats.rx_bytes,
               (unsigned int)stats.rx_errors,
               (unsigned int)stats.rx_overflows,
               (unsigned int)stats.rx_poll_rounds);
    rt_kprintf("[rs485] timer_rx=%s in_byte=%s windows=%u timeouts=%u edges=%u irq_rounds=%u\n",
               (g_rs485_timer_rx_running == RT_TRUE) ? "on" : "off",
               (g_rs485_timer_rx_in_byte == RT_TRUE) ? "yes" : "no",
               (unsigned int)stats.rx_timer_windows,
               (unsigned int)stats.rx_timer_timeouts,
               (unsigned int)stats.rx_timer_edges,
               (unsigned int)g_rs485_timer_irq_rounds);
    rt_kprintf("[rs485] tx_bytes=%u tx_frames=%u\n",
               (unsigned int)stats.tx_bytes,
               (unsigned int)stats.tx_frames);
    return 0;
}

static int rs485_clear_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rs485_app_stats_clear();
    rt_kprintf("[rs485] stats and rx ring cleared\n");
    return 0;
}

static int rs485_dump_cmd(int argc, char **argv)
{
    rt_uint8_t buffer[RS485_APP_READ_CHUNK_SIZE];
    rt_size_t size = sizeof(buffer);
    rt_size_t got;

    if (argc >= 2)
    {
        size = (rt_size_t)rs485_app_parse_u32(argv[1], sizeof(buffer));
        if (size > sizeof(buffer))
        {
            size = sizeof(buffer);
        }
    }

    got = rs485_app_read(buffer, size);
    if (got > 0U)
    {
        rs485_app_print_block(buffer, got);
    }
    else
    {
        rt_kprintf("[rs485] dump empty\n");
    }

    return 0;
}

static int rs485_forward_start_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (rs485_app_forward_start() != 0)
    {
        rt_kprintf("[rs485] forward start failed\n");
        return -1;
    }

    rt_kprintf("[rs485] forward running, service poll=%u ms sleep=%u ms\n",
               (unsigned int)g_rs485_service_poll_ms,
               (unsigned int)g_rs485_service_sleep_ms);
    return 0;
}

static int rs485_forward_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rs485_app_forward_stop();
    rt_kprintf("[rs485] forward stopping\n");
    return 0;
}

static int rs485_timer_rx_start_cmd(int argc, char **argv)
{
    uint32_t timeout_ms = RS485_APP_TIMER_RX_DEFAULT_MS;

    if (argc >= 2)
    {
        timeout_ms = rs485_app_parse_u32(argv[1], RS485_APP_TIMER_RX_DEFAULT_MS);
    }

    if (rs485_app_timer_rx_start(timeout_ms) != RT_EOK)
    {
        rt_kprintf("[rs485] timer rx start failed\n");
        return -1;
    }

    rt_kprintf("[rs485] timer rx window started timeout=%u ms scan=%u counts bit_ticks_q8=%u first_ticks=%u\n",
               (unsigned int)timeout_ms,
               (unsigned int)g_rs485_gpt_scan_counts,
               (unsigned int)g_rs485_timer_bit_ticks_q8,
               (unsigned int)g_rs485_timer_first_sample_ticks);
    return 0;
}

static int rs485_timer_rx_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rs485_app_timer_rx_stop();
    rt_kprintf("[rs485] timer rx stopped ring=%u/%u\n",
               (unsigned int)rs485_app_ring_count(),
               (unsigned int)RS485_APP_RX_RING_SIZE);
    return 0;
}

static int rs485_irq_rx_start_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (rs485_app_ensure_open() != RT_EOK)
    {
        rt_kprintf("[rs485] irq rx start failed\n");
        return -1;
    }

    if (rs485_app_irq_rx_start() != RT_EOK)
    {
        rt_kprintf("[rs485] irq rx start failed\n");
        return -1;
    }

    rt_kprintf("[rs485] irq rx started on P009 IRQ13-DS\n");
    return 0;
}

static int rs485_irq_rx_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rs485_app_irq_rx_stop();
    rt_kprintf("[rs485] irq rx stopped ring=%u/%u\n",
               (unsigned int)rs485_app_ring_count(),
               (unsigned int)RS485_APP_RX_RING_SIZE);
    return 0;
}

MSH_CMD_EXPORT_ALIAS(rs485_open_cmd, rs485_open, open software RS485 UART);
MSH_CMD_EXPORT_ALIAS(rs485_send_cmd, rs485_send, send text through software RS485 UART);
MSH_CMD_EXPORT_ALIAS(rs485_read_cmd, rs485_read, read bytes from software RS485 UART);
MSH_CMD_EXPORT_ALIAS(rs485_service_start_cmd, rs485_service_start, start RS485 background service);
MSH_CMD_EXPORT_ALIAS(rs485_service_stop_cmd, rs485_service_stop, stop RS485 background service);
MSH_CMD_EXPORT_ALIAS(rs485_stat_cmd, rs485_stat, show RS485 service statistics);
MSH_CMD_EXPORT_ALIAS(rs485_clear_cmd, rs485_clear, clear RS485 service statistics);
MSH_CMD_EXPORT_ALIAS(rs485_dump_cmd, rs485_dump, dump RS485 rx ring bytes);
MSH_CMD_EXPORT_ALIAS(rs485_forward_start_cmd, rs485_forward_start, start RS485 forward monitor);
MSH_CMD_EXPORT_ALIAS(rs485_forward_stop_cmd, rs485_forward_stop, stop RS485 forward monitor);
MSH_CMD_EXPORT_ALIAS(rs485_timer_rx_start_cmd, rs485_timer_rx_start, start RS485 timer rx window);
MSH_CMD_EXPORT_ALIAS(rs485_timer_rx_stop_cmd, rs485_timer_rx_stop, stop RS485 timer rx window);
MSH_CMD_EXPORT_ALIAS(rs485_irq_rx_start_cmd, rs485_irq_rx_start, start RS485 IRQ13 rx);
MSH_CMD_EXPORT_ALIAS(rs485_irq_rx_stop_cmd, rs485_irq_rx_stop, stop RS485 IRQ13 rx);

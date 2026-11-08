#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdlib.h>

#include "bsp_pin_cfg.h"
#include "hal_data.h"
#include "w5500_hwspi_app.h"
#include "modbus_rtu_app.h"

#ifndef W5500_CS
#define W5500_CS                 BSP_IO_PORT_10_PIN_05
#endif

#ifndef W5500_RST
#define W5500_RST                BSP_IO_PORT_00_PIN_11
#endif

#ifndef USER_S2
#define USER_S2                  BSP_IO_PORT_00_PIN_08
#endif

#define W5500_HWSPI_CS_PIN       W5500_CS
#define W5500_HWSPI_RST_PIN      W5500_RST
#define W5500_HWSPI_INT_PIN      USER_S2

#define W5500_VERSIONR_ADDR      0x0039U
#define W5500_EXPECTED_VERSION   0x04U
#define W5500_READ_COMMON_VDM    0x00U
#define W5500_HWSPI_TIMEOUT_MS   200U
#define W5500_HWSPI_WAIT_STEP_US 1U
#define W5500_HWSPI_BURST_CHUNK  4096U
/*
 * 仅在“停车专属关键区”内让路：传输前若 modbus 正处于关键命令(停车)期间，
 * 短暂 mdelay 让出 CPU，使本次 SPI 完成中断不抖乱软件 UART 接收停车回执。
 * 平时（状态轮询等）不触发，故网口不受影响。设上限防卡死。
 */
#define W5500_HWSPI_CRITICAL_YIELD_MAX_MS 80U

static volatile rt_bool_t s_w5500_hwspi_done = RT_FALSE;
static volatile spi_event_t s_w5500_hwspi_event = SPI_EVENT_TRANSFER_ABORTED;
static rt_bool_t s_w5500_hwspi_gpio_ready = RT_FALSE;
static rt_bool_t s_w5500_hwspi_opened = RT_FALSE;
static rt_uint8_t s_w5500_hwspi_zero_tx[W5500_HWSPI_BURST_CHUNK];
static rt_uint8_t s_w5500_hwspi_scratch_rx[W5500_HWSPI_BURST_CHUNK];
static rt_uint8_t s_w5500_hwspi_bench_tx[W5500_HWSPI_BURST_CHUNK];
static rt_uint8_t s_w5500_hwspi_bench_rx[W5500_HWSPI_BURST_CHUNK];

void sci2_b_spi_callback(spi_callback_args_t *p_args)
{
    if (p_args == RT_NULL)
    {
        return;
    }

    s_w5500_hwspi_event = p_args->event;
    s_w5500_hwspi_done = RT_TRUE;
}

static void w5500_hwspi_gpio_init(void)
{
    if (s_w5500_hwspi_gpio_ready == RT_TRUE)
    {
        return;
    }

    rt_pin_mode(W5500_HWSPI_CS_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(W5500_HWSPI_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(W5500_HWSPI_INT_PIN, PIN_MODE_INPUT_PULLUP);

    rt_pin_write(W5500_HWSPI_CS_PIN, PIN_HIGH);
    rt_pin_write(W5500_HWSPI_RST_PIN, PIN_HIGH);

    s_w5500_hwspi_gpio_ready = RT_TRUE;
}

void w5500_hwspi_cs_select(void)
{
    rt_pin_write(W5500_HWSPI_CS_PIN, PIN_LOW);
}

void w5500_hwspi_cs_deselect(void)
{
    rt_pin_write(W5500_HWSPI_CS_PIN, PIN_HIGH);
}

void w5500_hwspi_reset(void)
{
    w5500_hwspi_gpio_init();
    w5500_hwspi_cs_deselect();

    rt_pin_write(W5500_HWSPI_RST_PIN, PIN_LOW);
    rt_thread_mdelay(2);
    rt_pin_write(W5500_HWSPI_RST_PIN, PIN_HIGH);
    rt_thread_mdelay(50);
}

int w5500_hwspi_open(void)
{
    fsp_err_t err;

    w5500_hwspi_gpio_init();
    if (s_w5500_hwspi_opened == RT_TRUE)
    {
        return 0;
    }

    err = g_sci_spi2.p_api->open(g_sci_spi2.p_ctrl, g_sci_spi2.p_cfg);
    if (err != FSP_SUCCESS)
    {
        rt_kprintf("[w5500_hw] open g_sci_spi2 failed: %d\n", (int)err);
        return -1;
    }

    s_w5500_hwspi_opened = RT_TRUE;
    return 0;
}

/*
 * 仅在停车关键区内让路（线程上下文、非中断）。平时直接返回，不影响网口性能。
 */
static void w5500_hwspi_yield_if_modbus_critical(void)
{
    rt_tick_t wait_start;
    rt_tick_t wait_cap;

    if ((rt_interrupt_get_nest() != 0U) || (rt_thread_self() == RT_NULL))
    {
        return; /* 中断/无调度上下文不可睡眠 */
    }
    if (modbus_rtu_is_critical() == 0)
    {
        return; /* 非停车关键区：直接放行，网口零影响 */
    }

    wait_start = rt_tick_get();
    wait_cap = rt_tick_from_millisecond(W5500_HWSPI_CRITICAL_YIELD_MAX_MS);
    while (modbus_rtu_is_critical() != 0)
    {
        if ((rt_tick_get() - wait_start) > wait_cap)
        {
            break; /* 上限保护：关键区异常长时也不无限等待 */
        }
        rt_thread_mdelay(1);
    }
}

int w5500_hwspi_transfer(const rt_uint8_t *tx, rt_uint8_t *rx, rt_uint32_t length)
{
    fsp_err_t err;
    rt_tick_t start;
    rt_tick_t timeout;

    if ((tx == RT_NULL) || (rx == RT_NULL) || (length == 0U))
    {
        return -1;
    }

    w5500_hwspi_yield_if_modbus_critical();

    if (w5500_hwspi_open() != 0)
    {
        return -1;
    }

    s_w5500_hwspi_done = RT_FALSE;
    s_w5500_hwspi_event = SPI_EVENT_TRANSFER_ABORTED;

    err = g_sci_spi2.p_api->writeRead(g_sci_spi2.p_ctrl,
                                      tx,
                                      rx,
                                      length,
                                      SPI_BIT_WIDTH_8_BITS);
    if (err != FSP_SUCCESS)
    {
        rt_kprintf("[w5500_hw] writeRead failed: %d\n", (int)err);
        return -1;
    }

    start = rt_tick_get();
    timeout = rt_tick_from_millisecond(W5500_HWSPI_TIMEOUT_MS);
    while (s_w5500_hwspi_done != RT_TRUE)
    {
        if ((rt_tick_get() - start) > timeout)
        {
            rt_kprintf("[w5500_hw] transfer timeout\n");
            return -1;
        }
        rt_hw_us_delay(W5500_HWSPI_WAIT_STEP_US);
    }

    if (s_w5500_hwspi_event != SPI_EVENT_TRANSFER_COMPLETE)
    {
        rt_kprintf("[w5500_hw] transfer event error: %d\n", (int)s_w5500_hwspi_event);
        return -1;
    }

    return 0;
}

rt_uint8_t w5500_hwspi_transfer_byte(rt_uint8_t value)
{
    rt_uint8_t rx = 0xFFU;

    (void)w5500_hwspi_transfer(&value, &rx, 1U);
    return rx;
}

void w5500_hwspi_read_burst(rt_uint8_t *buf, rt_uint16_t len)
{
    rt_uint32_t offset = 0U;

    if ((buf == RT_NULL) || (len == 0U))
    {
        return;
    }

    while (offset < (rt_uint32_t)len)
    {
        rt_uint32_t chunk = ((rt_uint32_t)len - offset);
        if (chunk > W5500_HWSPI_BURST_CHUNK)
        {
            chunk = W5500_HWSPI_BURST_CHUNK;
        }

        if (w5500_hwspi_transfer(s_w5500_hwspi_zero_tx, &buf[offset], chunk) != 0)
        {
            return;
        }

        offset += chunk;
    }
}

void w5500_hwspi_write_burst(rt_uint8_t *buf, rt_uint16_t len)
{
    rt_uint32_t offset = 0U;

    if ((buf == RT_NULL) || (len == 0U))
    {
        return;
    }

    while (offset < (rt_uint32_t)len)
    {
        rt_uint32_t chunk = ((rt_uint32_t)len - offset);
        if (chunk > W5500_HWSPI_BURST_CHUNK)
        {
            chunk = W5500_HWSPI_BURST_CHUNK;
        }

        if (w5500_hwspi_transfer(&buf[offset], s_w5500_hwspi_scratch_rx, chunk) != 0)
        {
            return;
        }

        offset += chunk;
    }
}

int w5500_hwspi_int_level(void)
{
    w5500_hwspi_gpio_init();
    return rt_pin_read(W5500_HWSPI_INT_PIN);
}

static rt_uint8_t w5500_hwspi_read_reg(rt_uint16_t addr)
{
    rt_uint8_t tx[4];
    rt_uint8_t rx[4] = {0};

    if (w5500_hwspi_open() != 0)
    {
        return 0xFFU;
    }

    tx[0] = (rt_uint8_t)(addr >> 8);
    tx[1] = (rt_uint8_t)(addr & 0xFFU);
    tx[2] = W5500_READ_COMMON_VDM;
    tx[3] = 0x00U;

    w5500_hwspi_cs_select();
    if (w5500_hwspi_transfer(tx, rx, sizeof(tx)) != 0)
    {
        w5500_hwspi_cs_deselect();
        return 0xFFU;
    }
    w5500_hwspi_cs_deselect();

    return rx[3];
}

static int w5500_hw_probe_cmd(int argc, char **argv)
{
    const sci_b_spi_extended_cfg_t *extend;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    w5500_hwspi_gpio_init();
    extend = (const sci_b_spi_extended_cfg_t *)g_sci_spi2.p_cfg->p_extend;

    rt_kprintf("[w5500_hw] SCI2 hardware SPI mode0, CS GPIO controlled\n");
    rt_kprintf("[w5500_hw] MISO=PA02 MOSI=PA03 SCK=PA04 CS=PA05 RST=P011 INT=P008\n");
    rt_kprintf("[w5500_hw] CS/RST output high, INT input pull-up, INT level=%d\n",
               rt_pin_read(W5500_HWSPI_INT_PIN));
    rt_kprintf("[w5500_hw] channel=%u cks=%u brr=%u DTC tx=%s rx=%s\n",
               (unsigned int)g_sci_spi2.p_cfg->channel,
               (unsigned int)extend->clk_div.cks,
               (unsigned int)extend->clk_div.brr,
               (g_sci_spi2.p_cfg->p_transfer_tx != RT_NULL) ? "on" : "off",
               (g_sci_spi2.p_cfg->p_transfer_rx != RT_NULL) ? "on" : "off");

    return 0;
}

static int w5500_hw_ver_cmd(int argc, char **argv)
{
    rt_uint8_t version;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (w5500_hwspi_open() != 0)
    {
        return -1;
    }

    w5500_hwspi_reset();
    version = w5500_hwspi_read_reg(W5500_VERSIONR_ADDR);

    rt_kprintf("[w5500_hw] VERSIONR=0x%02x %s\n",
               (unsigned int)version,
               (version == W5500_EXPECTED_VERSION) ? "ok" : "unexpected");

    return (version == W5500_EXPECTED_VERSION) ? 0 : -1;
}

static int w5500_hw_bench_cmd(int argc, char **argv)
{
    rt_uint32_t loops = 1000U;
    rt_tick_t start;
    rt_tick_t elapsed;
    rt_uint32_t elapsed_ms;
    rt_uint32_t i;
    rt_uint32_t ok_count = 0U;

    if (argc >= 2)
    {
        loops = (rt_uint32_t)atoi(argv[1]);
        if (loops == 0U)
        {
            loops = 1000U;
        }
    }

    if (w5500_hwspi_open() != 0)
    {
        return -1;
    }

    w5500_hwspi_reset();

    start = rt_tick_get();
    for (i = 0U; i < loops; i++)
    {
        if (w5500_hwspi_read_reg(W5500_VERSIONR_ADDR) == W5500_EXPECTED_VERSION)
        {
            ok_count++;
        }
    }
    elapsed = rt_tick_get() - start;
    elapsed_ms = (rt_uint32_t)((elapsed * 1000U) / RT_TICK_PER_SECOND);
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    rt_kprintf("[w5500_hw] bench loops=%u ok=%u elapsed=%u ms approx=%u byte/s\n",
               (unsigned int)loops,
               (unsigned int)ok_count,
               (unsigned int)elapsed_ms,
               (unsigned int)((loops * 4U * 1000U) / elapsed_ms));

    return (ok_count == loops) ? 0 : -1;
}

static int w5500_hw_burst_bench_cmd(int argc, char **argv)
{
    rt_uint32_t total_bytes = 1024U;
    rt_uint32_t loops = 100U;
    rt_tick_t start;
    rt_tick_t elapsed;
    rt_uint32_t elapsed_ms;
    rt_uint32_t i;
    rt_uint32_t ok_count = 0U;
    rt_uint32_t remaining;
    rt_uint32_t chunk_bytes;
    rt_uint32_t sent_bytes;

    if (argc >= 2)
    {
        total_bytes = (rt_uint32_t)atoi(argv[1]);
        if (total_bytes == 0U)
        {
            total_bytes = 1024U;
        }
    }

    if (argc >= 3)
    {
        loops = (rt_uint32_t)atoi(argv[2]);
        if (loops == 0U)
        {
            loops = 100U;
        }
    }

    if (w5500_hwspi_open() != 0)
    {
        return -1;
    }

    w5500_hwspi_cs_deselect();

    chunk_bytes = W5500_HWSPI_BURST_CHUNK;
    if (chunk_bytes > total_bytes)
    {
        chunk_bytes = total_bytes;
    }

    for (i = 0U; i < chunk_bytes; i++)
    {
        s_w5500_hwspi_bench_tx[i] = 0xA5U;
    }

    start = rt_tick_get();
    for (i = 0U; i < loops; i++)
    {
        remaining = total_bytes;
        sent_bytes = 0U;
        while (remaining > 0U)
        {
            rt_uint32_t this_chunk = (remaining > chunk_bytes) ? chunk_bytes : remaining;

            if (w5500_hwspi_transfer(s_w5500_hwspi_bench_tx,
                                     s_w5500_hwspi_bench_rx,
                                     this_chunk) != 0)
            {
                break;
            }

            remaining -= this_chunk;
            sent_bytes += this_chunk;
        }

        if (sent_bytes == total_bytes)
        {
            ok_count++;
        }
    }
    elapsed = rt_tick_get() - start;
    elapsed_ms = (rt_uint32_t)((elapsed * 1000U) / RT_TICK_PER_SECOND);
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    rt_kprintf("[w5500_hw] burst bytes=%u loops=%u ok=%u elapsed=%u ms approx=%u byte/s\n",
               (unsigned int)total_bytes,
               (unsigned int)loops,
               (unsigned int)ok_count,
               (unsigned int)elapsed_ms,
               (unsigned int)((total_bytes * loops * 1000U) / elapsed_ms));

    return (ok_count == loops) ? 0 : -1;
}

MSH_CMD_EXPORT_ALIAS(w5500_hw_probe_cmd, w5500_hw_probe, probe W5500 SCI2 hardware SPI pins);
MSH_CMD_EXPORT_ALIAS(w5500_hw_ver_cmd, w5500_hw_ver, read W5500 VERSIONR by SCI2 hardware SPI);
MSH_CMD_EXPORT_ALIAS(w5500_hw_bench_cmd, w5500_hw_bench, benchmark W5500 SCI2 hardware SPI register reads);
MSH_CMD_EXPORT_ALIAS(w5500_hw_burst_bench_cmd, w5500_hw_burst_bench, benchmark W5500 SCI2 hardware SPI burst reads);

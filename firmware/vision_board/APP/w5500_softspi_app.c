#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdlib.h>

#include "bsp_pin_cfg.h"
#include "Components/soft_spi/soft_spi.h"

#ifndef W5500_MISO
#define W5500_MISO               BSP_IO_PORT_10_PIN_02
#endif

#ifndef W5500_MOSI
#define W5500_MOSI               BSP_IO_PORT_10_PIN_03
#endif

#ifndef W5500_SCK
#define W5500_SCK                BSP_IO_PORT_10_PIN_04
#endif

#ifndef W5500_CS
#define W5500_CS                 BSP_IO_PORT_10_PIN_05
#endif

#ifndef W5500_RST
#define W5500_RST                BSP_IO_PORT_00_PIN_11
#endif

#ifndef USER_S2
#define USER_S2                  BSP_IO_PORT_00_PIN_08
#endif

#define W5500_SOFTSPI_MISO_PIN   W5500_MISO  /* RPI-MISO2 -> W5500_MISO */
#define W5500_SOFTSPI_MOSI_PIN   W5500_MOSI  /* RPI-MOSI2 -> W5500_MOSI */
#define W5500_SOFTSPI_SCK_PIN    W5500_SCK   /* RPI-SCK2  -> W5500_SCK */
#define W5500_SOFTSPI_CS_PIN     W5500_CS    /* RPI-CS2   -> W5500_CS */
#define W5500_SOFTSPI_RST_PIN    W5500_RST   /* RPI-P011  -> W5500_RST */
#define W5500_SOFTSPI_INT_PIN    USER_S2     /* RPI-P008  -> W5500_INT */

#define W5500_VERSIONR_ADDR      0x0039U
#define W5500_EXPECTED_VERSION   0x04U
#define W5500_READ_COMMON_VDM    0x00U
#define W5500_SPI_DELAY_US       0U

static rt_bool_t s_w5500_softspi_ready = RT_FALSE;
static const soft_spi_bus_t s_w5500_softspi_bus = {
    .sck_pin = W5500_SOFTSPI_SCK_PIN,
    .mosi_pin = W5500_SOFTSPI_MOSI_PIN,
    .miso_pin = W5500_SOFTSPI_MISO_PIN,
    .cs_pin = W5500_SOFTSPI_CS_PIN,
    .delay_us = W5500_SPI_DELAY_US,
    .mode = SOFT_SPI_MODE_0,
    .bit_order = SOFT_SPI_MSB_FIRST,
    .cs_active = SOFT_SPI_CS_ACTIVE_LOW,
    .tx_fill = 0x00U
};

static void w5500_softspi_init_pins(void)
{
    if (s_w5500_softspi_ready == RT_TRUE)
    {
        return;
    }

    soft_spi_init(&s_w5500_softspi_bus);
    SOFT_SPI_GPIO_MODE_OUTPUT(W5500_SOFTSPI_RST_PIN);
    SOFT_SPI_GPIO_MODE_INPUT_PULLUP(W5500_SOFTSPI_INT_PIN);

    SOFT_SPI_GPIO_WRITE(W5500_SOFTSPI_RST_PIN, SOFT_SPI_PIN_HIGH);

    s_w5500_softspi_ready = RT_TRUE;
}

static void w5500_softspi_reset(void)
{
    w5500_softspi_init_pins();
    soft_spi_deselect(&s_w5500_softspi_bus);
    SOFT_SPI_GPIO_WRITE(W5500_SOFTSPI_RST_PIN, SOFT_SPI_PIN_LOW);
    rt_thread_mdelay(2);
    SOFT_SPI_GPIO_WRITE(W5500_SOFTSPI_RST_PIN, SOFT_SPI_PIN_HIGH);
    rt_thread_mdelay(50);
}

static rt_uint8_t w5500_softspi_read_reg(rt_uint16_t addr)
{
    rt_uint8_t value;
    rt_uint8_t tx_header[3];

    w5500_softspi_init_pins();
    tx_header[0] = (rt_uint8_t)(addr >> 8);
    tx_header[1] = (rt_uint8_t)(addr & 0xFFU);
    tx_header[2] = W5500_READ_COMMON_VDM;

    soft_spi_write_then_read(&s_w5500_softspi_bus, tx_header, sizeof(tx_header), &value, 1U);

    return value;
}

static int w5500_soft_probe_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    w5500_softspi_init_pins();
    rt_kprintf("[w5500_soft] generic software SPI mode0, no SCI2/SPI peripheral used\n");
    rt_kprintf("[w5500_soft] MOSI=PA03 MISO=PA02 SCK=PA04 CS=PA05 RST=P011 INT=P008\n");
    rt_kprintf("[w5500_soft] RASC: MOSI/SCK/CS/RST output, MISO/INT input\n");
    rt_kprintf("[w5500_soft] INT level=%d\n", SOFT_SPI_GPIO_READ(W5500_SOFTSPI_INT_PIN));
    return 0;
}

static int w5500_soft_ver_cmd(int argc, char **argv)
{
    rt_uint8_t version;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    w5500_softspi_reset();
    version = w5500_softspi_read_reg(W5500_VERSIONR_ADDR);

    rt_kprintf("[w5500_soft] VERSIONR=0x%02x %s\n",
               (unsigned int)version,
               (version == W5500_EXPECTED_VERSION) ? "ok" : "unexpected");

    if (version != W5500_EXPECTED_VERSION)
    {
        rt_kprintf("[w5500_soft] expected 0x04, check power/pins/RASC GPIO direction/wiring\n");
        return -1;
    }

    return 0;
}

static int w5500_soft_bench_cmd(int argc, char **argv)
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

    w5500_softspi_reset();

    start = rt_tick_get();
    for (i = 0U; i < loops; i++)
    {
        if (w5500_softspi_read_reg(W5500_VERSIONR_ADDR) == W5500_EXPECTED_VERSION)
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

    /*
     * 每次读取 VERSIONR 传输 4 字节（3 字节命令头 + 1 字节数据），
     * 结果为包含 GPIO 与循环开销在内的软件 SPI 端到端吞吐估算。
     */
    rt_kprintf("[w5500_soft] bench loops=%u ok=%u elapsed=%u ms approx=%u byte/s\n",
               (unsigned int)loops,
               (unsigned int)ok_count,
               (unsigned int)elapsed_ms,
               (unsigned int)((loops * 4U * 1000U) / elapsed_ms));

    return (ok_count == loops) ? 0 : -1;
}

MSH_CMD_EXPORT_ALIAS(w5500_soft_probe_cmd, w5500_soft_probe, probe W5500 software SPI GPIO pins);
MSH_CMD_EXPORT_ALIAS(w5500_soft_ver_cmd, w5500_soft_ver, read W5500 VERSIONR by software SPI);
MSH_CMD_EXPORT_ALIAS(w5500_soft_bench_cmd, w5500_soft_bench, benchmark W5500 software SPI register reads);

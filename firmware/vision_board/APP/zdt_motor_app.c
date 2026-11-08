#include "zdt_motor_app.h"

#include "modbus_rtu_app.h"

#include <rtthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ZDT_REG_SPEED_MODE              0x00F6U
#define ZDT_REG_POSITION_MODE           0x00FDU
#define ZDT_REG_STOP                    0x00FEU
/*
 * 当前真实电机为 X35 + Emm 固件。
 * 控制写命令使用 06H/10H 写 0x00F6/0x00FD/0x00FE；状态读取使用 04H
 * input-register 读取手册中 Emm 侧地址 35/36/3A/3B。
 * 实测该电机对状态读取不接受 03H，不使用 X42S/Y42 括号内地址。
 */
#define ZDT_REG_REALTIME_SPEED          0x0035U
#define ZDT_REG_REALTIME_POSITION       0x0036U
#define ZDT_REG_MOTOR_FLAGS             0x003AU
#define ZDT_REG_HOME_FLAGS              0x003BU
#define ZDT_TIMEOUT_MS                  800U
#define ZDT_FAST_TIMEOUT_MS             250U
#define ZDT_RETRIES                     2U
#define ZDT_READ_GAP_MS                 5U
/*
 * 快速停车（块A）：软件模拟 UART 的接收采样会被 W5500 SPI 完成中断抖乱，
 * 单帧停车的回执常丢失 → 上层误判“没停住”而反复重试、停车变慢。
 * STOP 写寄存器是幂等的（反复写 0x9800 无副作用），故采用“短超时 + 连发多帧”：
 * 抖动下至少一帧能干净落到电机，停车既快又稳。
 */
#define ZDT_STOP_FAST_TIMEOUT_MS        40U
#define ZDT_STOP_FAST_REPEATS           3U
#define ZDT_STOP_FAST_GAP_MS            25U

typedef struct
{
    uint32_t speed_cmds;
    uint32_t position_cmds;
    uint32_t stop_cmds;
    uint32_t read_cmds;
    uint32_t errors;
} zdt_motor_stats_t;

static zdt_motor_stats_t s_zdt_stats;

static uint32_t zdt_parse_u32(const char *text, uint32_t default_value)
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

static zdt_motor_dir_t zdt_parse_dir(const char *text)
{
    if (text == RT_NULL)
    {
        return ZDT_MOTOR_DIR_CW;
    }
    if ((strcmp(text, "ccw") == 0) || (strcmp(text, "CCW") == 0) || (strcmp(text, "1") == 0))
    {
        return ZDT_MOTOR_DIR_CCW;
    }
    return ZDT_MOTOR_DIR_CW;
}

static void zdt_count_error(int ret)
{
    if (ret != RT_EOK)
    {
        s_zdt_stats.errors++;
    }
}

int zdt_motor_stop(uint8_t addr)
{
    int ret = modbus_rtu_write_single_register(addr, ZDT_REG_STOP, 0x9800U,
                                               ZDT_FAST_TIMEOUT_MS, 0U);
    if (ret == RT_EOK)
    {
        s_zdt_stats.stop_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

/*
 * 幂等快速停车：连发 repeats 帧 STOP，每帧只用短超时等回执。
 * - 只要任意一帧收到回执即返回 RT_EOK（确认已停）。
 * - 即便全部回执丢失（抖动），STOP 帧也已多次发出，电机大概率已收到。
 * repeats==0 时按默认次数处理。
 */
int zdt_motor_stop_fast(uint8_t addr, uint8_t repeats)
{
    int last_ret = -RT_ERROR;
    rt_bool_t acked = RT_FALSE;
    uint8_t i;

    if (repeats == 0U)
    {
        repeats = ZDT_STOP_FAST_REPEATS;
    }

    /* 停车专属关键区：期间 W5500 SPI 让路，停车回执不被 SPI 中断抖乱。仅持续这几帧。 */
    modbus_rtu_begin_critical();
    for (i = 0U; i < repeats; i++)
    {
        last_ret = modbus_rtu_write_single_register(addr, ZDT_REG_STOP, 0x9800U,
                                                    ZDT_STOP_FAST_TIMEOUT_MS, 0U);
        s_zdt_stats.stop_cmds++;
        if (last_ret == RT_EOK)
        {
            acked = RT_TRUE;
        }
        if ((i + 1U) < repeats)
        {
            rt_thread_mdelay(ZDT_STOP_FAST_GAP_MS);
        }
    }
    modbus_rtu_end_critical();

    if (acked == RT_TRUE)
    {
        return RT_EOK;
    }
    zdt_count_error(last_ret);
    return last_ret;
}

int zdt_motor_speed(uint8_t addr, zdt_motor_dir_t dir, uint16_t rpm, uint8_t acc, uint8_t sync)
{
    uint16_t values[3];
    int ret;

    if (rpm > 3000U)
    {
        rpm = 3000U;
    }

    values[0] = (uint16_t)(((uint16_t)dir << 8) | acc);
    values[1] = rpm;
    values[2] = (uint16_t)(((uint16_t)(sync ? 1U : 0U) << 8) | 0x00U);

    ret = modbus_rtu_write_multiple_registers(addr, ZDT_REG_SPEED_MODE, values, 3U,
                                              ZDT_FAST_TIMEOUT_MS, 1U);
    if (ret == RT_EOK)
    {
        s_zdt_stats.speed_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

int zdt_motor_position(uint8_t addr, zdt_motor_dir_t dir, uint16_t rpm, uint8_t acc,
                       uint32_t pulses, uint8_t mode, uint8_t sync)
{
    uint16_t values[5];
    int ret;

    if (rpm > 3000U)
    {
        rpm = 3000U;
    }
    if (mode > 2U)
    {
        mode = 0U;
    }

    values[0] = (uint16_t)(((uint16_t)dir << 8) | acc);
    values[1] = rpm;
    values[2] = (uint16_t)((pulses >> 16) & 0xFFFFU);
    values[3] = (uint16_t)(pulses & 0xFFFFU);
    values[4] = (uint16_t)(((uint16_t)mode << 8) | (sync ? 1U : 0U));

    ret = modbus_rtu_write_multiple_registers(addr, ZDT_REG_POSITION_MODE, values, 5U,
                                              ZDT_TIMEOUT_MS, ZDT_RETRIES);
    if (ret == RT_EOK)
    {
        s_zdt_stats.position_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

int zdt_motor_read_speed(uint8_t addr, zdt_motor_status_t *status)
{
    uint16_t regs[2];
    int ret = modbus_rtu_read_input_registers(addr, ZDT_REG_REALTIME_SPEED, 2U, regs,
                                              ZDT_TIMEOUT_MS, ZDT_RETRIES);
    if (ret == RT_EOK)
    {
        int16_t rpm = (int16_t)regs[1];
        if (regs[0] != 0U)
        {
            rpm = (int16_t)(-rpm);
        }
        if (status != RT_NULL)
        {
            status->speed_rpm = rpm;
        }
        s_zdt_stats.read_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

int zdt_motor_read_position(uint8_t addr, zdt_motor_status_t *status)
{
    uint16_t regs[3];
    int ret = modbus_rtu_read_input_registers(addr, ZDT_REG_REALTIME_POSITION, 3U, regs,
                                              ZDT_TIMEOUT_MS, ZDT_RETRIES);
    if (ret == RT_EOK)
    {
        int32_t pos = (int32_t)(((uint32_t)regs[1] << 16) | regs[2]);
        if (regs[0] != 0U)
        {
            pos = -pos;
        }
        if (status != RT_NULL)
        {
            status->position_raw = pos;
        }
        s_zdt_stats.read_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

int zdt_motor_read_flags(uint8_t addr, zdt_motor_status_t *status)
{
    uint16_t home_regs[1];
    uint16_t motor_regs[1];
    int ret = modbus_rtu_read_input_registers(addr, ZDT_REG_HOME_FLAGS, 1U, home_regs,
                                              ZDT_TIMEOUT_MS, ZDT_RETRIES);
    if (ret == RT_EOK)
    {
        ret = modbus_rtu_read_input_registers(addr, ZDT_REG_MOTOR_FLAGS, 1U, motor_regs,
                                              ZDT_TIMEOUT_MS, ZDT_RETRIES);
    }
    if (ret == RT_EOK)
    {
        if (status != RT_NULL)
        {
            status->home_flags = (uint8_t)(home_regs[0] & 0xFFU);
            status->motor_flags = (uint8_t)(motor_regs[0] & 0xFFU);
        }
        s_zdt_stats.read_cmds++;
    }
    zdt_count_error(ret);
    return ret;
}

static void zdt_print_status(const zdt_motor_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    rt_kprintf("[zdt] speed=%d rpm position_raw=%d home_flags=0x%02x motor_flags=0x%02x\n",
               (int)status->speed_rpm,
               (int)status->position_raw,
               (unsigned int)status->home_flags,
               (unsigned int)status->motor_flags);
    rt_kprintf("[zdt] motor flags: enable=%u reached=%u left_limit=%u right_limit=%u alarm=%u\n",
               (unsigned int)((status->motor_flags & 0x01U) != 0U),
               (unsigned int)((status->motor_flags & 0x02U) != 0U),
               (unsigned int)((status->motor_flags & 0x10U) != 0U),
               (unsigned int)((status->motor_flags & 0x20U) != 0U),
               (unsigned int)((status->motor_flags & 0x80U) != 0U));
}

static int zdt_stop_cmd(int argc, char **argv)
{
    uint8_t addr = ZDT_MOTOR_DEFAULT_ADDR;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)zdt_parse_u32(argv[1], ZDT_MOTOR_DEFAULT_ADDR);
    }

    ret = zdt_motor_stop(addr);
    rt_kprintf("[zdt] stop addr=%u ret=%d\n", (unsigned int)addr, ret);
    return ret;
}

static int zdt_speed_cmd(int argc, char **argv)
{
    uint8_t addr = ZDT_MOTOR_DEFAULT_ADDR;
    zdt_motor_dir_t dir;
    uint16_t rpm;
    uint8_t acc;
    int ret;

    if (argc < 4)
    {
        rt_kprintf("[zdt] usage: zdt_speed <addr> <cw|ccw> <rpm> [acc]\n");
        return -1;
    }

    addr = (uint8_t)zdt_parse_u32(argv[1], ZDT_MOTOR_DEFAULT_ADDR);
    dir = zdt_parse_dir(argv[2]);
    rpm = (uint16_t)zdt_parse_u32(argv[3], 60U);
    acc = (argc >= 5) ? (uint8_t)zdt_parse_u32(argv[4], 20U) : 20U;

    ret = zdt_motor_speed(addr, dir, rpm, acc, 0U);
    rt_kprintf("[zdt] speed addr=%u dir=%s rpm=%u acc=%u ret=%d\n",
               (unsigned int)addr,
               (dir == ZDT_MOTOR_DIR_CCW) ? "ccw" : "cw",
               (unsigned int)rpm,
               (unsigned int)acc,
               ret);
    return ret;
}

static int zdt_pos_cmd(int argc, char **argv)
{
    uint8_t addr = ZDT_MOTOR_DEFAULT_ADDR;
    zdt_motor_dir_t dir;
    uint16_t rpm;
    uint8_t acc;
    uint32_t pulses;
    uint8_t mode;
    int ret;

    if (argc < 5)
    {
        rt_kprintf("[zdt] usage: zdt_pos <addr> <cw|ccw> <rpm> <pulses> [mode] [acc]\n");
        return -1;
    }

    addr = (uint8_t)zdt_parse_u32(argv[1], ZDT_MOTOR_DEFAULT_ADDR);
    dir = zdt_parse_dir(argv[2]);
    rpm = (uint16_t)zdt_parse_u32(argv[3], 60U);
    pulses = zdt_parse_u32(argv[4], 0U);
    mode = (argc >= 6) ? (uint8_t)zdt_parse_u32(argv[5], 0U) : 0U;
    acc = (argc >= 7) ? (uint8_t)zdt_parse_u32(argv[6], 20U) : 20U;

    ret = zdt_motor_position(addr, dir, rpm, acc, pulses, mode, 0U);
    rt_kprintf("[zdt] pos addr=%u dir=%s rpm=%u pulses=%u mode=%u acc=%u ret=%d\n",
               (unsigned int)addr,
               (dir == ZDT_MOTOR_DIR_CCW) ? "ccw" : "cw",
               (unsigned int)rpm,
               (unsigned int)pulses,
               (unsigned int)mode,
               (unsigned int)acc,
               ret);
    return ret;
}

static int zdt_read_cmd(int argc, char **argv)
{
    uint8_t addr = ZDT_MOTOR_DEFAULT_ADDR;
    zdt_motor_status_t status;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)zdt_parse_u32(argv[1], ZDT_MOTOR_DEFAULT_ADDR);
    }

    memset(&status, 0, sizeof(status));
    ret = zdt_motor_read_speed(addr, &status);
    if (ret == RT_EOK)
    {
        rt_thread_mdelay(ZDT_READ_GAP_MS);
        ret = zdt_motor_read_position(addr, &status);
    }
    if (ret == RT_EOK)
    {
        rt_thread_mdelay(ZDT_READ_GAP_MS);
        ret = zdt_motor_read_flags(addr, &status);
    }

    if (ret == RT_EOK)
    {
        zdt_print_status(&status);
    }
    else
    {
        rt_kprintf("[zdt] read failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int zdt_stat_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("[zdt] speed_cmds=%u position_cmds=%u stop_cmds=%u read_cmds=%u errors=%u default_addr=%u\n",
               (unsigned int)s_zdt_stats.speed_cmds,
               (unsigned int)s_zdt_stats.position_cmds,
               (unsigned int)s_zdt_stats.stop_cmds,
               (unsigned int)s_zdt_stats.read_cmds,
               (unsigned int)s_zdt_stats.errors,
               (unsigned int)ZDT_MOTOR_DEFAULT_ADDR);
    return 0;
}

static int zdt_clear_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    memset(&s_zdt_stats, 0, sizeof(s_zdt_stats));
    rt_kprintf("[zdt] stats cleared\n");
    return 0;
}

MSH_CMD_EXPORT_ALIAS(zdt_stop_cmd, zdt_stop, stop ZDT motor through ModbusRTU);
MSH_CMD_EXPORT_ALIAS(zdt_speed_cmd, zdt_speed, run ZDT motor speed mode through ModbusRTU);
MSH_CMD_EXPORT_ALIAS(zdt_pos_cmd, zdt_pos, run ZDT motor position mode through ModbusRTU);
MSH_CMD_EXPORT_ALIAS(zdt_read_cmd, zdt_read, read ZDT motor speed position and flags);
MSH_CMD_EXPORT_ALIAS(zdt_stat_cmd, zdt_stat, show ZDT motor statistics);
MSH_CMD_EXPORT_ALIAS(zdt_clear_cmd, zdt_clear, clear ZDT motor statistics);

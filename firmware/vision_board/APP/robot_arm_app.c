#include "robot_arm_app.h"

#include "modbus_rtu_app.h"

#include <rtthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROBOT_ARM_REG_BASE_STATUS       0x0000U
#define ROBOT_ARM_REG_LAST_COMMAND      0x0009U
#define ROBOT_ARM_REG_CURRENT_AXIS      0x0020U
#define ROBOT_ARM_REG_ACTION_STATUS     0x0060U
#define ROBOT_ARM_REG_CONTROL_COMMAND   0x00F0U
#define ROBOT_ARM_REG_TARGET_APPLY      0x00F1U
#define ROBOT_ARM_REG_AXIS_TARGET       0x0100U
#define ROBOT_ARM_REG_RAW_US_TARGET     0x0120U

#define ROBOT_ARM_BASE_STATUS_COUNT     16U
#define ROBOT_ARM_BASE_STATUS_USED      12U
#define ROBOT_ARM_BASE_READ_CHUNK       4U
#define ROBOT_ARM_LAST_COMMAND_COUNT    3U
#define ROBOT_ARM_ACTION_STATUS_COUNT   5U
#define ROBOT_ARM_AXIS_READ_CHUNK       3U
#define ROBOT_ARM_ACTION_READ_HEAD      3U
#define ROBOT_ARM_ACTION_READ_TAIL      2U
#define ROBOT_ARM_TIMEOUT_MS            800U
#define ROBOT_ARM_FAST_TIMEOUT_MS       250U
#define ROBOT_ARM_RETRIES               1U

#define ROBOT_ARM_STATUS_READY          (1U << 0)
#define ROBOT_ARM_STATUS_ENABLED        (1U << 1)
#define ROBOT_ARM_STATUS_MOVING         (1U << 2)
#define ROBOT_ARM_STATUS_HOMED          (1U << 3)
#define ROBOT_ARM_STATUS_HMI_ACTIVE     (1U << 4)
#define ROBOT_ARM_STATUS_REMOTE_ACTIVE  (1U << 5)
#define ROBOT_ARM_STATUS_ERROR          (1U << 6)
#define ROBOT_ARM_STATUS_ESTOP          (1U << 7)
#define ROBOT_ARM_STATUS_RECORDING      (1U << 8)
#define ROBOT_ARM_STATUS_PLAYING        (1U << 9)

typedef struct
{
    uint32_t read_base_cmds;
    uint32_t read_axis_cmds;
    uint32_t read_action_cmds;
    uint32_t control_cmds;
    uint32_t apply_cmds;
    uint32_t errors;
    uint16_t next_seq;
} robot_arm_stats_t;

static robot_arm_stats_t s_robot_arm_stats = {0U, 0U, 0U, 0U, 0U, 0U, 1U};

static uint32_t robot_arm_parse_u32(const char *text, uint32_t default_value)
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

static int32_t robot_arm_parse_i32(const char *text, int32_t default_value)
{
    long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return default_value;
    }

    value = strtol(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return default_value;
    }

    return (int32_t)value;
}

static uint16_t robot_arm_next_seq(void);
static uint16_t robot_arm_parse_seq_arg(int argc, char **argv, int index)
{
    if (argc > index)
    {
        return (uint16_t)robot_arm_parse_u32(argv[index], 0U);
    }

    return robot_arm_next_seq();
}

static int16_t robot_arm_reg_to_i16(uint16_t value)
{
    return (int16_t)value;
}

static uint16_t robot_arm_i16_to_reg(int16_t value)
{
    return (uint16_t)value;
}

static uint16_t robot_arm_next_seq(void)
{
    uint16_t seq = s_robot_arm_stats.next_seq;

    s_robot_arm_stats.next_seq++;
    if (s_robot_arm_stats.next_seq == 0U)
    {
        s_robot_arm_stats.next_seq = 1U;
    }

    return seq;
}

static void robot_arm_count_error(int ret)
{
    if (ret != RT_EOK)
    {
        s_robot_arm_stats.errors++;
    }
}

static const char *robot_arm_result_name(uint16_t result)
{
    switch (result)
    {
        case ROBOT_ARM_RESULT_NONE:
            return "none";
        case ROBOT_ARM_RESULT_ACCEPTED:
            return "accepted";
        case ROBOT_ARM_RESULT_BUSY:
            return "busy";
        case ROBOT_ARM_RESULT_INVALID_PARAM:
            return "invalid_param";
        case ROBOT_ARM_RESULT_ESTOP:
            return "estop";
        case ROBOT_ARM_RESULT_HMI_ACTIVE:
            return "hmi_active";
        case ROBOT_ARM_RESULT_ACTION_EMPTY:
            return "action_empty";
        case ROBOT_ARM_RESULT_FAILED:
            return "failed";
        case ROBOT_ARM_RESULT_UNSUPPORTED:
            return "unsupported";
        case ROBOT_ARM_RESULT_DUPLICATE:
            return "duplicate";
        default:
            return "unknown";
    }
}

static const char *robot_arm_mode_name(uint16_t mode)
{
    switch (mode)
    {
        case 0U:
            return "idle";
        case 1U:
            return "manual";
        case 2U:
            return "record";
        case 3U:
            return "playback";
        case 4U:
            return "estop";
        default:
            return "unknown";
    }
}

static const char *robot_arm_action_state_name(uint16_t state)
{
    switch (state)
    {
        case 0U:
            return "ready";
        case 1U:
            return "record";
        case 2U:
            return "playing";
        case 3U:
            return "stopped";
        case 4U:
            return "done";
        case 5U:
            return "error";
        default:
            return "unknown";
    }
}

static void robot_arm_print_status_flags(uint16_t flags)
{
    rt_kprintf("[arm] flags: ready=%u enabled=%u moving=%u homed=%u hmi=%u remote=%u error=%u estop=%u recording=%u playing=%u\n",
               (unsigned int)((flags & ROBOT_ARM_STATUS_READY) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_ENABLED) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_MOVING) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_HOMED) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_HMI_ACTIVE) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_REMOTE_ACTIVE) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_ERROR) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_ESTOP) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_RECORDING) != 0U),
               (unsigned int)((flags & ROBOT_ARM_STATUS_PLAYING) != 0U));
}

static void robot_arm_print_base_status(const robot_arm_base_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    rt_kprintf("[arm] id=0x%04x fw=%u proto=%u servos=%u heartbeat=%u comm_err=%u mode=%u(%s)\n",
               (unsigned int)status->device_id,
               (unsigned int)status->firmware_version,
               (unsigned int)status->protocol_version,
               (unsigned int)status->servo_count,
               (unsigned int)status->heartbeat_counter,
               (unsigned int)status->comm_error_count,
               (unsigned int)status->control_mode,
               robot_arm_mode_name(status->control_mode));
    rt_kprintf("[arm] status_flags=0x%04x error_code=0x%04x last_seq=%u last_code=0x%04x last_result=%u(%s)\n",
               (unsigned int)status->status_flags,
               (unsigned int)status->error_code,
               (unsigned int)status->last_command_seq,
               (unsigned int)status->last_command_code,
               (unsigned int)status->last_command_result,
               robot_arm_result_name(status->last_command_result));
    robot_arm_print_status_flags(status->status_flags);
}

static void robot_arm_print_axes(const robot_arm_axis_status_t *axes)
{
    if (axes == RT_NULL)
    {
        return;
    }

    rt_kprintf("[arm] axes: J1=%d(0.1deg) J2=%d J3=%d J4=%d J5=%d J6=%d(0.1%%)\n",
               (int)axes->axis_x10[0],
               (int)axes->axis_x10[1],
               (int)axes->axis_x10[2],
               (int)axes->axis_x10[3],
               (int)axes->axis_x10[4],
               (int)axes->axis_x10[5]);
}

static void robot_arm_print_action_status(const robot_arm_action_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    rt_kprintf("[arm] action slot=%u steps=%u current=%u state=%u(%s) progress=%u/1000\n",
               (unsigned int)status->slot,
               (unsigned int)status->step_count,
               (unsigned int)status->current_step,
               (unsigned int)status->state,
               robot_arm_action_state_name(status->state),
               (unsigned int)status->playback_progress);
}

int robot_arm_read_base_status(uint8_t addr, robot_arm_base_status_t *status)
{
    uint16_t regs[ROBOT_ARM_BASE_STATUS_USED];
    uint16_t offset;
    int ret;

    if (status == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(regs, 0, sizeof(regs));
    for (offset = 0U; offset < ROBOT_ARM_BASE_STATUS_USED; offset += ROBOT_ARM_BASE_READ_CHUNK)
    {
        ret = modbus_rtu_read_input_registers(addr,
                                              (uint16_t)(ROBOT_ARM_REG_BASE_STATUS + offset),
                                              ROBOT_ARM_BASE_READ_CHUNK,
                                              &regs[offset],
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
        if (ret != RT_EOK)
        {
            robot_arm_count_error(ret);
            return ret;
        }
    }

    ret = RT_EOK;
    if (ret == RT_EOK)
    {
        status->device_id = regs[0];
        status->firmware_version = regs[1];
        status->status_flags = regs[2];
        status->error_code = regs[3];
        status->servo_count = regs[4];
        status->heartbeat_counter = regs[5];
        status->comm_error_count = regs[6];
        status->control_mode = regs[7];
        status->protocol_version = regs[8];
        status->last_command_seq = regs[9];
        status->last_command_code = regs[10];
        status->last_command_result = regs[11];
        s_robot_arm_stats.read_base_cmds++;
    }

    return ret;
}

int robot_arm_read_axes(uint8_t addr, robot_arm_axis_status_t *axes)
{
    uint16_t regs[ROBOT_ARM_AXIS_COUNT];
    uint16_t offset;
    uint8_t i;
    int ret;

    if (axes == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(regs, 0, sizeof(regs));
    for (offset = 0U; offset < ROBOT_ARM_AXIS_COUNT; offset += ROBOT_ARM_AXIS_READ_CHUNK)
    {
        ret = modbus_rtu_read_input_registers(addr,
                                              (uint16_t)(ROBOT_ARM_REG_CURRENT_AXIS + offset),
                                              ROBOT_ARM_AXIS_READ_CHUNK,
                                              &regs[offset],
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
        if (ret != RT_EOK)
        {
            robot_arm_count_error(ret);
            return ret;
        }
    }

    for (i = 0U; i < ROBOT_ARM_AXIS_COUNT; i++)
    {
        axes->axis_x10[i] = robot_arm_reg_to_i16(regs[i]);
    }
    s_robot_arm_stats.read_axis_cmds++;
    return RT_EOK;
}

int robot_arm_read_action_status(uint8_t addr, robot_arm_action_status_t *status)
{
    uint16_t regs[ROBOT_ARM_ACTION_STATUS_COUNT];
    int ret;

    if (status == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(regs, 0, sizeof(regs));
    ret = modbus_rtu_read_input_registers(addr, ROBOT_ARM_REG_ACTION_STATUS,
                                          ROBOT_ARM_ACTION_READ_HEAD,
                                          regs,
                                          ROBOT_ARM_TIMEOUT_MS, ROBOT_ARM_RETRIES);
    if (ret != RT_EOK)
    {
        robot_arm_count_error(ret);
        return ret;
    }

    ret = modbus_rtu_read_input_registers(addr,
                                          (uint16_t)(ROBOT_ARM_REG_ACTION_STATUS + ROBOT_ARM_ACTION_READ_HEAD),
                                          ROBOT_ARM_ACTION_READ_TAIL,
                                          &regs[ROBOT_ARM_ACTION_READ_HEAD],
                                          ROBOT_ARM_TIMEOUT_MS, ROBOT_ARM_RETRIES);
    if (ret != RT_EOK)
    {
        robot_arm_count_error(ret);
        return ret;
    }

    status->slot = regs[0];
    status->step_count = regs[1];
    status->current_step = regs[2];
    status->state = regs[3];
    status->playback_progress = regs[4];
    s_robot_arm_stats.read_action_cmds++;
    return RT_EOK;
}

int robot_arm_read_last_command(uint8_t addr, uint16_t *seq, uint16_t *code, uint16_t *result)
{
    uint16_t regs[ROBOT_ARM_LAST_COMMAND_COUNT];
    int ret;

    ret = modbus_rtu_read_input_registers(addr, ROBOT_ARM_REG_LAST_COMMAND,
                                          ROBOT_ARM_LAST_COMMAND_COUNT, regs,
                                          ROBOT_ARM_TIMEOUT_MS, ROBOT_ARM_RETRIES);
    if (ret == RT_EOK)
    {
        if (seq != RT_NULL)
        {
            *seq = regs[0];
        }
        if (code != RT_NULL)
        {
            *code = regs[1];
        }
        if (result != RT_NULL)
        {
            *result = regs[2];
        }
        s_robot_arm_stats.read_base_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

static void robot_arm_print_command_confirm(uint8_t addr, uint16_t expected_seq, uint8_t check_seq)
{
    uint16_t seq = 0U;
    uint16_t code = 0U;
    uint16_t result = 0U;
    int ret;

    ret = robot_arm_read_last_command(addr, &seq, &code, &result);
    if (ret == RT_EOK)
    {
        if (check_seq != 0U)
        {
            rt_kprintf("[arm] confirm seq=%u code=0x%04x result=%u(%s) match=%u\n",
                       (unsigned int)seq,
                       (unsigned int)code,
                       (unsigned int)result,
                       robot_arm_result_name(result),
                       (unsigned int)(seq == expected_seq));
        }
        else
        {
            rt_kprintf("[arm] confirm seq=%u code=0x%04x result=%u(%s)\n",
                       (unsigned int)seq,
                       (unsigned int)code,
                       (unsigned int)result,
                       robot_arm_result_name(result));
        }
    }
    else
    {
        rt_kprintf("[arm] confirm read failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }
}

int robot_arm_send_command(uint8_t addr, robot_arm_command_t command, uint16_t seq)
{
    uint16_t values[3];
    int ret;

    values[0] = (uint16_t)command;
    values[1] = ROBOT_ARM_APPLY_NONE;
    values[2] = seq;

    ret = modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_CONTROL_COMMAND,
                                              values, 3U,
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.control_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

int robot_arm_set_task_param(uint8_t addr, int16_t object_yaw_x10, uint16_t seq)
{
    uint16_t values[2];
    int ret;

    values[0] = (uint16_t)object_yaw_x10;   /* int16 按补码写入 */
    values[1] = seq;

    ret = modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_OBJECT_YAW,
                                              values, 2U,
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.control_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

int robot_arm_emergency_stop(uint8_t addr)
{
    int ret = modbus_rtu_write_single_register(addr, ROBOT_ARM_REG_CONTROL_COMMAND,
                                               ROBOT_ARM_COMMAND_ESTOP,
                                               ROBOT_ARM_FAST_TIMEOUT_MS, 0U);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.control_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

int robot_arm_clear_emergency_stop(uint8_t addr)
{
    int ret = modbus_rtu_write_single_register(addr, ROBOT_ARM_REG_CONTROL_COMMAND,
                                               ROBOT_ARM_COMMAND_CLEAR_ESTOP,
                                               ROBOT_ARM_FAST_TIMEOUT_MS, 0U);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.control_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

int robot_arm_write_axis_targets(uint8_t addr, const int16_t axis_x10[ROBOT_ARM_AXIS_COUNT])
{
    uint16_t values[ROBOT_ARM_AXIS_COUNT];
    uint8_t i;

    if (axis_x10 == RT_NULL)
    {
        return -RT_EINVAL;
    }

    for (i = 0U; i < ROBOT_ARM_AXIS_COUNT; i++)
    {
        values[i] = robot_arm_i16_to_reg(axis_x10[i]);
    }

    return modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_AXIS_TARGET,
                                               values, ROBOT_ARM_AXIS_COUNT,
                                               ROBOT_ARM_TIMEOUT_MS,
                                               ROBOT_ARM_RETRIES);
}

int robot_arm_apply_axis_targets(uint8_t addr, uint16_t seq)
{
    uint16_t values[2];
    int ret;

    values[0] = ROBOT_ARM_APPLY_AXIS;
    values[1] = seq;

    ret = modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_TARGET_APPLY,
                                              values, 2U,
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.apply_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

int robot_arm_write_raw_us_targets(uint8_t addr, const uint16_t raw_us[ROBOT_ARM_AXIS_COUNT])
{
    if (raw_us == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_RAW_US_TARGET,
                                               raw_us, ROBOT_ARM_AXIS_COUNT,
                                               ROBOT_ARM_TIMEOUT_MS,
                                               ROBOT_ARM_RETRIES);
}

int robot_arm_apply_raw_us_targets(uint8_t addr, uint16_t seq)
{
    uint16_t values[2];
    int ret;

    values[0] = ROBOT_ARM_APPLY_RAW_US;
    values[1] = seq;

    ret = modbus_rtu_write_multiple_registers(addr, ROBOT_ARM_REG_TARGET_APPLY,
                                              values, 2U,
                                              ROBOT_ARM_TIMEOUT_MS,
                                              ROBOT_ARM_RETRIES);
    if (ret == RT_EOK)
    {
        s_robot_arm_stats.apply_cmds++;
    }

    robot_arm_count_error(ret);
    return ret;
}

static int robot_arm_read_cmd(int argc, char **argv)
{
    uint8_t addr = ROBOT_ARM_DEFAULT_ADDR;
    robot_arm_base_status_t base;
    robot_arm_axis_status_t axes;
    robot_arm_action_status_t action;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)robot_arm_parse_u32(argv[1], ROBOT_ARM_DEFAULT_ADDR);
    }

    memset(&base, 0, sizeof(base));
    memset(&axes, 0, sizeof(axes));
    memset(&action, 0, sizeof(action));

    ret = robot_arm_read_base_status(addr, &base);
    if (ret == RT_EOK)
    {
        ret = robot_arm_read_axes(addr, &axes);
    }
    if (ret == RT_EOK)
    {
        ret = robot_arm_read_action_status(addr, &action);
    }

    if (ret == RT_EOK)
    {
        robot_arm_print_base_status(&base);
        robot_arm_print_axes(&axes);
        robot_arm_print_action_status(&action);
    }
    else
    {
        rt_kprintf("[arm] read failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int robot_arm_status_cmd(int argc, char **argv)
{
    uint8_t addr = ROBOT_ARM_DEFAULT_ADDR;
    robot_arm_base_status_t status;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)robot_arm_parse_u32(argv[1], ROBOT_ARM_DEFAULT_ADDR);
    }

    memset(&status, 0, sizeof(status));
    ret = robot_arm_read_base_status(addr, &status);
    if (ret == RT_EOK)
    {
        robot_arm_print_base_status(&status);
    }
    else
    {
        rt_kprintf("[arm] status failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int robot_arm_axes_cmd(int argc, char **argv)
{
    uint8_t addr = ROBOT_ARM_DEFAULT_ADDR;
    robot_arm_axis_status_t axes;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)robot_arm_parse_u32(argv[1], ROBOT_ARM_DEFAULT_ADDR);
    }

    memset(&axes, 0, sizeof(axes));
    ret = robot_arm_read_axes(addr, &axes);
    if (ret == RT_EOK)
    {
        robot_arm_print_axes(&axes);
    }
    else
    {
        rt_kprintf("[arm] axes failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int robot_arm_action_status_cmd(int argc, char **argv)
{
    uint8_t addr = ROBOT_ARM_DEFAULT_ADDR;
    robot_arm_action_status_t status;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)robot_arm_parse_u32(argv[1], ROBOT_ARM_DEFAULT_ADDR);
    }

    memset(&status, 0, sizeof(status));
    ret = robot_arm_read_action_status(addr, &status);
    if (ret == RT_EOK)
    {
        robot_arm_print_action_status(&status);
    }
    else
    {
        rt_kprintf("[arm] action status failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int robot_arm_last_cmd(int argc, char **argv)
{
    uint8_t addr = ROBOT_ARM_DEFAULT_ADDR;
    uint16_t seq = 0U;
    uint16_t code = 0U;
    uint16_t result = 0U;
    int ret;

    if (argc >= 2)
    {
        addr = (uint8_t)robot_arm_parse_u32(argv[1], ROBOT_ARM_DEFAULT_ADDR);
    }

    ret = robot_arm_read_last_command(addr, &seq, &code, &result);
    if (ret == RT_EOK)
    {
        rt_kprintf("[arm] last seq=%u code=0x%04x result=%u(%s)\n",
                   (unsigned int)seq,
                   (unsigned int)code,
                   (unsigned int)result,
                   robot_arm_result_name(result));
    }
    else
    {
        rt_kprintf("[arm] last failed addr=%u ret=%d\n", (unsigned int)addr, ret);
    }

    return ret;
}

static int robot_arm_play_cmd(int argc, char **argv)
{
    uint16_t seq;
    int ret;

    seq = robot_arm_parse_seq_arg(argc, argv, 1);

    ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_PLAY, seq);
    rt_kprintf("[arm] play addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_home_cmd(int argc, char **argv)
{
    uint16_t seq;
    int ret;

    seq = robot_arm_parse_seq_arg(argc, argv, 1);

    ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_HOME, seq);
    rt_kprintf("[arm] home addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_stop_cmd(int argc, char **argv)
{
    uint16_t seq;
    int ret;

    seq = robot_arm_parse_seq_arg(argc, argv, 1);

    ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_STOP_PLAY, seq);
    rt_kprintf("[arm] stop addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_save_cmd(int argc, char **argv)
{
    uint16_t seq;
    int ret;

    seq = robot_arm_parse_seq_arg(argc, argv, 1);

    ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_SAVE_ACTION, seq);
    rt_kprintf("[arm] save action addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_clear_action_cmd(int argc, char **argv)
{
    uint16_t seq;
    int ret;

    seq = robot_arm_parse_seq_arg(argc, argv, 1);

    ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_CLEAR_ACTION, seq);
    rt_kprintf("[arm] clear action addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_estop_cmd(int argc, char **argv)
{
    int ret;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    ret = robot_arm_emergency_stop(ROBOT_ARM_DEFAULT_ADDR);
    rt_kprintf("[arm] estop addr=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, 0U, 0U);
    }
    return ret;
}

static int robot_arm_clear_estop_cmd(int argc, char **argv)
{
    int ret;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    ret = robot_arm_clear_emergency_stop(ROBOT_ARM_DEFAULT_ADDR);
    rt_kprintf("[arm] clear estop addr=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, 0U, 0U);
    }
    return ret;
}

static int robot_arm_axis_cmd(int argc, char **argv)
{
    int16_t axes[ROBOT_ARM_AXIS_COUNT];
    uint16_t seq;
    uint8_t i;
    int ret;

    if ((argc != 7) && (argc != 8))
    {
        rt_kprintf("[arm] usage: arm_axis <j1_x10> <j2_x10> <j3_x10> <j4_x10> <j5_x10> <j6_x10> [seq]\n");
        rt_kprintf("[arm] unit: J1-J5 0.1deg signed, J6 0.1%% 0..1000\n");
        return -1;
    }

    for (i = 0U; i < ROBOT_ARM_AXIS_COUNT; i++)
    {
        axes[i] = (int16_t)robot_arm_parse_i32(argv[1 + i], 0);
    }

    if (argc > 7)
    {
        seq = (uint16_t)robot_arm_parse_u32(argv[7], 0U);
    }
    else
    {
        seq = robot_arm_next_seq();
    }

    ret = robot_arm_write_axis_targets(ROBOT_ARM_DEFAULT_ADDR, axes);
    if (ret == RT_EOK)
    {
        ret = robot_arm_apply_axis_targets(ROBOT_ARM_DEFAULT_ADDR, seq);
    }

    rt_kprintf("[arm] axis addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_raw_us_cmd(int argc, char **argv)
{
    uint16_t raw_us[ROBOT_ARM_AXIS_COUNT];
    uint16_t seq;
    uint8_t i;
    int ret;

    if ((argc != 7) && (argc != 8))
    {
        rt_kprintf("[arm] usage: arm_raw_us <us0> <us1> <us2> <us3> <us4> <us5> [seq]\n");
        return -1;
    }

    for (i = 0U; i < ROBOT_ARM_AXIS_COUNT; i++)
    {
        raw_us[i] = (uint16_t)robot_arm_parse_u32(argv[1 + i], 1500U);
    }

    if (argc > 7)
    {
        seq = (uint16_t)robot_arm_parse_u32(argv[7], 0U);
    }
    else
    {
        seq = robot_arm_next_seq();
    }

    ret = robot_arm_write_raw_us_targets(ROBOT_ARM_DEFAULT_ADDR, raw_us);
    if (ret == RT_EOK)
    {
        ret = robot_arm_apply_raw_us_targets(ROBOT_ARM_DEFAULT_ADDR, seq);
    }

    rt_kprintf("[arm] raw_us addr=%u seq=%u ret=%d\n", (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
    }
    return ret;
}

static int robot_arm_stat_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("[arm] read_base=%u read_axis=%u read_action=%u control=%u apply=%u errors=%u default_addr=%u next_seq=%u\n",
               (unsigned int)s_robot_arm_stats.read_base_cmds,
               (unsigned int)s_robot_arm_stats.read_axis_cmds,
               (unsigned int)s_robot_arm_stats.read_action_cmds,
               (unsigned int)s_robot_arm_stats.control_cmds,
               (unsigned int)s_robot_arm_stats.apply_cmds,
               (unsigned int)s_robot_arm_stats.errors,
               (unsigned int)ROBOT_ARM_DEFAULT_ADDR,
               (unsigned int)s_robot_arm_stats.next_seq);
    return 0;
}

static int robot_arm_clear_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    memset(&s_robot_arm_stats, 0, sizeof(s_robot_arm_stats));
    s_robot_arm_stats.next_seq = 1U;
    rt_kprintf("[arm] stats cleared\n");
    return 0;
}

static int robot_arm_yaw_cmd(int argc, char **argv)
{
    int16_t  yaw_x10;
    uint16_t seq;
    int ret;

    if (argc < 2)
    {
        rt_kprintf("[arm] usage: arm_yaw <yaw_x10> [seq]\n");
        rt_kprintf("[arm] writes 0x00D0/0x00D1 only; then run: arm_play <same seq>\n");
        return -1;
    }

    yaw_x10 = (int16_t)robot_arm_parse_i32(argv[1], 0);
    if (argc > 2)
    {
        seq = (uint16_t)robot_arm_parse_u32(argv[2], 0U);
    }
    else
    {
        seq = robot_arm_next_seq();
    }

    ret = robot_arm_set_task_param(ROBOT_ARM_DEFAULT_ADDR, yaw_x10, seq);
    rt_kprintf("[arm] yaw set addr=%u yaw_x10=%d seq=%u ret=%d (pair with: arm_play %u)\n",
               (unsigned int)ROBOT_ARM_DEFAULT_ADDR, (int)yaw_x10,
               (unsigned int)seq, ret, (unsigned int)seq);
    return ret;
}

static int robot_arm_ng_cmd(int argc, char **argv)
{
    int16_t  yaw_x10;
    uint16_t seq;
    int ret;

    if (argc < 2)
    {
        rt_kprintf("[arm] usage: arm_ng <yaw_x10>\n");
        return -1;
    }

    yaw_x10 = (int16_t)robot_arm_parse_i32(argv[1], 0);
    seq = robot_arm_next_seq();

    ret = robot_arm_set_task_param(ROBOT_ARM_DEFAULT_ADDR, yaw_x10, seq);
    rt_kprintf("[arm] ng yaw set yaw_x10=%d seq=%u ret=%d\n",
               (int)yaw_x10, (unsigned int)seq, ret);
    if (ret == RT_EOK)
    {
        ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_PLAY, seq);
        rt_kprintf("[arm] ng play seq=%u ret=%d\n", (unsigned int)seq, ret);
        if (ret == RT_EOK)
        {
            robot_arm_print_command_confirm(ROBOT_ARM_DEFAULT_ADDR, seq, 1U);
        }
    }
    return ret;
}

MSH_CMD_EXPORT_ALIAS(robot_arm_read_cmd, arm_read, read robot arm base axes and action status);
MSH_CMD_EXPORT_ALIAS(robot_arm_status_cmd, arm_status, read robot arm base status);
MSH_CMD_EXPORT_ALIAS(robot_arm_axes_cmd, arm_axes, read robot arm current axes);
MSH_CMD_EXPORT_ALIAS(robot_arm_action_status_cmd, arm_action_status, read robot arm action status);
MSH_CMD_EXPORT_ALIAS(robot_arm_last_cmd, arm_last, read robot arm last command result);
MSH_CMD_EXPORT_ALIAS(robot_arm_play_cmd, arm_play, play robot arm recorded action);
MSH_CMD_EXPORT_ALIAS(robot_arm_home_cmd, arm_home, return robot arm to saved home pose);
MSH_CMD_EXPORT_ALIAS(robot_arm_stop_cmd, arm_stop, stop robot arm playback);
MSH_CMD_EXPORT_ALIAS(robot_arm_save_cmd, arm_save, save robot arm action to Data Flash);
MSH_CMD_EXPORT_ALIAS(robot_arm_clear_action_cmd, arm_clear_action, clear robot arm action slot);
MSH_CMD_EXPORT_ALIAS(robot_arm_estop_cmd, arm_estop, emergency stop robot arm);
MSH_CMD_EXPORT_ALIAS(robot_arm_clear_estop_cmd, arm_clear_estop, clear robot arm emergency stop);
MSH_CMD_EXPORT_ALIAS(robot_arm_axis_cmd, arm_axis, write and apply robot arm axis targets);
MSH_CMD_EXPORT_ALIAS(robot_arm_raw_us_cmd, arm_raw_us, write and apply robot arm raw us targets);
MSH_CMD_EXPORT_ALIAS(robot_arm_stat_cmd, arm_stat, show robot arm master statistics);
MSH_CMD_EXPORT_ALIAS(robot_arm_clear_cmd, arm_clear, clear robot arm master statistics);
MSH_CMD_EXPORT_ALIAS(robot_arm_yaw_cmd, arm_yaw, set NG roll yaw param 0x00D0/0x00D1 only);
MSH_CMD_EXPORT_ALIAS(robot_arm_ng_cmd, arm_ng, write yaw then PLAY NG sort in one shot);

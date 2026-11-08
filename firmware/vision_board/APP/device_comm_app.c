#include "device_comm_app.h"

#include "modbus_rtu_app.h"

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

#define DEVICE_COMM_THREAD_STACK_SIZE   4096U
#define DEVICE_COMM_THREAD_PRIORITY     24U
#define DEVICE_COMM_THREAD_TICK         10U
#define DEVICE_COMM_POLL_MS             800U
#define DEVICE_COMM_GAP_MS              20U
#define DEVICE_COMM_CONVEYOR_RPM_MIN    5U
#define DEVICE_COMM_CONVEYOR_RPM_MAX    3000U
#define DEVICE_COMM_CONVEYOR_ACC_MAX    255U
#define DEVICE_COMM_CMD_QUEUE_DEPTH     8U

typedef struct
{
    rt_thread_t thread;
    rt_mutex_t lock;
    rt_sem_t sem;
    rt_bool_t ready;
    device_comm_cmd_t cmd_queue[DEVICE_COMM_CMD_QUEUE_DEPTH];
    uint8_t cmd_head;
    uint8_t cmd_tail;
    uint8_t cmd_count;
    uint16_t next_arm_seq;
    device_comm_snapshot_t snapshot;
} device_comm_context_t;

static device_comm_context_t s_device_comm;

static void device_comm_thread_entry(void *parameter);
static void device_comm_poll_once(void);
static void device_comm_handle_command(device_comm_cmd_t cmd);
static void device_comm_queue_push_locked(device_comm_cmd_t cmd);
static device_comm_cmd_t device_comm_queue_pop_locked(void);
static rt_bool_t device_comm_parse_u32_arg(const char *text, uint32_t *out_value);

int device_comm_app_init(void)
{
    if (s_device_comm.ready == RT_TRUE)
    {
        return RT_EOK;
    }

    memset(&s_device_comm, 0, sizeof(s_device_comm));
    s_device_comm.next_arm_seq = 100U;
    s_device_comm.snapshot.conveyor_set_rpm = DEVICE_COMM_DEFAULT_CONVEYOR_RPM;
    s_device_comm.snapshot.conveyor_acc = DEVICE_COMM_DEFAULT_CONVEYOR_ACC;

    s_device_comm.lock = rt_mutex_create("devcomm", RT_IPC_FLAG_PRIO);
    if (s_device_comm.lock == RT_NULL)
    {
        return -RT_ERROR;
    }

    s_device_comm.sem = rt_sem_create("devcomm", 0, RT_IPC_FLAG_PRIO);
    if (s_device_comm.sem == RT_NULL)
    {
        rt_mutex_delete(s_device_comm.lock);
        s_device_comm.lock = RT_NULL;
        return -RT_ERROR;
    }

    s_device_comm.thread = rt_thread_create("devcomm",
                                            device_comm_thread_entry,
                                            RT_NULL,
                                            DEVICE_COMM_THREAD_STACK_SIZE,
                                            DEVICE_COMM_THREAD_PRIORITY,
                                            DEVICE_COMM_THREAD_TICK);
    if (s_device_comm.thread == RT_NULL)
    {
        rt_sem_delete(s_device_comm.sem);
        rt_mutex_delete(s_device_comm.lock);
        s_device_comm.sem = RT_NULL;
        s_device_comm.lock = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(s_device_comm.thread);
    s_device_comm.ready = RT_TRUE;
    return RT_EOK;
}

int device_comm_app_get_snapshot(device_comm_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (device_comm_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    *out_snapshot = s_device_comm.snapshot;
    rt_mutex_release(s_device_comm.lock);
    return RT_EOK;
}

void device_comm_app_request(device_comm_cmd_t cmd)
{
    if (cmd == DEVICE_COMM_CMD_NONE)
    {
        return;
    }

    if (device_comm_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    device_comm_queue_push_locked(cmd);
    rt_mutex_release(s_device_comm.lock);
    rt_sem_release(s_device_comm.sem);
}

void device_comm_app_set_conveyor_rpm(uint16_t rpm)
{
    if (rpm < DEVICE_COMM_CONVEYOR_RPM_MIN)
    {
        rpm = DEVICE_COMM_CONVEYOR_RPM_MIN;
    }
    else if (rpm > DEVICE_COMM_CONVEYOR_RPM_MAX)
    {
        rpm = DEVICE_COMM_CONVEYOR_RPM_MAX;
    }

    if (device_comm_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    s_device_comm.snapshot.conveyor_set_rpm = rpm;
    rt_mutex_release(s_device_comm.lock);
}

uint16_t device_comm_app_get_conveyor_rpm(void)
{
    uint16_t rpm = DEVICE_COMM_DEFAULT_CONVEYOR_RPM;

    if (device_comm_app_init() != RT_EOK)
    {
        return rpm;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    rpm = s_device_comm.snapshot.conveyor_set_rpm;
    rt_mutex_release(s_device_comm.lock);
    return rpm;
}

void device_comm_app_set_conveyor_acc(uint8_t acc)
{
    if (acc > DEVICE_COMM_CONVEYOR_ACC_MAX)
    {
        acc = DEVICE_COMM_CONVEYOR_ACC_MAX;
    }

    if (device_comm_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    s_device_comm.snapshot.conveyor_acc = acc;
    rt_mutex_release(s_device_comm.lock);
}

uint8_t device_comm_app_get_conveyor_acc(void)
{
    uint8_t acc = DEVICE_COMM_DEFAULT_CONVEYOR_ACC;

    if (device_comm_app_init() != RT_EOK)
    {
        return acc;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    acc = s_device_comm.snapshot.conveyor_acc;
    rt_mutex_release(s_device_comm.lock);
    return acc;
}

uint16_t device_comm_app_next_arm_seq(void)
{
    uint16_t seq;

    if (device_comm_app_init() != RT_EOK)
    {
        return 0U;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    if ((s_device_comm.snapshot.arm_base.last_command_seq != 0U) &&
        (s_device_comm.next_arm_seq <= s_device_comm.snapshot.arm_base.last_command_seq))
    {
        s_device_comm.next_arm_seq = (uint16_t)(s_device_comm.snapshot.arm_base.last_command_seq + 1U);
        if (s_device_comm.next_arm_seq == 0U)
        {
            s_device_comm.next_arm_seq = 100U;
        }
    }
    seq = s_device_comm.next_arm_seq;
    s_device_comm.next_arm_seq++;
    if (s_device_comm.next_arm_seq == 0U)
    {
        s_device_comm.next_arm_seq = 100U;
    }
    rt_mutex_release(s_device_comm.lock);
    return seq;
}

const char *device_comm_arm_mode_name(uint16_t mode)
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
            return "play";
        case 4U:
            return "estop";
        default:
            return "unknown";
    }
}

const char *device_comm_arm_action_name(uint16_t state)
{
    switch (state)
    {
        case 0U:
            return "ready";
        case 1U:
            return "record";
        case 2U:
            return "play";
        case 3U:
            return "stop";
        case 4U:
            return "done";
        case 5U:
            return "error";
        default:
            return "unknown";
    }
}

const char *device_comm_arm_result_name(uint16_t result)
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
            return "invalid";
        case ROBOT_ARM_RESULT_ESTOP:
            return "estop";
        case ROBOT_ARM_RESULT_HMI_ACTIVE:
            return "hmi";
        case ROBOT_ARM_RESULT_ACTION_EMPTY:
            return "empty";
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

static void device_comm_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        device_comm_cmd_t cmd;

        rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
        cmd = device_comm_queue_pop_locked();
        rt_mutex_release(s_device_comm.lock);

        if (cmd != DEVICE_COMM_CMD_NONE)
        {
            device_comm_handle_command(cmd);
        }

        device_comm_poll_once();
        rt_sem_take(s_device_comm.sem, rt_tick_from_millisecond(DEVICE_COMM_POLL_MS));
    }
}

static void device_comm_poll_once(void)
{
    robot_arm_base_status_t arm_base;
    robot_arm_action_status_t arm_action;
    zdt_motor_status_t conveyor;
    uint8_t bus_open = 0U;
    uint8_t arm_online = 0U;
    uint8_t conveyor_online = 0U;
    int ret;
    int last_error = RT_EOK;

    memset(&arm_base, 0, sizeof(arm_base));
    memset(&arm_action, 0, sizeof(arm_action));
    memset(&conveyor, 0, sizeof(conveyor));

    ret = modbus_rtu_init();
    if (ret == RT_EOK)
    {
        bus_open = 1U;
    }
    else
    {
        last_error = ret;
    }

    if (bus_open != 0U)
    {
        ret = robot_arm_read_base_status(ROBOT_ARM_DEFAULT_ADDR, &arm_base);
        if (ret == RT_EOK)
        {
            arm_online = 1U;
            rt_thread_mdelay(DEVICE_COMM_GAP_MS);
            ret = robot_arm_read_action_status(ROBOT_ARM_DEFAULT_ADDR, &arm_action);
            if (ret != RT_EOK)
            {
                last_error = ret;
            }
        }
        else
        {
            last_error = ret;
        }

        rt_thread_mdelay(DEVICE_COMM_GAP_MS);
        ret = zdt_motor_read_speed(ZDT_MOTOR_DEFAULT_ADDR, &conveyor);
        if (ret == RT_EOK)
        {
            rt_thread_mdelay(DEVICE_COMM_GAP_MS);
            ret = zdt_motor_read_position(ZDT_MOTOR_DEFAULT_ADDR, &conveyor);
        }
        if (ret == RT_EOK)
        {
            rt_thread_mdelay(DEVICE_COMM_GAP_MS);
            ret = zdt_motor_read_flags(ZDT_MOTOR_DEFAULT_ADDR, &conveyor);
        }
        if (ret == RT_EOK)
        {
            conveyor_online = 1U;
        }
        else
        {
            last_error = ret;
        }
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    s_device_comm.snapshot.service_running = 1U;
    s_device_comm.snapshot.bus_open = bus_open;
    s_device_comm.snapshot.arm_online = arm_online;
    s_device_comm.snapshot.conveyor_online = conveyor_online;
    s_device_comm.snapshot.last_error = last_error;
    s_device_comm.snapshot.poll_count++;
    if (arm_online != 0U)
    {
        s_device_comm.snapshot.arm_base = arm_base;
        s_device_comm.snapshot.arm_action = arm_action;
    }
    else
    {
        memset(&s_device_comm.snapshot.arm_base, 0, sizeof(s_device_comm.snapshot.arm_base));
        memset(&s_device_comm.snapshot.arm_action, 0, sizeof(s_device_comm.snapshot.arm_action));
    }
    if (conveyor_online != 0U)
    {
        s_device_comm.snapshot.conveyor = conveyor;
    }
    else
    {
        memset(&s_device_comm.snapshot.conveyor, 0, sizeof(s_device_comm.snapshot.conveyor));
    }
    rt_mutex_release(s_device_comm.lock);
}

static void device_comm_handle_command(device_comm_cmd_t cmd)
{
    uint16_t rpm;
    uint8_t acc;
    int ret = RT_EOK;

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    rpm = s_device_comm.snapshot.conveyor_set_rpm;
    acc = s_device_comm.snapshot.conveyor_acc;
    s_device_comm.snapshot.command_count++;
    rt_mutex_release(s_device_comm.lock);

    switch (cmd)
    {
        case DEVICE_COMM_CMD_CONVEYOR_CW:
            ret = zdt_motor_speed(ZDT_MOTOR_DEFAULT_ADDR, ZDT_MOTOR_DIR_CW, rpm, acc, 0U);
            break;
        case DEVICE_COMM_CMD_CONVEYOR_CCW:
            ret = zdt_motor_speed(ZDT_MOTOR_DEFAULT_ADDR, ZDT_MOTOR_DIR_CCW, rpm, acc, 0U);
            break;
        case DEVICE_COMM_CMD_CONVEYOR_STOP:
            ret = zdt_motor_stop(ZDT_MOTOR_DEFAULT_ADDR);
            break;
        case DEVICE_COMM_CMD_ARM_HOME:
            ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_HOME, device_comm_app_next_arm_seq());
            break;
        case DEVICE_COMM_CMD_ARM_PLAY:
            ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_PLAY, device_comm_app_next_arm_seq());
            break;
        case DEVICE_COMM_CMD_ARM_STOP:
            ret = robot_arm_send_command(ROBOT_ARM_DEFAULT_ADDR, ROBOT_ARM_COMMAND_STOP_PLAY, device_comm_app_next_arm_seq());
            break;
        case DEVICE_COMM_CMD_ARM_ESTOP:
            ret = robot_arm_emergency_stop(ROBOT_ARM_DEFAULT_ADDR);
            break;
        case DEVICE_COMM_CMD_ARM_CLEAR_ESTOP:
            ret = robot_arm_clear_emergency_stop(ROBOT_ARM_DEFAULT_ADDR);
            break;
        case DEVICE_COMM_CMD_REFRESH:
        case DEVICE_COMM_CMD_ARM_REFRESH:
        case DEVICE_COMM_CMD_NONE:
        default:
            ret = RT_EOK;
            break;
    }

    rt_mutex_take(s_device_comm.lock, RT_WAITING_FOREVER);
    s_device_comm.snapshot.last_error = ret;
    rt_mutex_release(s_device_comm.lock);
}

static void device_comm_queue_push_locked(device_comm_cmd_t cmd)
{
    if (cmd == DEVICE_COMM_CMD_ARM_ESTOP)
    {
        s_device_comm.cmd_head = 0U;
        s_device_comm.cmd_tail = 0U;
        s_device_comm.cmd_count = 0U;
    }

    if (s_device_comm.cmd_count >= DEVICE_COMM_CMD_QUEUE_DEPTH)
    {
        s_device_comm.cmd_head = (uint8_t)((s_device_comm.cmd_head + 1U) % DEVICE_COMM_CMD_QUEUE_DEPTH);
        s_device_comm.cmd_count--;
        s_device_comm.snapshot.last_error = -RT_ERROR;
    }

    s_device_comm.cmd_queue[s_device_comm.cmd_tail] = cmd;
    s_device_comm.cmd_tail = (uint8_t)((s_device_comm.cmd_tail + 1U) % DEVICE_COMM_CMD_QUEUE_DEPTH);
    s_device_comm.cmd_count++;
}

static device_comm_cmd_t device_comm_queue_pop_locked(void)
{
    device_comm_cmd_t cmd;

    if (s_device_comm.cmd_count == 0U)
    {
        return DEVICE_COMM_CMD_NONE;
    }

    cmd = s_device_comm.cmd_queue[s_device_comm.cmd_head];
    s_device_comm.cmd_head = (uint8_t)((s_device_comm.cmd_head + 1U) % DEVICE_COMM_CMD_QUEUE_DEPTH);
    s_device_comm.cmd_count--;
    return cmd;
}

static rt_bool_t device_comm_parse_u32_arg(const char *text, uint32_t *out_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0') || (out_value == RT_NULL))
    {
        return RT_FALSE;
    }
    if (text[0] == '-')
    {
        return RT_FALSE;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return RT_FALSE;
    }

    *out_value = (uint32_t)value;
    return RT_TRUE;
}

static int device_comm_stat_cmd(int argc, char **argv)
{
    device_comm_snapshot_t snapshot;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (device_comm_app_get_snapshot(&snapshot) != RT_EOK)
    {
        rt_kprintf("[devcomm] unavailable\n");
        return -RT_ERROR;
    }

    rt_kprintf("[devcomm] bus=%u arm=%u conveyor=%u rpm=%u acc=%u polls=%u cmds=%u last=%d\n",
               (unsigned int)snapshot.bus_open,
               (unsigned int)snapshot.arm_online,
               (unsigned int)snapshot.conveyor_online,
               (unsigned int)snapshot.conveyor_set_rpm,
               (unsigned int)snapshot.conveyor_acc,
               (unsigned int)snapshot.poll_count,
               (unsigned int)snapshot.command_count,
               snapshot.last_error);
    return RT_EOK;
}

static int device_comm_set_rpm_cmd(int argc, char **argv)
{
    uint32_t rpm;

    if ((argc != 2) || (device_comm_parse_u32_arg(argv[1], &rpm) != RT_TRUE))
    {
        rt_kprintf("[devcomm] usage: devcomm_set_rpm <rpm>\n");
        return -RT_EINVAL;
    }
    if (rpm > DEVICE_COMM_CONVEYOR_RPM_MAX)
    {
        rpm = DEVICE_COMM_CONVEYOR_RPM_MAX;
    }

    device_comm_app_set_conveyor_rpm((uint16_t)rpm);
    rt_kprintf("[devcomm] rpm=%u\n", (unsigned int)device_comm_app_get_conveyor_rpm());
    return RT_EOK;
}

static int device_comm_set_acc_cmd(int argc, char **argv)
{
    uint32_t acc;

    if ((argc != 2) || (device_comm_parse_u32_arg(argv[1], &acc) != RT_TRUE))
    {
        rt_kprintf("[devcomm] usage: devcomm_set_acc <acc>\n");
        return -RT_EINVAL;
    }
    if (acc > DEVICE_COMM_CONVEYOR_ACC_MAX)
    {
        acc = DEVICE_COMM_CONVEYOR_ACC_MAX;
    }

    device_comm_app_set_conveyor_acc((uint8_t)acc);
    rt_kprintf("[devcomm] acc=%u\n", (unsigned int)device_comm_app_get_conveyor_acc());
    return RT_EOK;
}

MSH_CMD_EXPORT_ALIAS(device_comm_stat_cmd, devcomm_stat, show device communication snapshot);
MSH_CMD_EXPORT_ALIAS(device_comm_set_rpm_cmd, devcomm_set_rpm, set conveyor rpm for device communication);
MSH_CMD_EXPORT_ALIAS(device_comm_set_acc_cmd, devcomm_set_acc, set conveyor acceleration for device communication);

/*
 * 分拣流程 msh 调试命令。
 */
#include "sort_demo_internal.h"

/* 解析无符号整数参数，非法时返回默认值。 */
static uint32_t sort_demo_parse_u32(const char *text, uint32_t default_value)
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

/* 解析无符号整数参数，非法返回 -RT_EINVAL。 */
static int sort_demo_parse_u32_arg(const char *text, uint32_t *out_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0'))
    {
        return -RT_EINVAL;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return -RT_EINVAL;
    }

    *out_value = (uint32_t)value;
    return RT_EOK;
}

/* 解析有符号整数参数，非法返回 -RT_EINVAL。 */
static int sort_demo_parse_i32_arg(const char *text, int32_t *out_value)
{
    long value;
    char *end_ptr;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0'))
    {
        return -RT_EINVAL;
    }

    value = strtol(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return -RT_EINVAL;
    }

    *out_value = (int32_t)value;
    return RT_EOK;
}

static int sort_demo_once_cmd(int argc, char **argv)
{
    uint16_t seq;
    uint16_t rpm;
    uint8_t acc;
    uint32_t pulses;

    if (argc > 5)
    {
        rt_kprintf("[sort] usage: demo_sort_once [seq] [rpm] [pulses] [acc]\n");
        rt_kprintf("[sort] default: seq=auto rpm=10 pulses=transfer acc=0\n");
        return -RT_EINVAL;
    }

    seq = (argc >= 2) ? (uint16_t)sort_demo_parse_u32(argv[1], 0U) : sort_demo_next_seq();
    if (seq == 0U)
    {
        seq = sort_demo_next_seq();
    }
    rpm = (uint16_t)((argc >= 3) ? sort_demo_parse_u32(argv[2], SORT_DEMO_DEFAULT_RPM) : SORT_DEMO_DEFAULT_RPM);
    pulses = (argc >= 4) ? sort_demo_parse_u32(argv[3], sort_flow_get_transfer_pulses()) : sort_flow_get_transfer_pulses();
    acc = (uint8_t)((argc >= 5) ? sort_demo_parse_u32(argv[4], SORT_DEMO_DEFAULT_ACC) : SORT_DEMO_DEFAULT_ACC);

    return sort_demo_run_once(seq, rpm, pulses, acc);
}

static int sort_flow_start_cmd(int argc, char **argv)
{
    int ret;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (g_sort_flow.busy == RT_TRUE)
    {
        rt_kprintf("[flow] busy\n");
        return -RT_EBUSY;
    }

    ret = sort_flow_start_conveyor();
    g_sort_flow.active = (ret == RT_EOK) ? RT_TRUE : RT_FALSE;
    g_sort_flow.last_error = ret;
    rt_kprintf("[flow] start ret=%d active=%u\n", ret, (unsigned int)g_sort_flow.active);
    return ret;
}

static int sort_flow_stop_cmd(int argc, char **argv)
{
    int ret;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    ret = sort_flow_stop_conveyor();
    g_sort_flow.active = RT_FALSE;
    g_sort_flow.last_error = ret;
    rt_kprintf("[flow] stop ret=%d active=0\n", ret);
    return ret;
}

static int sort_flow_status_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("[flow] active=%u busy=%u ok=%u ng=%u last=%s score=%u err=%d rpm=%u acc=%u base=%u comp=%d pulses=%u\n",
               (unsigned int)g_sort_flow.active,
               (unsigned int)g_sort_flow.busy,
               (unsigned int)g_sort_flow.ok_count,
               (unsigned int)g_sort_flow.ng_count,
               (g_sort_flow.last_ng == RT_TRUE) ? "ng" : "ok",
               (unsigned int)g_sort_flow.last_score_x10,
               g_sort_flow.last_error,
               (unsigned int)device_comm_app_get_conveyor_rpm(),
               (unsigned int)device_comm_app_get_conveyor_acc(),
               (unsigned int)sort_flow_get_transfer_base_pulses(),
               (int)sort_flow_get_transfer_comp_pulses(),
               (unsigned int)sort_flow_get_transfer_pulses());
    return RT_EOK;
}

static int sort_flow_comp_cmd(int argc, char **argv)
{
    int32_t comp_pulses;
    int ret;

    if (argc > 2)
    {
        rt_kprintf("[flow] usage: sort_flow_comp [comp_pulses]\n");
        rt_kprintf("[flow] range: %d..%d, actual=base+comp\n",
                   SORT_FLOW_TRANSFER_COMP_MIN,
                   SORT_FLOW_TRANSFER_COMP_MAX);
        return -RT_EINVAL;
    }

    if (argc == 2)
    {
        ret = sort_demo_parse_i32_arg(argv[1], &comp_pulses);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow] invalid comp_pulses\n");
            return ret;
        }

        ret = sort_flow_set_transfer_comp_pulses(comp_pulses);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow] comp out of range: %d..%d\n",
                       SORT_FLOW_TRANSFER_COMP_MIN,
                       SORT_FLOW_TRANSFER_COMP_MAX);
            return ret;
        }
    }

    rt_kprintf("[flow] transfer base=%u comp=%d pulses=%u\n",
               (unsigned int)sort_flow_get_transfer_base_pulses(),
               (int)sort_flow_get_transfer_comp_pulses(),
               (unsigned int)sort_flow_get_transfer_pulses());
    return RT_EOK;
}

static int sort_flow_queue_cmd(int argc, char **argv)
{
    int32_t value;
    int ret;

    if (argc == 1)
    {
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    if ((argc == 2) && (strcmp(argv[1], "clear") == 0))
    {
        if ((sort_flow_auto_is_active() == RT_TRUE) || (g_sort_flow.busy == RT_TRUE))
        {
            rt_kprintf("[flow_queue] stop auto flow before clear\n");
            return -RT_EBUSY;
        }
        sort_flow_queue_reset(RT_TRUE);
        rt_kprintf("[flow_queue] cleared\n");
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "scale") == 0))
    {
        ret = sort_demo_parse_i32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value < 1) || (value > 1000))
        {
            rt_kprintf("[flow_queue] invalid scale, range 1..1000 raw/pulse\n");
            return -RT_EINVAL;
        }
        g_sort_flow_position_scale_raw = value;
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    if ((argc == 3) && ((strcmp(argv[1], "lead") == 0) || (strcmp(argv[1], "tol") == 0)))
    {
        ret = sort_demo_parse_i32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value < 0) || (value > 50000))
        {
            rt_kprintf("[flow_queue] invalid lead, range 0..50000 raw\n");
            return -RT_EINVAL;
        }
        g_sort_flow_arrive_tolerance_raw = value;
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "guard") == 0) && (strcmp(argv[2], "auto") == 0))
    {
        g_sort_flow_queue_guard_auto = RT_TRUE;
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "guard") == 0))
    {
        ret = sort_demo_parse_i32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value < 0) || (value > 200000))
        {
            rt_kprintf("[flow_queue] invalid guard, range 0..200000 raw\n");
            return -RT_EINVAL;
        }
        g_sort_flow_queue_guard_raw = value;
        g_sort_flow_queue_guard_auto = RT_FALSE;
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    if ((argc == 4) && (strcmp(argv[1], "guard") == 0) && (strcmp(argv[2], "fixed") == 0))
    {
        ret = sort_demo_parse_i32_arg(argv[3], &value);
        if ((ret != RT_EOK) || (value < 0) || (value > 200000))
        {
            rt_kprintf("[flow_queue] invalid guard, range 0..200000 raw\n");
            return -RT_EINVAL;
        }
        g_sort_flow_queue_guard_raw = value;
        g_sort_flow_queue_guard_auto = RT_FALSE;
        sort_flow_queue_print_status();
        return RT_EOK;
    }

    rt_kprintf("[flow_queue] usage: sort_flow_queue [clear|scale <raw_per_pulse>|lead <raw>|guard auto|guard <raw>|guard fixed <raw>]\n");
    rt_kprintf("[flow_queue] note: tol <raw> is kept as an alias of lead <raw>\n");
    sort_flow_queue_print_status();
    return -RT_EINVAL;
}

static int sort_flow_object_cmd(int argc, char **argv)
{
    rt_bool_t ng;
    uint16_t score_x10;

    if ((argc < 2) || (argc > 3))
    {
        rt_kprintf("[flow] usage: sort_flow_object <ok|ng> [score_x10]\n");
        return -RT_EINVAL;
    }
    if (strcmp(argv[1], "ok") == 0)
    {
        ng = RT_FALSE;
        score_x10 = SORT_FLOW_OK_SCORE_X10;
    }
    else if (strcmp(argv[1], "ng") == 0)
    {
        ng = RT_TRUE;
        score_x10 = SORT_FLOW_NG_SCORE_X10;
    }
    else
    {
        rt_kprintf("[flow] usage: sort_flow_object <ok|ng> [score_x10]\n");
        return -RT_EINVAL;
    }
    if (argc == 3)
    {
        score_x10 = (uint16_t)sort_demo_parse_u32(argv[2], score_x10);
    }

    return sort_flow_submit_result(ng, score_x10);
}

static int sort_flow_auto_start_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    return sort_flow_auto_start();
}

static int sort_flow_auto_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    return sort_flow_auto_stop();
}

static int sort_flow_auto_status_cmd(int argc, char **argv)
{
    software_gate_snapshot_t gate_snapshot;
    sort_flow_auto_state_t state;
    rt_bool_t active;
    uint32_t handled_count;
    uint32_t last_gate_trigger_count;
    uint32_t settle_ms;
    uint32_t detect_timeout_ms;
    uint32_t pc_timeout_ms;
    uint32_t pc_req_id;
    uint32_t pc_accepted_count;
    uint32_t pc_timeout_count;
    uint32_t pc_fallback_count;
    rt_bool_t pc_waiting;
    int last_error;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (sort_flow_auto_init() != RT_EOK)
    {
        rt_kprintf("[flow_auto] unavailable\n");
        return -RT_ERROR;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    active = g_sort_flow_auto.active;
    state = g_sort_flow_auto.state;
    handled_count = g_sort_flow_auto.handled_count;
    last_gate_trigger_count = g_sort_flow_auto.last_gate_trigger_count;
    settle_ms = g_sort_flow_auto.settle_ms;
    detect_timeout_ms = g_sort_flow_auto.detect_timeout_ms;
    pc_timeout_ms = g_sort_flow_auto.pc_timeout_ms;
    pc_req_id = g_sort_flow_auto.pc_wait_req_id;
    pc_waiting = g_sort_flow_auto.pc_waiting;
    pc_accepted_count = g_sort_flow_auto.pc_accepted_count;
    pc_timeout_count = g_sort_flow_auto.pc_timeout_count;
    pc_fallback_count = g_sort_flow_auto.pc_fallback_count;
    last_error = g_sort_flow_auto.last_error;
    rt_mutex_release(g_sort_flow_auto.lock);

    software_gate_app_get_snapshot(&gate_snapshot);
    rt_kprintf("[flow_auto] active=%u state=%s handled=%lu last_gate=%lu gate=%lu err=%d settle=%lu detect_timeout=%lu\n",
               (unsigned int)active,
               sort_flow_auto_state_name(state),
               (unsigned long)handled_count,
               (unsigned long)last_gate_trigger_count,
               (unsigned long)gate_snapshot.trigger_count,
               last_error,
               (unsigned long)settle_ms,
               (unsigned long)detect_timeout_ms);
    rt_kprintf("[flow_auto] pc wait=%u req=%lu timeout=%lu ok=%lu to=%lu fallback=%lu\n",
               (unsigned int)pc_waiting,
               (unsigned long)pc_req_id,
               (unsigned long)pc_timeout_ms,
               (unsigned long)pc_accepted_count,
               (unsigned long)pc_timeout_count,
               (unsigned long)pc_fallback_count);
    rt_kprintf("[flow_auto] flow active=%u busy=%u ok=%lu ng=%lu score=%u\n",
               (unsigned int)g_sort_flow.active,
               (unsigned int)g_sort_flow.busy,
               (unsigned long)g_sort_flow.ok_count,
               (unsigned long)g_sort_flow.ng_count,
               (unsigned int)g_sort_flow.last_score_x10);
    sort_flow_queue_print_status();
    return RT_EOK;
}

static int sort_flow_align_cmd(int argc, char **argv)
{
    uint32_t value;
    uint32_t value2;
    int ret;

    if (argc == 1)
    {
        sort_flow_align_print();
        return RT_EOK;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            g_sort_flow_align.enabled = RT_TRUE;
            sort_flow_align_print();
            return RT_EOK;
        }
        if (strcmp(argv[1], "off") == 0)
        {
            g_sort_flow_align.enabled = RT_FALSE;
            sort_flow_align_print();
            return RT_EOK;
        }
    }

    if ((argc == 3) && (strcmp(argv[1], "target") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 239U))
        {
            rt_kprintf("[flow_align] invalid target, range 0..239\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.target_y = (uint16_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "tol") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 80U))
        {
            rt_kprintf("[flow_align] invalid tol, range 0..80\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.tolerance_px = (uint16_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "gain") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 200U))
        {
            rt_kprintf("[flow_align] invalid gain, range 0..200 pulse/px\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.pulse_per_px = (uint16_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "rpm") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value == 0U) || (value > 60U))
        {
            rt_kprintf("[flow_align] invalid rpm, range 1..60\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.rpm = (uint16_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "acc") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 255U))
        {
            rt_kprintf("[flow_align] invalid acc, range 0..255\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.acc = (uint8_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 4) && (strcmp(argv[1], "pulses") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if (ret == RT_EOK)
        {
            ret = sort_demo_parse_u32_arg(argv[3], &value2);
        }
        if ((ret != RT_EOK) || (value == 0U) || (value > value2) || (value2 > 3000U))
        {
            rt_kprintf("[flow_align] invalid pulses, require 1 <= min <= max <= 3000\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.min_pulses = value;
        g_sort_flow_align.max_pulses = value2;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "iter") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 10U))
        {
            rt_kprintf("[flow_align] invalid iter, range 0..10\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.max_iter = (uint8_t)value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "settle") == 0))
    {
        ret = sort_demo_parse_u32_arg(argv[2], &value);
        if ((ret != RT_EOK) || (value > 3000U))
        {
            rt_kprintf("[flow_align] invalid settle, range 0..3000 ms\n");
            return -RT_EINVAL;
        }
        g_sort_flow_align.settle_ms = value;
        sort_flow_align_print();
        return RT_EOK;
    }

    if ((argc == 3) && (strcmp(argv[1], "dir") == 0))
    {
        if (strcmp(argv[2], "normal") == 0)
        {
            g_sort_flow_align.reverse_dir = RT_FALSE;
            sort_flow_align_print();
            return RT_EOK;
        }
        if (strcmp(argv[2], "reverse") == 0)
        {
            g_sort_flow_align.reverse_dir = RT_TRUE;
            sort_flow_align_print();
            return RT_EOK;
        }
    }

    sort_flow_align_print();
    return -RT_EINVAL;
}

MSH_CMD_EXPORT_ALIAS(sort_demo_once_cmd, demo_sort_once, move conveyor to arm then trigger robot arm once);
MSH_CMD_EXPORT_ALIAS(sort_flow_start_cmd, sort_flow_start, start continuous conveyor sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_stop_cmd, sort_flow_stop, stop continuous conveyor sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_status_cmd, sort_flow_status, show continuous sort flow status);
MSH_CMD_EXPORT_ALIAS(sort_flow_comp_cmd, sort_flow_comp, set transfer pulse compensation);
MSH_CMD_EXPORT_ALIAS(sort_flow_queue_cmd, sort_flow_queue, show or tune queued NG pick flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_object_cmd, sort_flow_object, simulate object entered ROI for sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_auto_start_cmd, sort_flow_auto_start, start software-gate automatic sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_auto_stop_cmd, sort_flow_auto_stop, stop software-gate automatic sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_auto_status_cmd, sort_flow_auto_status, show software-gate automatic sort flow);
MSH_CMD_EXPORT_ALIAS(sort_flow_align_cmd, sort_flow_align, tune object centering before auto detection);

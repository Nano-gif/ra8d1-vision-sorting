/*
 * 分拣演示与总线操作：传送带启停/定位、机械臂就绪检查、动作下发确认与完成等待。
 */
#include "sort_demo_internal.h"

static uint16_t s_sort_demo_seq = 100U;
sort_flow_context_t g_sort_flow;
sort_flow_auto_context_t g_sort_flow_auto;
sort_flow_ng_queue_t g_sort_flow_ng_queue;
sort_flow_align_config_t g_sort_flow_align = {
    SORT_FLOW_ALIGN_DEFAULT_ENABLED,
    RT_FALSE,
    SORT_FLOW_ALIGN_TARGET_Y,
    SORT_FLOW_ALIGN_TOLERANCE_PX,
    SORT_FLOW_ALIGN_PULSE_PER_PX,
    SORT_FLOW_ALIGN_MIN_PULSES,
    SORT_FLOW_ALIGN_MAX_PULSES,
    SORT_FLOW_ALIGN_RPM,
    SORT_FLOW_ALIGN_ACC,
    SORT_FLOW_ALIGN_MAX_ITER,
    SORT_FLOW_ALIGN_SETTLE_MS,
};
int32_t g_sort_flow_position_scale_raw = SORT_FLOW_POSITION_SCALE_RAW;
int32_t g_sort_flow_arrive_tolerance_raw = SORT_FLOW_POSITION_LEAD_RAW;
int32_t g_sort_flow_queue_guard_raw = SORT_FLOW_QUEUE_GUARD_RAW;
rt_bool_t g_sort_flow_queue_guard_auto = RT_TRUE;
uint32_t g_sort_flow_queue_guard_log_id = 0U;

/* 本地命令序号兜底生成（跳过 0）。 */
uint16_t sort_demo_next_seq(void)
{
    uint16_t seq = s_sort_demo_seq;

    s_sort_demo_seq++;
    if (s_sort_demo_seq == 0U)
    {
        s_sort_demo_seq = 100U;
    }

    return seq;
}

static const char *sort_demo_result_name(uint16_t result)
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

/* 打印总线命令重试日志。 */
void sort_demo_log_retry(const char *op, uint8_t attempt, int ret)
{
    rt_kprintf("[sort] %s retry %u/%u after ret=%d\n",
               op,
               (unsigned int)attempt,
               (unsigned int)SORT_DEMO_BUS_CMD_RETRIES,
               ret);
}

/* 以当前配置的转速正向连续运行传送带（带重试）。 */
int sort_flow_start_conveyor(void)
{
    uint16_t rpm = device_comm_app_get_conveyor_rpm();
    uint8_t acc = device_comm_app_get_conveyor_acc();
    uint8_t attempt;
    int ret = -RT_ERROR;

    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = zdt_motor_speed(SORT_DEMO_ZDT_ADDR, ZDT_MOTOR_DIR_CW, rpm, acc, 0U);
        if (ret == RT_EOK)
        {
            break;
        }
        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("conveyor run", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }

    rt_kprintf("[flow] conveyor run rpm=%u acc=%u ret=%d\n",
               (unsigned int)rpm,
               (unsigned int)acc,
               ret);
    return ret;
}

int sort_flow_stop_conveyor(void)
{
    /*
     * 幂等快速停车：zdt_motor_stop_fast 内部以短超时连发多帧，总裕量约 170ms，
     * 避免工件在停车延迟期间冲出画面；一帧回执都没收到时再补发一轮。
     */
    int ret = zdt_motor_stop_fast(SORT_DEMO_ZDT_ADDR, 0U);
    if (ret != RT_EOK)
    {
        sort_demo_log_retry("conveyor stop", 1U, ret);
        rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        ret = zdt_motor_stop_fast(SORT_DEMO_ZDT_ADDR, 0U);
    }

    rt_kprintf("[flow] conveyor stop ret=%d\n", ret);
    return ret;
}

/* 向检测模块注入模拟检测结果（用于手动流程演示）。 */
int sort_flow_inject_result(rt_bool_t ng, uint16_t score_x10)
{
    uint16_t mean_x10 = ng ? score_x10 : (uint16_t)(score_x10 / 2U);
    uint8_t hot_cells = ng ? 3U : 0U;

    return detect_app_inject_result(score_x10,
                                    mean_x10,
                                    hot_cells,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    RT_FALSE);
}

/* 读取机械臂基础状态，确认就绪且无 HMI 占用、故障或急停。 */
int sort_demo_check_arm_ready(void)
{
    robot_arm_base_status_t status;
    uint8_t attempt;
    int ret;

    ret = -RT_ERROR;
    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = robot_arm_read_base_status(SORT_DEMO_ARM_ADDR, &status);
        if (ret == RT_EOK)
        {
            break;
        }
        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("arm status", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }
    if (ret != RT_EOK)
    {
        rt_kprintf("[sort] arm status read failed ret=%d\n", ret);
        return ret;
    }

    rt_kprintf("[sort] arm flags=0x%04x ready=%u hmi=%u error=%u estop=%u\n",
               (unsigned int)status.status_flags,
               (unsigned int)((status.status_flags & SORT_DEMO_STATUS_READY) != 0U),
               (unsigned int)((status.status_flags & SORT_DEMO_STATUS_HMI_ACTIVE) != 0U),
               (unsigned int)((status.status_flags & SORT_DEMO_STATUS_ERROR) != 0U),
               (unsigned int)((status.status_flags & SORT_DEMO_STATUS_ESTOP) != 0U));

    if ((status.status_flags & SORT_DEMO_STATUS_READY) == 0U)
    {
        return -RT_EBUSY;
    }
    if ((status.status_flags & SORT_DEMO_STATUS_HMI_ACTIVE) != 0U)
    {
        return -RT_EBUSY;
    }
    if ((status.status_flags & (SORT_DEMO_STATUS_ERROR | SORT_DEMO_STATUS_ESTOP)) != 0U)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 轮询机械臂动作状态直到完成、出错或超时。 */
int sort_demo_wait_arm_done(void)
{
    robot_arm_action_status_t action;
    uint32_t elapsed_ms = 0U;
    uint8_t saw_playing = 0U;
    uint8_t missed_reads = 0U;
    int ret;

    while (elapsed_ms <= SORT_DEMO_ARM_TIMEOUT_MS)
    {
        ret = robot_arm_read_action_status(SORT_DEMO_ARM_ADDR, &action);
        if (ret != RT_EOK)
        {
            missed_reads++;
            rt_kprintf("[sort] arm action read miss %u/%u ret=%d\n",
                       (unsigned int)missed_reads,
                       (unsigned int)SORT_DEMO_READ_MISS_LIMIT,
                       ret);
            if (missed_reads >= SORT_DEMO_READ_MISS_LIMIT)
            {
                rt_kprintf("[sort] arm action read failed ret=%d\n", ret);
                return ret;
            }

            rt_thread_mdelay(SORT_DEMO_ARM_POLL_MS);
            elapsed_ms += SORT_DEMO_ARM_POLL_MS;
            continue;
        }
        missed_reads = 0U;

        rt_kprintf("[sort] arm action state=%u step=%u/%u progress=%u/1000\n",
                   (unsigned int)action.state,
                   (unsigned int)action.current_step,
                   (unsigned int)action.step_count,
                   (unsigned int)action.playback_progress);

        if (action.state == SORT_DEMO_ACTION_PLAYING)
        {
            saw_playing = 1U;
        }
        else if (action.state == SORT_DEMO_ACTION_DONE)
        {
            return RT_EOK;
        }
        else if ((action.state == SORT_DEMO_ACTION_READY) && (saw_playing != 0U))
        {
            return RT_EOK;
        }
        else if (action.state == SORT_DEMO_ACTION_ERROR)
        {
            return -RT_ERROR;
        }

        rt_thread_mdelay(SORT_DEMO_ARM_POLL_MS);
        elapsed_ms += SORT_DEMO_ARM_POLL_MS;
    }

    return -RT_ETIMEOUT;
}

/* 下发 PLAY 命令并读回 last-command 确认被接受。 */
int sort_demo_play_arm_with_confirm(uint16_t seq, uint16_t *out_result)
{
    uint16_t confirm_seq = 0U;
    uint16_t confirm_code = 0U;
    uint16_t confirm_result = 0U;
    uint8_t attempt;
    uint8_t confirm_attempt;
    int ret;

    if (out_result != RT_NULL)
    {
        *out_result = ROBOT_ARM_RESULT_NONE;
    }

    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = robot_arm_send_command(SORT_DEMO_ARM_ADDR, ROBOT_ARM_COMMAND_PLAY, seq);
        rt_kprintf("[sort] arm play seq=%u ret=%d\n", (unsigned int)seq, ret);
        if (ret == RT_EOK)
        {
            break;
        }

        /*
         * 写超时不代表机械臂没收到命令：重试前先读回 last-command，
         * 若首帧已被接受则不再重复下发 PLAY。
         */
        for (confirm_attempt = 0U; confirm_attempt <= SORT_DEMO_BUS_CMD_RETRIES; confirm_attempt++)
        {
            int confirm_ret = robot_arm_read_last_command(SORT_DEMO_ARM_ADDR,
                                                          &confirm_seq,
                                                          &confirm_code,
                                                          &confirm_result);
            if (confirm_ret == RT_EOK)
            {
                if ((confirm_seq == seq) &&
                    (confirm_result == ROBOT_ARM_RESULT_ACCEPTED))
                {
                    rt_kprintf("[sort] arm play seq=%u accepted after write timeout\n",
                               (unsigned int)seq);
                    if (out_result != RT_NULL)
                    {
                        *out_result = confirm_result;
                    }
                    return RT_EOK;
                }
                break;
            }

            if (confirm_attempt < SORT_DEMO_BUS_CMD_RETRIES)
            {
                sort_demo_log_retry("arm confirm-after-timeout",
                                    (uint8_t)(confirm_attempt + 1U),
                                    confirm_ret);
                rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
            }
        }

        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("arm play", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = -RT_ERROR;
    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = robot_arm_read_last_command(SORT_DEMO_ARM_ADDR, &confirm_seq, &confirm_code, &confirm_result);
        if (ret == RT_EOK)
        {
            break;
        }
        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("arm confirm", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }
    if (ret != RT_EOK)
    {
        rt_kprintf("[sort] arm confirm read failed ret=%d\n", ret);
        return ret;
    }
    if (out_result != RT_NULL)
    {
        *out_result = confirm_result;
    }

    rt_kprintf("[sort] arm confirm seq=%u code=0x%04x result=%u(%s)\n",
               (unsigned int)confirm_seq,
               (unsigned int)confirm_code,
               (unsigned int)confirm_result,
               sort_demo_result_name(confirm_result));

    if ((confirm_seq != seq) || (confirm_result != ROBOT_ARM_RESULT_ACCEPTED))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 等待传送带定位运动先离开再回到到位状态。 */
static int sort_demo_wait_conveyor_reached(void)
{
    zdt_motor_status_t status;
    uint32_t elapsed_ms = 0U;
    rt_bool_t saw_moving = RT_FALSE;
    uint8_t missed_reads = 0U;
    int ret;

    rt_thread_mdelay(100U);
    while (elapsed_ms <= SORT_DEMO_ZDT_TIMEOUT_MS)
    {
        memset(&status, 0, sizeof(status));
        ret = zdt_motor_read_flags(SORT_DEMO_ZDT_ADDR, &status);
        if (ret != RT_EOK)
        {
            missed_reads++;
            rt_kprintf("[sort] conveyor flags read miss %u/%u ret=%d\n",
                       (unsigned int)missed_reads,
                       (unsigned int)SORT_DEMO_READ_MISS_LIMIT,
                       ret);
            if (missed_reads >= SORT_DEMO_READ_MISS_LIMIT)
            {
                rt_kprintf("[sort] conveyor flags read failed ret=%d\n", ret);
                return ret;
            }

            rt_thread_mdelay(SORT_DEMO_ZDT_POLL_MS);
            elapsed_ms += SORT_DEMO_ZDT_POLL_MS;
            continue;
        }
        missed_reads = 0U;

        rt_kprintf("[sort] conveyor flags=0x%02x reached=%u\n",
                   (unsigned int)status.motor_flags,
                   (unsigned int)((status.motor_flags & SORT_DEMO_ZDT_REACHED) != 0U));

        if ((status.motor_flags & SORT_DEMO_ZDT_REACHED) == 0U)
        {
            saw_moving = RT_TRUE;
        }
        else if (saw_moving == RT_TRUE)
        {
            return RT_EOK;
        }

        rt_thread_mdelay(SORT_DEMO_ZDT_POLL_MS);
        elapsed_ms += SORT_DEMO_ZDT_POLL_MS;
    }

    return -RT_ETIMEOUT;
}

/* 短距离对位用：直接等待到位标志，带读失败容忍。 */
int sort_flow_wait_conveyor_reached_fast(uint32_t timeout_ms)
{
    zdt_motor_status_t status;
    uint32_t elapsed_ms = 0U;
    uint8_t missed_reads = 0U;
    int ret;

    rt_thread_mdelay(SORT_FLOW_ALIGN_START_DELAY_MS);
    elapsed_ms += SORT_FLOW_ALIGN_START_DELAY_MS;

    while (elapsed_ms <= timeout_ms)
    {
        memset(&status, 0, sizeof(status));
        ret = zdt_motor_read_flags(SORT_DEMO_ZDT_ADDR, &status);
        if (ret != RT_EOK)
        {
            missed_reads++;
            rt_kprintf("[flow_align] flags read miss %u/%u ret=%d\n",
                       (unsigned int)missed_reads,
                       (unsigned int)SORT_DEMO_READ_MISS_LIMIT,
                       ret);
            if (missed_reads >= SORT_DEMO_READ_MISS_LIMIT)
            {
                return ret;
            }
        }
        else
        {
            missed_reads = 0U;
            if ((status.motor_flags & SORT_DEMO_ZDT_REACHED) != 0U)
            {
                return RT_EOK;
            }
        }

        rt_thread_mdelay(SORT_DEMO_ZDT_POLL_MS);
        elapsed_ms += SORT_DEMO_ZDT_POLL_MS;
    }

    return -RT_ETIMEOUT;
}

/* 传送带相对定位运动（带重试）。 */
int sort_flow_align_move(zdt_motor_dir_t dir,
                         uint16_t rpm,
                         uint8_t acc,
                         uint32_t pulses)
{
    uint8_t attempt;
    int ret = -RT_ERROR;

    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = zdt_motor_position(SORT_DEMO_ZDT_ADDR, dir, rpm, acc, pulses, 0U, 0U);
        if (ret == RT_EOK)
        {
            break;
        }

        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("align pos", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }

    return ret;
}

/* 演示一次：传送带定位送到抓取位后触发机械臂动作。 */
int sort_demo_run_once(uint16_t seq, uint16_t rpm, uint32_t pulses, uint8_t acc)
{
    uint16_t arm_result = ROBOT_ARM_RESULT_NONE;
    uint8_t attempt;
    int ret;

    rt_kprintf("[sort] once seq=%u rpm=%u pulses=%u acc=%u\n",
               (unsigned int)seq,
               (unsigned int)rpm,
               (unsigned int)pulses,
               (unsigned int)acc);

    ret = sort_demo_check_arm_ready();
    if (ret != RT_EOK)
    {
        rt_kprintf("[sort] abort: arm not ready ret=%d\n", ret);
        return ret;
    }

    ret = -RT_ERROR;
    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        ret = zdt_motor_position(SORT_DEMO_ZDT_ADDR, ZDT_MOTOR_DIR_CW, rpm, acc, pulses, 0U, 0U);
        if (ret == RT_EOK)
        {
            break;
        }
        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("conveyor pos", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }
    rt_kprintf("[sort] conveyor pos ret=%d\n", ret);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = sort_demo_wait_conveyor_reached();
    rt_kprintf("[sort] conveyor reached ret=%d\n", ret);
    if (ret != RT_EOK)
    {
        return ret;
    }

    rt_thread_mdelay(100U);

    ret = sort_demo_play_arm_with_confirm(seq, &arm_result);
    if ((ret != RT_EOK) && (arm_result == ROBOT_ARM_RESULT_DUPLICATE))
    {
        seq = device_comm_app_next_arm_seq();
        if (seq == 0U)
        {
            seq = sort_demo_next_seq();
        }
        rt_kprintf("[sort] duplicate command ignored, retry arm play seq=%u\n", (unsigned int)seq);
        ret = sort_demo_play_arm_with_confirm(seq, &arm_result);
    }
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = sort_demo_wait_arm_done();
    rt_kprintf("[sort] once done ret=%d\n", ret);
    return ret;
}

/* 使用当前传送带配置与自动序号执行一次演示。 */
int sort_demo_run_auto(uint32_t pulses)
{
    uint16_t rpm = device_comm_app_get_conveyor_rpm();
    uint8_t acc = device_comm_app_get_conveyor_acc();
    uint16_t seq = device_comm_app_next_arm_seq();

    if (seq == 0U)
    {
        seq = sort_demo_next_seq();
    }
    return sort_demo_run_once(seq, rpm, pulses, acc);
}

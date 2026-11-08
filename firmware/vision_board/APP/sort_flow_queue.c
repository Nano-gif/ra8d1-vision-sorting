/*
 * NG 工件队列：输送位置跟踪、到位判定、防撞保护距离计算与到位抓取。
 */
#include "sort_demo_internal.h"

static int32_t s_sort_transfer_comp_pulses = 0;

static int sort_flow_pick_align_to_target(const sort_flow_ng_item_t *item);

/* 设置检测位到抓取位的输送脉冲补偿量。 */
int sort_flow_set_transfer_comp_pulses(int32_t comp_pulses)
{
    if ((comp_pulses < SORT_FLOW_TRANSFER_COMP_MIN) ||
        (comp_pulses > SORT_FLOW_TRANSFER_COMP_MAX))
    {
        return -RT_EINVAL;
    }

    s_sort_transfer_comp_pulses = comp_pulses;
    return RT_EOK;
}

int32_t sort_flow_get_transfer_comp_pulses(void)
{
    return s_sort_transfer_comp_pulses;
}

uint32_t sort_flow_get_transfer_base_pulses(void)
{
    return SORT_DEMO_TRANSFER_BASE_PULSES;
}

/* 实际输送脉冲 = 基准 + 补偿，并限幅。 */
uint32_t sort_flow_get_transfer_pulses(void)
{
    int32_t pulses = (int32_t)SORT_DEMO_TRANSFER_BASE_PULSES + s_sort_transfer_comp_pulses;

    if (pulses < (int32_t)SORT_FLOW_TRANSFER_MIN_PULSES)
    {
        return SORT_FLOW_TRANSFER_MIN_PULSES;
    }
    if (pulses > (int32_t)SORT_FLOW_TRANSFER_MAX_PULSES)
    {
        return SORT_FLOW_TRANSFER_MAX_PULSES;
    }

    return (uint32_t)pulses;
}

/* 读取传送带编码器位置（带重试），并顺带估算输送速度。 */
int sort_flow_read_conveyor_position(int32_t *out_position)
{
    zdt_motor_status_t status;
    uint8_t attempt;
    int ret = -RT_ERROR;

    if (out_position == RT_NULL)
    {
        return -RT_EINVAL;
    }

    for (attempt = 0U; attempt <= SORT_DEMO_BUS_CMD_RETRIES; attempt++)
    {
        memset(&status, 0, sizeof(status));
        ret = zdt_motor_read_position(SORT_DEMO_ZDT_ADDR, &status);
        if (ret == RT_EOK)
        {
            rt_tick_t now_tick = rt_tick_get();

            if ((g_sort_flow_ng_queue.position_valid == RT_TRUE) &&
                (g_sort_flow_ng_queue.last_position_tick != 0))
            {
                uint32_t elapsed_ms = sort_flow_auto_ticks_to_ms(now_tick - g_sort_flow_ng_queue.last_position_tick);
                int32_t delta_raw = status.position_raw - g_sort_flow_ng_queue.last_position;

                if ((delta_raw > 0) && (elapsed_ms > 0U) && (elapsed_ms < 5000U))
                {
                    uint64_t speed = ((uint64_t)(uint32_t)delta_raw * 1000ULL) / elapsed_ms;
                    g_sort_flow_ng_queue.speed_raw_per_s = (speed > UINT32_MAX) ? UINT32_MAX : (uint32_t)speed;
                }
            }

            *out_position = status.position_raw;
            g_sort_flow_ng_queue.last_position = status.position_raw;
            g_sort_flow_ng_queue.last_position_tick = now_tick;
            g_sort_flow_ng_queue.position_valid = RT_TRUE;
            (void)sort_flow_queue_get_guard_raw();
            return RT_EOK;
        }

        if (attempt < SORT_DEMO_BUS_CMD_RETRIES)
        {
            sort_demo_log_retry("queue position", (uint8_t)(attempt + 1U), ret);
            rt_thread_mdelay(SORT_DEMO_BUS_RETRY_GAP_MS);
        }
    }

    return ret;
}

/* 输送脉冲换算为编码器位置增量。 */
static int32_t sort_flow_transfer_raw_delta(uint32_t transfer_pulses)
{
    int64_t delta = (int64_t)transfer_pulses * (int64_t)g_sort_flow_position_scale_raw;

    if (delta > INT32_MAX)
    {
        return INT32_MAX;
    }
    if (delta < INT32_MIN)
    {
        return INT32_MIN;
    }
    return (int32_t)delta;
}

int32_t sort_flow_abs_i32(int32_t value)
{
    if (value == INT32_MIN)
    {
        return INT32_MAX;
    }
    return (value < 0) ? -value : value;
}

static int32_t sort_flow_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static int32_t sort_flow_queue_transfer_raw_abs(void)
{
    return sort_flow_abs_i32(sort_flow_transfer_raw_delta(sort_flow_get_transfer_pulses()));
}

/* 计算到位前的保护距离：自动模式按速度与周期动态估算，再限制在上下限内。 */
int32_t sort_flow_queue_get_guard_raw(void)
{
    int32_t transfer_raw = sort_flow_queue_transfer_raw_abs();
    int32_t min_raw;
    int32_t max_raw;
    int32_t margin_raw;
    int32_t guard_raw;

    if (transfer_raw <= 0)
    {
        transfer_raw = sort_flow_abs_i32((int32_t)SORT_DEMO_TRANSFER_BASE_PULSES * SORT_FLOW_POSITION_SCALE_RAW);
    }

    min_raw = transfer_raw / SORT_FLOW_QUEUE_GUARD_MIN_DIV;
    max_raw = (int32_t)(((int64_t)transfer_raw * SORT_FLOW_QUEUE_GUARD_MAX_NUM) /
                        SORT_FLOW_QUEUE_GUARD_MAX_DEN);
    margin_raw = transfer_raw / SORT_FLOW_QUEUE_GUARD_MARGIN_DIV;
    if (min_raw < (g_sort_flow_arrive_tolerance_raw + margin_raw))
    {
        min_raw = g_sort_flow_arrive_tolerance_raw + margin_raw;
    }
    if (max_raw < min_raw)
    {
        max_raw = min_raw;
    }

    if (g_sort_flow_queue_guard_auto == RT_TRUE)
    {
        uint32_t cycle_ms = (g_sort_flow_ng_queue.last_cycle_ms > 0U) ?
                            g_sort_flow_ng_queue.last_cycle_ms : SORT_FLOW_QUEUE_DEFAULT_CYCLE_MS;
        uint32_t speed_raw_per_s = g_sort_flow_ng_queue.speed_raw_per_s;

        if (speed_raw_per_s > 0U)
        {
            uint64_t dynamic_raw = ((uint64_t)speed_raw_per_s * cycle_ms) / 1000ULL;
            dynamic_raw += (uint32_t)g_sort_flow_arrive_tolerance_raw;
            dynamic_raw += (uint32_t)margin_raw;
            guard_raw = (dynamic_raw > INT32_MAX) ? INT32_MAX : (int32_t)dynamic_raw;
        }
        else
        {
            guard_raw = g_sort_flow_queue_guard_raw;
        }
    }
    else
    {
        guard_raw = g_sort_flow_queue_guard_raw;
    }

    guard_raw = sort_flow_clamp_i32(guard_raw, min_raw, max_raw);
    g_sort_flow_ng_queue.guard_min_raw = min_raw;
    g_sort_flow_ng_queue.guard_max_raw = max_raw;
    g_sort_flow_ng_queue.guard_current_raw = guard_raw;
    return guard_raw;
}

/* 记录一次检测周期耗时（指数平滑），用于保护距离估算。 */
void sort_flow_queue_note_cycle_ms(uint32_t cycle_ms)
{
    if (cycle_ms == 0U)
    {
        return;
    }
    if (cycle_ms > SORT_FLOW_AUTO_DETECT_TIMEOUT_MS)
    {
        cycle_ms = SORT_FLOW_AUTO_DETECT_TIMEOUT_MS;
    }

    if (g_sort_flow_ng_queue.last_cycle_ms == 0U)
    {
        g_sort_flow_ng_queue.last_cycle_ms = cycle_ms;
    }
    else
    {
        g_sort_flow_ng_queue.last_cycle_ms = ((g_sort_flow_ng_queue.last_cycle_ms * 3U) + cycle_ms) / 4U;
    }
    (void)sort_flow_queue_get_guard_raw();
}

/* 门限是否有尚未触发的候选工件。 */
rt_bool_t sort_flow_gate_candidate_pending(const software_gate_snapshot_t *gate_snapshot)
{
    if (gate_snapshot == RT_NULL)
    {
        return RT_FALSE;
    }

    if ((gate_snapshot->state == SOFTWARE_GATE_STATE_SEEN) &&
        (gate_snapshot->candidate == RT_TRUE) &&
        ((gate_snapshot->entry_allowed == RT_TRUE) ||
         (gate_snapshot->config.entry_filter_enabled != RT_TRUE)))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

/* 清空 NG 队列；reset_stats 为假时保留累计统计。 */
void sort_flow_queue_reset(rt_bool_t reset_stats)
{
    uint32_t queued_count = g_sort_flow_ng_queue.queued_count;
    uint32_t picked_count = g_sort_flow_ng_queue.picked_count;
    uint32_t dropped_count = g_sort_flow_ng_queue.dropped_count;

    memset(&g_sort_flow_ng_queue, 0, sizeof(g_sort_flow_ng_queue));
    g_sort_flow_ng_queue.next_id = 1U;
    g_sort_flow_ng_queue.last_cycle_ms = SORT_FLOW_QUEUE_DEFAULT_CYCLE_MS;
    g_sort_flow_queue_guard_log_id = 0U;
    (void)sort_flow_queue_get_guard_raw();
    if (reset_stats != RT_TRUE)
    {
        g_sort_flow_ng_queue.queued_count = queued_count;
        g_sort_flow_ng_queue.picked_count = picked_count;
        g_sort_flow_ng_queue.dropped_count = dropped_count;
    }
}

rt_bool_t sort_flow_queue_peek(sort_flow_ng_item_t *out_item)
{
    if (g_sort_flow_ng_queue.count == 0U)
    {
        return RT_FALSE;
    }

    if (out_item != RT_NULL)
    {
        *out_item = g_sort_flow_ng_queue.items[g_sort_flow_ng_queue.head];
    }
    return RT_TRUE;
}

void sort_flow_queue_pop(void)
{
    if (g_sort_flow_ng_queue.count == 0U)
    {
        return;
    }

    memset(&g_sort_flow_ng_queue.items[g_sort_flow_ng_queue.head],
           0,
           sizeof(g_sort_flow_ng_queue.items[g_sort_flow_ng_queue.head]));
    g_sort_flow_ng_queue.head = (uint8_t)((g_sort_flow_ng_queue.head + 1U) % SORT_FLOW_NG_QUEUE_SIZE);
    g_sort_flow_ng_queue.count--;
}

/* 记录当前位置并把 NG 工件压入队列，目标位置 = 当前位置 + 输送增量。 */
int sort_flow_queue_push_ng(uint16_t score_x10, int16_t angle_x10)
{
    sort_flow_ng_item_t *item;
    uint8_t tail;
    int32_t position;
    uint32_t transfer_pulses;
    int ret;

    if (g_sort_flow_ng_queue.count >= SORT_FLOW_NG_QUEUE_SIZE)
    {
        g_sort_flow_ng_queue.dropped_count++;
        rt_kprintf("[flow_queue] full, cannot queue ng score=%u\n", (unsigned int)score_x10);
        return -RT_EFULL;
    }

    ret = sort_flow_read_conveyor_position(&position);
    if (ret != RT_EOK)
    {
        rt_kprintf("[flow_queue] read position failed ret=%d\n", ret);
        return ret;
    }

    transfer_pulses = sort_flow_get_transfer_pulses();
    tail = (uint8_t)((g_sort_flow_ng_queue.head + g_sort_flow_ng_queue.count) % SORT_FLOW_NG_QUEUE_SIZE);
    item = &g_sort_flow_ng_queue.items[tail];
    memset(item, 0, sizeof(*item));
    item->id = g_sort_flow_ng_queue.next_id++;
    if (g_sort_flow_ng_queue.next_id == 0U)
    {
        g_sort_flow_ng_queue.next_id = 1U;
    }
    item->score_x10 = score_x10;
    item->angle_x10 = angle_x10;
    item->transfer_pulses = transfer_pulses;
    item->detect_pos = position;
    item->target_pos = position + sort_flow_transfer_raw_delta(transfer_pulses);

    g_sort_flow_ng_queue.count++;
    g_sort_flow_ng_queue.queued_count++;
    rt_kprintf("[flow_queue] push id=%lu score=%u angle_x10=%d detect=%d target=%d pulses=%lu scale=%d lead=%d guard=%s:%d count=%u/%u\n",
               (unsigned long)item->id,
               (unsigned int)item->score_x10,
               (int)item->angle_x10,
               (int)item->detect_pos,
               (int)item->target_pos,
               (unsigned long)item->transfer_pulses,
               (int)g_sort_flow_position_scale_raw,
               (int)g_sort_flow_arrive_tolerance_raw,
               (g_sort_flow_queue_guard_auto == RT_TRUE) ? "auto" : "fixed",
               (int)sort_flow_queue_get_guard_raw(),
               (unsigned int)g_sort_flow_ng_queue.count,
               (unsigned int)SORT_FLOW_NG_QUEUE_SIZE);
    return RT_EOK;
}

/* 工件是否已进入抓取到位容差。 */
rt_bool_t sort_flow_queue_item_arrived(const sort_flow_ng_item_t *item,
                                       int32_t current_position,
                                       int32_t *out_remaining)
{
    int32_t remaining;

    if (item == RT_NULL)
    {
        return RT_FALSE;
    }

    remaining = item->target_pos - current_position;
    if (out_remaining != RT_NULL)
    {
        *out_remaining = remaining;
    }

    return (remaining <= g_sort_flow_arrive_tolerance_raw) ? RT_TRUE : RT_FALSE;
}

/* 工件是否处于到位前的保护区间（此时暂停处理新的门限触发）。 */
rt_bool_t sort_flow_queue_item_guarded(const sort_flow_ng_item_t *item,
                                       int32_t current_position,
                                       int32_t *out_remaining)
{
    int32_t remaining;

    if (item == RT_NULL)
    {
        return RT_FALSE;
    }

    remaining = item->target_pos - current_position;
    if (out_remaining != RT_NULL)
    {
        *out_remaining = remaining;
    }

    return ((remaining > g_sort_flow_arrive_tolerance_raw) &&
            (remaining <= sort_flow_queue_get_guard_raw())) ? RT_TRUE : RT_FALSE;
}

/*
 * 工件抓取具有 180° 对称性：把偏航角折叠到 (-90°,+90°] 并限幅到 J5 腕部范围。
 * 机械臂不可达角度的死区由机械臂侧处理，视觉板只下发归一化后的角度。
 */
static int16_t sort_flow_normalize_yaw_x10(int16_t yaw_x10)
{
    int32_t y = (int32_t)yaw_x10;

    while (y > 900)
    {
        y -= 1800;
    }
    while (y <= -900)
    {
        y += 1800;
    }
    if (y > 900)
    {
        y = 900;
    }
    if (y < -900)
    {
        y = -900;
    }

    return (int16_t)y;
}

/* 下发抓取偏航角参数。写失败不中止动作：机械臂侧 seq 不匹配时按“无补偿”降级执行。 */
static void sort_flow_send_yaw_param(const sort_flow_ng_item_t *item, uint16_t seq)
{
    int16_t norm_yaw = sort_flow_normalize_yaw_x10(item->angle_x10);
    int param_ret = robot_arm_set_task_param(SORT_DEMO_ARM_ADDR, norm_yaw, seq);

    rt_kprintf("[flow_queue] arm yaw set seq=%u yaw_x10=%d(raw=%d) ret=%d\n",
               (unsigned int)seq,
               (int)norm_yaw,
               (int)item->angle_x10,
               param_ret);
}

/* 工件到达抓取位后：检查机械臂就绪、精对位、下发偏航角并触发抓取动作，等待完成。 */
int sort_flow_run_arm_at_pick_position(const sort_flow_ng_item_t *item)
{
    uint16_t arm_result = ROBOT_ARM_RESULT_NONE;
    uint16_t seq = device_comm_app_next_arm_seq();
    int ret;

    if (item == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (seq == 0U)
    {
        seq = sort_demo_next_seq();
    }

    rt_kprintf("[flow_queue] pick id=%lu seq=%u score=%u angle_x10=%d target=%d\n",
               (unsigned long)item->id,
               (unsigned int)seq,
               (unsigned int)item->score_x10,
               (int)item->angle_x10,
               (int)item->target_pos);

    ret = sort_demo_check_arm_ready();
    if (ret != RT_EOK)
    {
        rt_kprintf("[flow_queue] abort pick: arm not ready ret=%d\n", ret);
        return ret;
    }

    ret = sort_flow_pick_align_to_target(item);
    if (ret != RT_EOK)
    {
        rt_kprintf("[flow_queue] abort pick: align failed ret=%d\n", ret);
        return ret;
    }

    sort_flow_send_yaw_param(item, seq);

    ret = sort_demo_play_arm_with_confirm(seq, &arm_result);
    if ((ret != RT_EOK) && (arm_result == ROBOT_ARM_RESULT_DUPLICATE))
    {
        seq = device_comm_app_next_arm_seq();
        if (seq == 0U)
        {
            seq = sort_demo_next_seq();
        }
        rt_kprintf("[flow_queue] duplicate ignored, retry seq=%u\n", (unsigned int)seq);
        sort_flow_send_yaw_param(item, seq);
        ret = sort_demo_play_arm_with_confirm(seq, &arm_result);
    }
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = sort_demo_wait_arm_done();
    rt_kprintf("[flow_queue] pick done id=%lu ret=%d\n",
               (unsigned long)item->id,
               ret);
    return ret;
}

/* 打印 NG 队列与位置跟踪状态。 */
void sort_flow_queue_print_status(void)
{
    sort_flow_ng_item_t item;
    int32_t position = 0;
    int32_t remaining = 0;
    int ret;
    rt_bool_t has_head;

    has_head = sort_flow_queue_peek(&item);
    ret = sort_flow_read_conveyor_position(&position);
    if ((ret == RT_EOK) && (has_head == RT_TRUE))
    {
        remaining = item.target_pos - position;
    }

    (void)sort_flow_queue_get_guard_raw();
    rt_kprintf("[flow_queue] count=%u/%u queued=%lu picked=%lu dropped=%lu scale=%d lead=%d guard=%s:%d min=%d max=%d pos=%s%d\n",
               (unsigned int)g_sort_flow_ng_queue.count,
               (unsigned int)SORT_FLOW_NG_QUEUE_SIZE,
               (unsigned long)g_sort_flow_ng_queue.queued_count,
               (unsigned long)g_sort_flow_ng_queue.picked_count,
               (unsigned long)g_sort_flow_ng_queue.dropped_count,
               (int)g_sort_flow_position_scale_raw,
               (int)g_sort_flow_arrive_tolerance_raw,
               (g_sort_flow_queue_guard_auto == RT_TRUE) ? "auto" : "fixed",
               (int)g_sort_flow_ng_queue.guard_current_raw,
               (int)g_sort_flow_ng_queue.guard_min_raw,
               (int)g_sort_flow_ng_queue.guard_max_raw,
               (ret == RT_EOK) ? "" : "err:",
               (ret == RT_EOK) ? (int)position : ret);
    rt_kprintf("[flow_queue] speed=%lu raw/s cycle=%lu ms\n",
               (unsigned long)g_sort_flow_ng_queue.speed_raw_per_s,
               (unsigned long)g_sort_flow_ng_queue.last_cycle_ms);

    if (has_head == RT_TRUE)
    {
        rt_kprintf("[flow_queue] head id=%lu score=%u angle_x10=%d detect=%d target=%d remaining=%d pulses=%lu\n",
                   (unsigned long)item.id,
                   (unsigned int)item.score_x10,
                   (int)item.angle_x10,
                   (int)item.detect_pos,
                   (int)item.target_pos,
                   (int)remaining,
                   (unsigned long)item.transfer_pulses);
    }
    else
    {
        rt_kprintf("[flow_queue] head=none\n");
    }
}

/* 使能/关闭软件门限，使能时重新布防。 */
int sort_flow_gate_set_enabled(rt_bool_t enabled)
{
    software_gate_config_t config;
    int ret;

    software_gate_app_get_config(&config);
    config.enabled = enabled ? RT_TRUE : RT_FALSE;
    ret = software_gate_app_set_config(&config);
    if ((ret == RT_EOK) && (enabled == RT_TRUE))
    {
        software_gate_app_rearm();
    }
    return ret;
}

/* 编码器位置误差换算为对位脉冲（向上取整，至少 1）。 */
static uint32_t sort_flow_pick_align_raw_to_pulses(uint32_t raw_abs)
{
    int32_t scale = sort_flow_abs_i32(g_sort_flow_position_scale_raw);
    uint32_t pulses;

    if (scale <= 0)
    {
        scale = SORT_FLOW_POSITION_SCALE_RAW;
    }

    pulses = (raw_abs + (uint32_t)scale - 1U) / (uint32_t)scale;
    return (pulses == 0U) ? 1U : pulses;
}

/* 抓取前按编码器位置闭环微调，使工件停在目标抓取位。 */
static int sort_flow_pick_align_to_target(const sort_flow_ng_item_t *item)
{
    uint8_t iter;
    int ret;

    if (item == RT_NULL)
    {
        return -RT_EINVAL;
    }

    for (iter = 0U; iter < SORT_FLOW_PICK_ALIGN_MAX_ITER; iter++)
    {
        int32_t position;
        int32_t error_raw;
        uint32_t abs_raw;
        uint32_t pulses;
        zdt_motor_dir_t dir;

        rt_thread_mdelay(SORT_FLOW_PICK_ALIGN_SETTLE_MS);
        ret = sort_flow_read_conveyor_position(&position);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_queue] pick align read failed id=%lu ret=%d\n",
                       (unsigned long)item->id,
                       ret);
            return ret;
        }

        error_raw = item->target_pos - position;
        abs_raw = (uint32_t)sort_flow_abs_i32(error_raw);
        if (abs_raw <= (uint32_t)SORT_FLOW_PICK_ALIGN_TOL_RAW)
        {
            rt_kprintf("[flow_queue] pick align ok id=%lu pos=%d target=%d err=%d tol=%d\n",
                       (unsigned long)item->id,
                       (int)position,
                       (int)item->target_pos,
                       (int)error_raw,
                       SORT_FLOW_PICK_ALIGN_TOL_RAW);
            return RT_EOK;
        }

        if (abs_raw > (uint32_t)SORT_FLOW_PICK_ALIGN_MAX_RAW)
        {
            rt_kprintf("[flow_queue] pick align error too large id=%lu pos=%d target=%d err=%d max=%d\n",
                       (unsigned long)item->id,
                       (int)position,
                       (int)item->target_pos,
                       (int)error_raw,
                       SORT_FLOW_PICK_ALIGN_MAX_RAW);
            return -RT_EINVAL;
        }

        dir = (error_raw >= 0) ? ZDT_MOTOR_DIR_CW : ZDT_MOTOR_DIR_CCW;
        pulses = sort_flow_pick_align_raw_to_pulses(abs_raw);
        rt_kprintf("[flow_queue] pick align iter=%u id=%lu pos=%d target=%d err=%d dir=%s pulses=%lu\n",
                   (unsigned int)(iter + 1U),
                   (unsigned long)item->id,
                   (int)position,
                   (int)item->target_pos,
                   (int)error_raw,
                   (dir == ZDT_MOTOR_DIR_CW) ? "cw" : "ccw",
                   (unsigned long)pulses);

        ret = sort_flow_align_move(dir,
                                   SORT_FLOW_PICK_ALIGN_RPM,
                                   SORT_FLOW_PICK_ALIGN_ACC,
                                   pulses);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_queue] pick align move failed id=%lu ret=%d\n",
                       (unsigned long)item->id,
                       ret);
            return ret;
        }

        ret = sort_flow_wait_conveyor_reached_fast(SORT_FLOW_PICK_ALIGN_TIMEOUT_MS);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_queue] pick align wait failed id=%lu ret=%d\n",
                       (unsigned long)item->id,
                       ret);
            return ret;
        }
    }

    {
        int32_t position;
        int32_t error_raw;
        uint32_t abs_raw;

        rt_thread_mdelay(SORT_FLOW_PICK_ALIGN_SETTLE_MS);
        ret = sort_flow_read_conveyor_position(&position);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_queue] pick align final read failed id=%lu ret=%d\n",
                       (unsigned long)item->id,
                       ret);
            return ret;
        }

        error_raw = item->target_pos - position;
        abs_raw = (uint32_t)sort_flow_abs_i32(error_raw);
        if (abs_raw <= (uint32_t)SORT_FLOW_PICK_ALIGN_TOL_RAW)
        {
            rt_kprintf("[flow_queue] pick align ok id=%lu pos=%d target=%d err=%d tol=%d\n",
                       (unsigned long)item->id,
                       (int)position,
                       (int)item->target_pos,
                       (int)error_raw,
                       SORT_FLOW_PICK_ALIGN_TOL_RAW);
            return RT_EOK;
        }

        rt_kprintf("[flow_queue] pick align not stable id=%lu pos=%d target=%d err=%d tol=%d max_iter=%u\n",
                   (unsigned long)item->id,
                   (int)position,
                   (int)item->target_pos,
                   (int)error_raw,
                   SORT_FLOW_PICK_ALIGN_TOL_RAW,
                   (unsigned int)SORT_FLOW_PICK_ALIGN_MAX_ITER);
    }
    return -RT_ETIMEOUT;
}

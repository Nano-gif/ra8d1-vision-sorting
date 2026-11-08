/*
 * 自动分拣流程：软件门限触发 -> 停带 -> 居中 -> 等待 PC/本地 AI 结果 -> NG 入队 -> 到位抓取。
 */
#include "sort_demo_internal.h"

static int sort_flow_auto_start_conveyor_if_active(void);
static void sort_flow_auto_thread_entry(void *parameter);

/* 停带后处理检测结果：NG 入队（或模拟模式直接抓取），更新统计并重启传送带。 */
static int sort_flow_handle_result_after_stop(rt_bool_t ng,
                                              uint16_t score_x10,
                                              int16_t angle_x10,
                                              rt_bool_t publish_mock)
{
    int ret;

    if (publish_mock == RT_TRUE)
    {
        ret = sort_flow_inject_result(ng, score_x10);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    if (ng == RT_TRUE)
    {
        if (publish_mock == RT_TRUE)
        {
            ret = sort_demo_run_auto(sort_flow_get_transfer_pulses());
        }
        else
        {
            ret = sort_flow_queue_push_ng(score_x10, angle_x10);
        }
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    if (ng == RT_TRUE)
    {
        g_sort_flow.ng_count++;
    }
    else
    {
        g_sort_flow.ok_count++;
    }

    if (publish_mock == RT_TRUE)
    {
        return sort_flow_start_conveyor();
    }

    return sort_flow_auto_start_conveyor_if_active();
}

/* 手动流程：提交一次检测结果并执行分拣。 */
int sort_flow_submit_result(rt_bool_t ng, uint16_t score_x10)
{
    int ret;

    if (g_sort_flow.active != RT_TRUE)
    {
        rt_kprintf("[flow] inactive, use sort_flow_start first\n");
        return -RT_ERROR;
    }
    if (g_sort_flow.busy == RT_TRUE)
    {
        rt_kprintf("[flow] busy\n");
        return -RT_EBUSY;
    }

    g_sort_flow.busy = RT_TRUE;
    g_sort_flow.last_ng = ng;
    g_sort_flow.last_score_x10 = score_x10;
    rt_kprintf("[flow] object=%s score=%u\n",
               ng ? "ng" : "ok",
               (unsigned int)score_x10);

    ret = sort_flow_stop_conveyor();
    if (ret == RT_EOK)
    {
        ret = sort_flow_handle_result_after_stop(ng, score_x10, 0, RT_TRUE);
    }

    g_sort_flow.last_error = ret;
    g_sort_flow.active = (ret == RT_EOK) ? RT_TRUE : RT_FALSE;
    g_sort_flow.busy = RT_FALSE;
    rt_kprintf("[flow] object done ret=%d active=%u\n", ret, (unsigned int)g_sort_flow.active);
    return ret;
}

/* 创建自动流程的锁、信号量与线程（仅首次）。 */
int sort_flow_auto_init(void)
{
    if (g_sort_flow_auto.ready == RT_TRUE)
    {
        return RT_EOK;
    }

    memset(&g_sort_flow_auto, 0, sizeof(g_sort_flow_auto));
    g_sort_flow_auto.state = SORT_FLOW_AUTO_IDLE;
    g_sort_flow_auto.settle_ms = SORT_FLOW_AUTO_SETTLE_MS;
    g_sort_flow_auto.detect_timeout_ms = SORT_FLOW_AUTO_DETECT_TIMEOUT_MS;
    g_sort_flow_auto.pc_timeout_ms = SORT_FLOW_AUTO_PC_TIMEOUT_MS;
    g_sort_flow_auto.pc_next_req_id = 1U;

    g_sort_flow_auto.lock = rt_mutex_create("flowauto", RT_IPC_FLAG_PRIO);
    if (g_sort_flow_auto.lock == RT_NULL)
    {
        return -RT_ERROR;
    }

    g_sort_flow_auto.sem = rt_sem_create("flowauto", 0, RT_IPC_FLAG_PRIO);
    if (g_sort_flow_auto.sem == RT_NULL)
    {
        rt_mutex_delete(g_sort_flow_auto.lock);
        g_sort_flow_auto.lock = RT_NULL;
        return -RT_ERROR;
    }

    g_sort_flow_auto.pc_sem = rt_sem_create("flowpc", 0, RT_IPC_FLAG_PRIO);
    if (g_sort_flow_auto.pc_sem == RT_NULL)
    {
        rt_sem_delete(g_sort_flow_auto.sem);
        rt_mutex_delete(g_sort_flow_auto.lock);
        g_sort_flow_auto.sem = RT_NULL;
        g_sort_flow_auto.lock = RT_NULL;
        return -RT_ERROR;
    }
    g_sort_flow_auto.thread = rt_thread_create("flowauto",
                                               sort_flow_auto_thread_entry,
                                               RT_NULL,
                                               SORT_FLOW_AUTO_THREAD_STACK,
                                               SORT_FLOW_AUTO_THREAD_PRIORITY,
                                               SORT_FLOW_AUTO_THREAD_TICK);
    if (g_sort_flow_auto.thread == RT_NULL)
    {
        rt_sem_delete(g_sort_flow_auto.pc_sem);
        rt_sem_delete(g_sort_flow_auto.sem);
        rt_mutex_delete(g_sort_flow_auto.lock);
        g_sort_flow_auto.pc_sem = RT_NULL;
        g_sort_flow_auto.sem = RT_NULL;
        g_sort_flow_auto.lock = RT_NULL;
        return -RT_ERROR;
    }
    rt_thread_startup(g_sort_flow_auto.thread);
    g_sort_flow_auto.ready = RT_TRUE;
    return RT_EOK;
}

/* 自动流程状态名。 */
const char *sort_flow_auto_state_name(sort_flow_auto_state_t state)
{
    switch (state)
    {
        case SORT_FLOW_AUTO_IDLE:
            return "IDLE";
        case SORT_FLOW_AUTO_WAIT_GATE:
            return "WAIT_GATE";
        case SORT_FLOW_AUTO_SETTLE:
            return "SETTLE";
        case SORT_FLOW_AUTO_ALIGN:
            return "ALIGN";
        case SORT_FLOW_AUTO_PC_WAIT:
            return "PC_WAIT";
        case SORT_FLOW_AUTO_DETECT:
            return "DETECT";
        case SORT_FLOW_AUTO_SORT:
            return "SORT";
        case SORT_FLOW_AUTO_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

/* 加锁更新自动流程状态。 */
static void sort_flow_auto_set_state(sort_flow_auto_state_t state)
{
    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    g_sort_flow_auto.state = state;
    rt_mutex_release(g_sort_flow_auto.lock);
}

/* 加锁读取自动流程是否运行中。 */
rt_bool_t sort_flow_auto_active_now(void)
{
    rt_bool_t active;

    if (g_sort_flow_auto.ready != RT_TRUE)
    {
        return RT_FALSE;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    active = g_sort_flow_auto.active;
    rt_mutex_release(g_sort_flow_auto.lock);
    return active;
}

/* tick 转毫秒，饱和到 16 位。 */
uint16_t sort_flow_auto_ticks_to_ms(rt_tick_t ticks)
{
    uint32_t ms = (uint32_t)(((uint64_t)ticks * 1000ULL) / RT_TICK_PER_SECOND);

    return (ms > 65535U) ? 65535U : (uint16_t)ms;
}

/* 获取自动流程状态快照。 */
int sort_flow_auto_get_snapshot(sort_flow_auto_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->state = (uint8_t)SORT_FLOW_AUTO_IDLE;
    if (g_sort_flow_auto.ready != RT_TRUE)
    {
        return RT_EOK;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    out_snapshot->ready = g_sort_flow_auto.ready;
    out_snapshot->active = g_sort_flow_auto.active;
    out_snapshot->state = (uint8_t)g_sort_flow_auto.state;
    out_snapshot->handled_count = g_sort_flow_auto.handled_count;
    out_snapshot->last_gate_trigger_count = g_sort_flow_auto.last_gate_trigger_count;
    out_snapshot->settle_ms = g_sort_flow_auto.settle_ms;
    out_snapshot->detect_timeout_ms = g_sort_flow_auto.detect_timeout_ms;
    out_snapshot->pc_timeout_ms = g_sort_flow_auto.pc_timeout_ms;
    out_snapshot->pc_waiting = g_sort_flow_auto.pc_waiting;
    out_snapshot->pc_req_id = g_sort_flow_auto.pc_wait_req_id;
    out_snapshot->pc_accepted_count = g_sort_flow_auto.pc_accepted_count;
    out_snapshot->pc_timeout_count = g_sort_flow_auto.pc_timeout_count;
    out_snapshot->pc_fallback_count = g_sort_flow_auto.pc_fallback_count;
    out_snapshot->last_error = g_sort_flow_auto.last_error;
    rt_mutex_release(g_sort_flow_auto.lock);
    return RT_EOK;
}

/* 是否正在等待指定请求号的 PC 结果。 */
rt_bool_t sort_flow_auto_is_waiting_for_pc_result(uint32_t req_id)
{
    rt_bool_t waiting = RT_FALSE;

    if ((req_id == 0U) || (g_sort_flow_auto.ready != RT_TRUE))
    {
        return RT_FALSE;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    if ((g_sort_flow_auto.active == RT_TRUE) &&
        (g_sort_flow_auto.pc_waiting == RT_TRUE) &&
        (g_sort_flow_auto.pc_wait_req_id == req_id))
    {
        waiting = RT_TRUE;
    }
    rt_mutex_release(g_sort_flow_auto.lock);
    return waiting;
}

/* 获取指定请求号已等待的毫秒数。 */
int sort_flow_auto_get_pc_wait_elapsed(uint32_t req_id, uint16_t *out_elapsed_ms)
{
    int ret = -RT_ERROR;

    if ((req_id == 0U) || (out_elapsed_ms == RT_NULL) || (g_sort_flow_auto.ready != RT_TRUE))
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    if ((g_sort_flow_auto.active == RT_TRUE) &&
        (g_sort_flow_auto.pc_waiting == RT_TRUE) &&
        (g_sort_flow_auto.pc_wait_req_id == req_id) &&
        (g_sort_flow_auto.pc_wait_start_tick != 0))
    {
        *out_elapsed_ms = sort_flow_auto_ticks_to_ms(rt_tick_get() - g_sort_flow_auto.pc_wait_start_tick);
        ret = RT_EOK;
    }
    rt_mutex_release(g_sort_flow_auto.lock);
    return ret;
}

/* PC 端提交检测结果，请求号匹配时唤醒自动流程。 */
int sort_flow_auto_submit_pc_result(uint32_t req_id,
                                    rt_bool_t ng,
                                    uint16_t score_x10,
                                    int16_t angle_x10)
{
    rt_bool_t accepted = RT_FALSE;

    if (req_id == 0U)
    {
        return -RT_EINVAL;
    }
    if ((g_sort_flow_auto.ready != RT_TRUE) || (g_sort_flow_auto.pc_sem == RT_NULL))
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    if ((g_sort_flow_auto.active == RT_TRUE) &&
        (g_sort_flow_auto.pc_waiting == RT_TRUE) &&
        (g_sort_flow_auto.pc_wait_req_id == req_id))
    {
        g_sort_flow_auto.pc_result_ready = RT_TRUE;
        g_sort_flow_auto.pc_result_ng = ng ? RT_TRUE : RT_FALSE;
        g_sort_flow_auto.pc_result_score_x10 = score_x10;
        g_sort_flow_auto.pc_result_angle_x10 = angle_x10;
        g_sort_flow_auto.pc_waiting = RT_FALSE;
        g_sort_flow_auto.pc_wait_start_tick = 0;
        g_sort_flow_auto.pc_accepted_count++;
        accepted = RT_TRUE;
    }
    rt_mutex_release(g_sort_flow_auto.lock);

    if (accepted != RT_TRUE)
    {
        rt_kprintf("[flow_auto] pc result ignored req=%lu\n", (unsigned long)req_id);
        return -RT_ERROR;
    }

    /* PC 结果被接受后，再把检测阶段压回 OUTPUT，避免等待刷新把界面留在 AI。 */
    detect_app_set_external_stage(UI_DETECT_STAGE_OUTPUT);
    rt_sem_release(g_sort_flow_auto.pc_sem);
    rt_kprintf("[flow_auto] pc result accepted req=%lu result=%s score=%u angle_x10=%d\n",
               (unsigned long)req_id,
               ng ? "ng" : "ok",
               (unsigned int)score_x10,
               (int)angle_x10);
    return RT_EOK;
}

/* 自动流程仍在运行时重启传送带。 */
static int sort_flow_auto_start_conveyor_if_active(void)
{
    int ret;

    if (g_sort_flow_auto.ready != RT_TRUE)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    if (g_sort_flow_auto.active != RT_TRUE)
    {
        rt_mutex_release(g_sort_flow_auto.lock);
        return -RT_ERROR;
    }

    /* 与 stop 共用自动流程锁：两者并发时 stop 后执行，会再次停下传送带。 */
    ret = sort_flow_start_conveyor();
    rt_mutex_release(g_sort_flow_auto.lock);
    return ret;
}

/* 判断本次门限触发是否为同一工件的重复触发（按编码器位置距离）。 */
static rt_bool_t sort_flow_auto_gate_duplicate(uint32_t gate_count, int32_t *out_position)
{
    int32_t position = 0;
    rt_bool_t duplicate = RT_FALSE;
    int ret;

    if (out_position != RT_NULL)
    {
        *out_position = 0;
    }

    ret = sort_flow_read_conveyor_position(&position);
    if (ret != RT_EOK)
    {
        return RT_FALSE;
    }

    if (out_position != RT_NULL)
    {
        *out_position = position;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    if (g_sort_flow_auto.last_gate_position_valid == RT_TRUE)
    {
        int32_t delta = position - g_sort_flow_auto.last_gate_position;
        if (sort_flow_abs_i32(delta) < SORT_FLOW_GATE_DUP_RAW)
        {
            duplicate = RT_TRUE;
            rt_kprintf("[flow_auto] gate trigger #%lu duplicate ignored pos=%d last=%d delta=%d tol=%d\n",
                       (unsigned long)gate_count,
                       (int)position,
                       (int)g_sort_flow_auto.last_gate_position,
                       (int)delta,
                       SORT_FLOW_GATE_DUP_RAW);
        }
    }
    rt_mutex_release(g_sort_flow_auto.lock);

    return duplicate;
}

/* 检查队首 NG 工件是否到位，到位则停带抓取；门限有新工件时让出。 */
static int sort_flow_auto_handle_queue_pick(rt_bool_t *out_picked, rt_bool_t *out_guarded)
{
    sort_flow_ng_item_t item;
    int32_t position;
    int32_t remaining;
    rt_bool_t gate_disabled = RT_FALSE;
    rt_bool_t pick_done = RT_FALSE;
    int ret;

    if (out_picked != RT_NULL)
    {
        *out_picked = RT_FALSE;
    }
    if (out_guarded != RT_NULL)
    {
        *out_guarded = RT_FALSE;
    }

    if (sort_flow_queue_peek(&item) != RT_TRUE)
    {
        g_sort_flow_queue_guard_log_id = 0U;
        return RT_EOK;
    }

    ret = sort_flow_read_conveyor_position(&position);
    if (ret != RT_EOK)
    {
        rt_kprintf("[flow_queue] read position for pick failed ret=%d\n", ret);
        return ret;
    }

    if (sort_flow_queue_item_arrived(&item, position, &remaining) != RT_TRUE)
    {
        if (sort_flow_queue_item_guarded(&item, position, &remaining) == RT_TRUE)
        {
            if (out_guarded != RT_NULL)
            {
                *out_guarded = RT_TRUE;
            }
            if (g_sort_flow_queue_guard_log_id != item.id)
            {
                g_sort_flow_queue_guard_log_id = item.id;
                rt_kprintf("[flow_queue] guard id=%lu pos=%d target=%d remaining=%d lead=%d guard=%s:%d\n",
                           (unsigned long)item.id,
                           (int)position,
                           (int)item.target_pos,
                           (int)remaining,
                           (int)g_sort_flow_arrive_tolerance_raw,
                           (g_sort_flow_queue_guard_auto == RT_TRUE) ? "auto" : "fixed",
                           (int)sort_flow_queue_get_guard_raw());
            }
        }
        else if (g_sort_flow_queue_guard_log_id == item.id)
        {
            g_sort_flow_queue_guard_log_id = 0U;
        }
        return RT_EOK;
    }

    {
        software_gate_snapshot_t gate_snapshot;
        uint32_t last_gate_trigger_count;

        software_gate_app_get_snapshot(&gate_snapshot);
        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        last_gate_trigger_count = g_sort_flow_auto.last_gate_trigger_count;
        rt_mutex_release(g_sort_flow_auto.lock);
        if (((gate_snapshot.trigger_count != last_gate_trigger_count) &&
             (gate_snapshot.trigger_count != 0U)) ||
            (sort_flow_gate_candidate_pending(&gate_snapshot) == RT_TRUE))
        {
            if (out_guarded != RT_NULL)
            {
                *out_guarded = RT_TRUE;
            }
            return RT_EOK;
        }
    }

    g_sort_flow.busy = RT_TRUE;
    sort_flow_auto_set_state(SORT_FLOW_AUTO_SORT);
    rt_kprintf("[flow_queue] arrived id=%lu pos=%d target=%d remaining=%d lead=%d\n",
               (unsigned long)item.id,
               (int)position,
               (int)item.target_pos,
               (int)remaining,
               (int)g_sort_flow_arrive_tolerance_raw);

    ret = sort_flow_stop_conveyor();
    if (ret == RT_EOK)
    {
        software_gate_snapshot_t gate_snapshot;
        uint32_t last_gate_trigger_count;

        software_gate_app_get_snapshot(&gate_snapshot);
        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        last_gate_trigger_count = g_sort_flow_auto.last_gate_trigger_count;
        rt_mutex_release(g_sort_flow_auto.lock);
        if ((gate_snapshot.trigger_count != last_gate_trigger_count) &&
            (gate_snapshot.trigger_count != 0U))
        {
            rt_kprintf("[flow_queue] pick preempted by gate #%lu\n",
                       (unsigned long)gate_snapshot.trigger_count);
            g_sort_flow.busy = RT_FALSE;
            sort_flow_auto_set_state(SORT_FLOW_AUTO_WAIT_GATE);
            if (out_guarded != RT_NULL)
            {
                *out_guarded = RT_TRUE;
            }
            return RT_EOK;
        }
        if (sort_flow_gate_candidate_pending(&gate_snapshot) == RT_TRUE)
        {
            rt_kprintf("[flow_queue] pick deferred by gate candidate entry=%u\n",
                       (unsigned int)gate_snapshot.entry_allowed);
            if (sort_flow_auto_active_now() == RT_TRUE)
            {
                (void)sort_flow_auto_start_conveyor_if_active();
            }
            g_sort_flow.busy = RT_FALSE;
            sort_flow_auto_set_state(SORT_FLOW_AUTO_WAIT_GATE);
            if (out_guarded != RT_NULL)
            {
                *out_guarded = RT_TRUE;
            }
            return RT_EOK;
        }

        ret = sort_flow_gate_set_enabled(RT_FALSE);
        if (ret == RT_EOK)
        {
            gate_disabled = RT_TRUE;
            ret = sort_flow_run_arm_at_pick_position(&item);
            if (ret == RT_EOK)
            {
                pick_done = RT_TRUE;
            }
        }
    }

    if (gate_disabled == RT_TRUE)
    {
        int gate_ret = sort_flow_gate_set_enabled(RT_TRUE);
        if ((ret == RT_EOK) && (gate_ret != RT_EOK))
        {
            ret = gate_ret;
        }
    }

    if (pick_done == RT_TRUE)
    {
        sort_flow_queue_pop();
        g_sort_flow_ng_queue.picked_count++;
        g_sort_flow_queue_guard_log_id = 0U;
    }

    if (ret == RT_EOK)
    {
        if (sort_flow_auto_active_now() == RT_TRUE)
        {
            ret = sort_flow_auto_start_conveyor_if_active();
        }
    }

    g_sort_flow.last_error = ret;
    g_sort_flow.active = ((ret == RT_EOK) && (sort_flow_auto_active_now() == RT_TRUE)) ?
                         RT_TRUE : RT_FALSE;
    g_sort_flow.busy = RT_FALSE;
    if (out_picked != RT_NULL)
    {
        *out_picked = RT_TRUE;
    }
    return ret;
}

/* 处理一次门限触发：停带、居中、等待 PC 结果（超时回退本地 AI），然后分拣。 */
static int sort_flow_auto_handle_trigger(void)
{
    int ret;
    rt_err_t wait_ret;
    ui_detect_snapshot_t detect_snapshot;
    rt_bool_t ng = RT_FALSE;
    uint16_t score_x10 = 0U;
    int16_t angle_x10 = 0;
    uint32_t settle_ms;
    uint32_t detect_timeout_ms;
    uint32_t pc_timeout_ms;
    uint32_t pc_wait_elapsed_ms = 0U;
    uint32_t pc_wait_step_ms;
    uint32_t req_id;
    rt_bool_t have_pc_result = RT_FALSE;
    rt_bool_t object_present = RT_TRUE;
    rt_tick_t cycle_start_tick = rt_tick_get();

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    settle_ms = g_sort_flow_auto.settle_ms;
    detect_timeout_ms = g_sort_flow_auto.detect_timeout_ms;
    pc_timeout_ms = g_sort_flow_auto.pc_timeout_ms;
    rt_mutex_release(g_sort_flow_auto.lock);

    sort_flow_auto_set_state(SORT_FLOW_AUTO_SETTLE);
    ret = sort_flow_stop_conveyor();
    if (ret != RT_EOK)
    {
        return ret;
    }

    rt_thread_mdelay(settle_ms);
    if (sort_flow_auto_active_now() != RT_TRUE)
    {
        return -RT_ERROR;
    }

    sort_flow_auto_set_state(SORT_FLOW_AUTO_ALIGN);
    ret = sort_flow_align_before_detect(&object_present);
    if (ret != RT_EOK)
    {
        return ret;
    }
    if (sort_flow_auto_active_now() != RT_TRUE)
    {
        return -RT_ERROR;
    }

    /*
     * 反转拉回后仍无工件 → 空带/无件：跳过本次检测(不判 OK/NG、不触发机械臂、不计 OK/NG 数)，
     * 重启传送带继续等下一个，保证不把空帧误判成 NG，也不误占产线统计。
     */
    if (object_present != RT_TRUE)
    {
        rt_kprintf("[flow_auto] no object present, skip detect and resume\n");
        (void)sort_flow_auto_start_conveyor_if_active();
        return RT_EOK;
    }

    while (rt_sem_take(g_sort_flow_auto.pc_sem, RT_WAITING_NO) == RT_EOK)
    {
        /* 发布新请求号前清掉残留的唤醒信号。 */
    }

    sort_flow_auto_set_state(SORT_FLOW_AUTO_PC_WAIT);
    detect_app_set_external_stage(UI_DETECT_STAGE_AI);
    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    req_id = g_sort_flow_auto.pc_next_req_id++;
    if (g_sort_flow_auto.pc_next_req_id == 0U)
    {
        g_sort_flow_auto.pc_next_req_id = 1U;
    }
    g_sort_flow_auto.pc_wait_req_id = req_id;
    g_sort_flow_auto.pc_wait_start_tick = rt_tick_get();
    g_sort_flow_auto.pc_waiting = RT_TRUE;
    g_sort_flow_auto.pc_result_ready = RT_FALSE;
    g_sort_flow_auto.pc_result_ng = RT_FALSE;
    g_sort_flow_auto.pc_result_score_x10 = 0U;
    g_sort_flow_auto.pc_result_angle_x10 = 0;
    rt_mutex_release(g_sort_flow_auto.lock);

    rt_kprintf("[flow_auto] wait pc result req=%lu timeout=%lu\n",
               (unsigned long)req_id,
               (unsigned long)pc_timeout_ms);
    pc_wait_step_ms = (pc_timeout_ms < SORT_FLOW_PC_WAIT_STEP_MS) ? pc_timeout_ms : SORT_FLOW_PC_WAIT_STEP_MS;
    if (pc_wait_step_ms == 0U)
    {
        pc_wait_step_ms = SORT_FLOW_PC_WAIT_STEP_MS;
    }
    wait_ret = -RT_ETIMEOUT;
    while (pc_wait_elapsed_ms < pc_timeout_ms)
    {
        wait_ret = rt_sem_take(g_sort_flow_auto.pc_sem, rt_tick_from_millisecond(pc_wait_step_ms));
        if (wait_ret == RT_EOK)
        {
            break;
        }
        pc_wait_elapsed_ms += pc_wait_step_ms;
        detect_app_set_external_stage(UI_DETECT_STAGE_AI);
        if (sort_flow_auto_active_now() != RT_TRUE)
        {
            break;
        }
        if ((pc_timeout_ms - pc_wait_elapsed_ms) < pc_wait_step_ms)
        {
            pc_wait_step_ms = pc_timeout_ms - pc_wait_elapsed_ms;
        }
        if (pc_wait_step_ms == 0U)
        {
            break;
        }
    }
    if (sort_flow_auto_active_now() != RT_TRUE)
    {
        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        if (g_sort_flow_auto.pc_wait_req_id == req_id)
        {
            g_sort_flow_auto.pc_wait_req_id = 0U;
            g_sort_flow_auto.pc_wait_start_tick = 0;
            g_sort_flow_auto.pc_waiting = RT_FALSE;
            g_sort_flow_auto.pc_result_ready = RT_FALSE;
            g_sort_flow_auto.pc_result_angle_x10 = 0;
        }
        rt_mutex_release(g_sort_flow_auto.lock);
        return -RT_ERROR;
    }

    if (wait_ret == RT_EOK)
    {
        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        if ((g_sort_flow_auto.pc_result_ready == RT_TRUE) &&
            (g_sort_flow_auto.pc_wait_req_id == req_id))
        {
            ng = g_sort_flow_auto.pc_result_ng;
            score_x10 = g_sort_flow_auto.pc_result_score_x10;
            angle_x10 = g_sort_flow_auto.pc_result_angle_x10;
            have_pc_result = RT_TRUE;
        }
        g_sort_flow_auto.pc_wait_req_id = 0U;
        g_sort_flow_auto.pc_wait_start_tick = 0;
        g_sort_flow_auto.pc_waiting = RT_FALSE;
        g_sort_flow_auto.pc_result_ready = RT_FALSE;
        g_sort_flow_auto.pc_result_angle_x10 = 0;
        rt_mutex_release(g_sort_flow_auto.lock);
    }
    else
    {
        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        if (g_sort_flow_auto.pc_wait_req_id == req_id)
        {
            g_sort_flow_auto.pc_wait_req_id = 0U;
            g_sort_flow_auto.pc_wait_start_tick = 0;
            g_sort_flow_auto.pc_waiting = RT_FALSE;
            g_sort_flow_auto.pc_result_ready = RT_FALSE;
            g_sort_flow_auto.pc_result_angle_x10 = 0;
            g_sort_flow_auto.pc_timeout_count++;
            g_sort_flow_auto.pc_fallback_count++;
        }
        rt_mutex_release(g_sort_flow_auto.lock);
    }

    if (have_pc_result == RT_TRUE)
    {
        rt_kprintf("[flow_auto] use pc result req=%lu result=%s score=%u angle_x10=%d\n",
                   (unsigned long)req_id,
                   ng ? "ng" : "ok",
                   (unsigned int)score_x10,
                   (int)angle_x10);
    }
    else
    {
        sort_flow_auto_set_state(SORT_FLOW_AUTO_DETECT);
        rt_kprintf("[flow_auto] pc timeout req=%lu, fallback ai detect\n", (unsigned long)req_id);
        ret = detect_app_request_once_and_wait(&detect_snapshot, detect_timeout_ms);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_auto] detect failed ret=%d\n", ret);
            return ret;
        }
        if (sort_flow_auto_active_now() != RT_TRUE)
        {
            return -RT_ERROR;
        }

        ng = detect_snapshot.ai.anomaly ? RT_TRUE : RT_FALSE;
        score_x10 = detect_snapshot.ai.score_x10;
        angle_x10 = 0;
        rt_kprintf("[flow_auto] fallback ai result=%s score=%u frame=%lu\n",
                   ng ? "ng" : "ok",
                   (unsigned int)score_x10,
                   (unsigned long)detect_snapshot.camera.frame_id);
    }

    g_sort_flow.busy = RT_TRUE;
    g_sort_flow.last_ng = ng;
    g_sort_flow.last_score_x10 = score_x10;

    sort_flow_auto_set_state(SORT_FLOW_AUTO_SORT);
    ret = sort_flow_handle_result_after_stop(ng, score_x10, angle_x10, RT_FALSE);
    sort_flow_queue_note_cycle_ms(sort_flow_auto_ticks_to_ms(rt_tick_get() - cycle_start_tick));
    g_sort_flow.last_error = ret;
    g_sort_flow.active = (ret == RT_EOK) ? RT_TRUE : RT_FALSE;
    g_sort_flow.busy = RT_FALSE;
    return ret;
}

/* 自动流程线程：轮询门限触发与 NG 队列到位。 */
static void sort_flow_auto_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        rt_bool_t active;
        rt_bool_t flow_busy;
        sort_flow_auto_state_t state;
        uint32_t last_gate_trigger_count;
        software_gate_snapshot_t gate_snapshot;

        rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
        active = g_sort_flow_auto.active;
        state = g_sort_flow_auto.state;
        last_gate_trigger_count = g_sort_flow_auto.last_gate_trigger_count;
        rt_mutex_release(g_sort_flow_auto.lock);

        if (active != RT_TRUE)
        {
            rt_sem_take(g_sort_flow_auto.sem, RT_WAITING_FOREVER);
            continue;
        }

        flow_busy = g_sort_flow.busy;
        software_gate_app_get_snapshot(&gate_snapshot);
        if ((gate_snapshot.trigger_count != last_gate_trigger_count) &&
            (gate_snapshot.trigger_count != 0U))
        {
            int ret;
            uint32_t handled_gate_count = gate_snapshot.trigger_count;
            int32_t gate_position = 0;

            if ((state != SORT_FLOW_AUTO_WAIT_GATE) || (flow_busy == RT_TRUE))
            {
                rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
                g_sort_flow_auto.last_gate_trigger_count = gate_snapshot.trigger_count;
                rt_mutex_release(g_sort_flow_auto.lock);
                rt_kprintf("[flow_auto] gate trigger #%lu ignored state=%s busy=%u\n",
                           (unsigned long)gate_snapshot.trigger_count,
                           sort_flow_auto_state_name(state),
                           (unsigned int)flow_busy);
                rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
                continue;
            }

            if (sort_flow_auto_gate_duplicate(handled_gate_count, &gate_position) == RT_TRUE)
            {
                rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
                g_sort_flow_auto.last_gate_trigger_count = handled_gate_count;
                rt_mutex_release(g_sort_flow_auto.lock);
                rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
                continue;
            }

            rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
            g_sort_flow_auto.last_gate_trigger_count = handled_gate_count;
            g_sort_flow_auto.last_gate_position = gate_position;
            g_sort_flow_auto.last_gate_position_valid = RT_TRUE;
            g_sort_flow_auto.handled_count++;
            rt_kprintf("[flow_auto] gate trigger #%lu handled=%lu pos=%d\n",
                       (unsigned long)handled_gate_count,
                       (unsigned long)g_sort_flow_auto.handled_count,
                       (int)gate_position);
            rt_mutex_release(g_sort_flow_auto.lock);

            ret = sort_flow_auto_handle_trigger();
            software_gate_app_get_snapshot(&gate_snapshot);
            rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
            /*
             * 吸收本次处理期间(居中/反转拉回会移动传送带)可能引发的门限再触发，
             * 避免这些自身运动造成的触发被当成新工件、开启幻影新周期。
             * 处理期间传送带受我们控制、不会有独立新件到达，故安全。
             */
            g_sort_flow_auto.last_gate_trigger_count = gate_snapshot.trigger_count;
            g_sort_flow_auto.last_error = ret;
            if (ret == RT_EOK)
            {
                g_sort_flow_auto.state = SORT_FLOW_AUTO_WAIT_GATE;
                detect_app_set_external_stage(UI_DETECT_STAGE_IDLE);
            }
            else if (g_sort_flow_auto.active != RT_TRUE)
            {
                g_sort_flow_auto.state = SORT_FLOW_AUTO_IDLE;
                detect_app_set_external_run_state(RT_FALSE);
            }
            else
            {
                g_sort_flow_auto.state = SORT_FLOW_AUTO_ERROR;
                g_sort_flow_auto.active = RT_FALSE;
                detect_app_set_external_run_state(RT_FALSE);
            }
            active = g_sort_flow_auto.active;
            rt_mutex_release(g_sort_flow_auto.lock);

            rt_kprintf("[flow_auto] cycle done ret=%d active=%u\n",
                       ret,
                       (unsigned int)active);
            rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
            continue;
        }

        if ((state == SORT_FLOW_AUTO_WAIT_GATE) && (flow_busy != RT_TRUE))
        {
            rt_bool_t picked = RT_FALSE;
            rt_bool_t guarded = RT_FALSE;
            int ret;

            if (sort_flow_gate_candidate_pending(&gate_snapshot) == RT_TRUE)
            {
                rt_thread_mdelay(SORT_FLOW_QUEUE_GUARD_POLL_MS);
                continue;
            }

            ret = sort_flow_auto_handle_queue_pick(&picked, &guarded);
            if (ret != RT_EOK)
            {
                rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
                g_sort_flow_auto.last_error = ret;
                g_sort_flow_auto.state = SORT_FLOW_AUTO_ERROR;
                g_sort_flow_auto.active = RT_FALSE;
                rt_mutex_release(g_sort_flow_auto.lock);
                detect_app_set_external_run_state(RT_FALSE);
                rt_kprintf("[flow_auto] queue pick failed ret=%d\n", ret);
                rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
                continue;
            }
            if (picked == RT_TRUE)
            {
                rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
                if (g_sort_flow_auto.active == RT_TRUE)
                {
                    g_sort_flow_auto.state = SORT_FLOW_AUTO_WAIT_GATE;
                }
                rt_mutex_release(g_sort_flow_auto.lock);
                if (sort_flow_auto_active_now() == RT_TRUE)
                {
                    detect_app_set_external_stage(UI_DETECT_STAGE_IDLE);
                }
                rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
                continue;
            }
            if (guarded == RT_TRUE)
            {
                rt_thread_mdelay(SORT_FLOW_QUEUE_GUARD_POLL_MS);
                continue;
            }
        }

        rt_thread_mdelay(SORT_FLOW_AUTO_POLL_MS);
    }
}

/* 启动自动分拣流程。 */
int sort_flow_auto_start(void)
{
    int ret;
    software_gate_config_t gate_config;
    software_gate_snapshot_t gate_snapshot;
    sort_flow_auto_state_t state;
    rt_bool_t active;

    ret = sort_flow_auto_init();
    if (ret != RT_EOK)
    {
        return ret;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    active = g_sort_flow_auto.active;
    state = g_sort_flow_auto.state;
    rt_mutex_release(g_sort_flow_auto.lock);
    if (active == RT_TRUE)
    {
        rt_kprintf("[flow_auto] already active state=%s\n", sort_flow_auto_state_name(state));
        return RT_EOK;
    }

    if (g_sort_flow.busy == RT_TRUE)
    {
        rt_kprintf("[flow_auto] busy\n");
        return -RT_EBUSY;
    }

    software_gate_app_get_config(&gate_config);
    gate_config.enabled = RT_TRUE;
    ret = software_gate_app_set_config(&gate_config);
    if (ret != RT_EOK)
    {
        rt_kprintf("[flow_auto] enable gate failed ret=%d\n", ret);
        return ret;
    }

    software_gate_app_rearm();
    software_gate_app_get_snapshot(&gate_snapshot);
    ret = sort_flow_start_conveyor();
    g_sort_flow.active = (ret == RT_EOK) ? RT_TRUE : RT_FALSE;
    g_sort_flow.last_error = ret;
    if (ret != RT_EOK)
    {
        return ret;
    }

    while (rt_sem_take(g_sort_flow_auto.pc_sem, RT_WAITING_NO) == RT_EOK)
    {
        /* 丢弃上一轮遗留的 PC 结果唤醒信号。 */
    }
    sort_flow_queue_reset(RT_FALSE);

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    g_sort_flow_auto.active = RT_TRUE;
    g_sort_flow_auto.state = SORT_FLOW_AUTO_WAIT_GATE;
    g_sort_flow_auto.last_gate_trigger_count = gate_snapshot.trigger_count;
    g_sort_flow_auto.last_gate_position = 0;
    g_sort_flow_auto.last_gate_position_valid = RT_FALSE;
    g_sort_flow_auto.pc_wait_req_id = 0U;
    g_sort_flow_auto.pc_wait_start_tick = 0;
    g_sort_flow_auto.pc_waiting = RT_FALSE;
    g_sort_flow_auto.pc_result_ready = RT_FALSE;
    g_sort_flow_auto.pc_result_angle_x10 = 0;
    if (g_sort_flow_auto.pc_next_req_id == 0U)
    {
        g_sort_flow_auto.pc_next_req_id = 1U;
    }
    g_sort_flow_auto.last_error = RT_EOK;
    rt_mutex_release(g_sort_flow_auto.lock);
    detect_app_set_external_run_state(RT_TRUE);
    rt_sem_release(g_sort_flow_auto.sem);

    rt_kprintf("[flow_auto] start rpm=%u acc=%u gate_trigger=%lu\n",
               (unsigned int)device_comm_app_get_conveyor_rpm(),
               (unsigned int)device_comm_app_get_conveyor_acc(),
               (unsigned long)gate_snapshot.trigger_count);
    return RT_EOK;
}

/* 停止自动分拣流程并停带。 */
int sort_flow_auto_stop(void)
{
    int ret;
    rt_bool_t was_active;
    rt_bool_t was_pc_waiting;

    if (g_sort_flow_auto.ready != RT_TRUE)
    {
        if (g_sort_flow.active == RT_TRUE)
        {
            ret = sort_flow_stop_conveyor();
            g_sort_flow.active = RT_FALSE;
            g_sort_flow.last_error = ret;
            detect_app_set_external_run_state(RT_FALSE);
            return ret;
        }

        detect_app_set_external_run_state(RT_FALSE);
        return RT_EOK;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    was_active = g_sort_flow_auto.active;
    was_pc_waiting = g_sort_flow_auto.pc_waiting;
    g_sort_flow_auto.active = RT_FALSE;
    g_sort_flow_auto.state = SORT_FLOW_AUTO_IDLE;
    g_sort_flow_auto.pc_wait_req_id = 0U;
    g_sort_flow_auto.pc_wait_start_tick = 0;
    g_sort_flow_auto.pc_waiting = RT_FALSE;
    g_sort_flow_auto.pc_result_ready = RT_FALSE;
    g_sort_flow_auto.pc_result_angle_x10 = 0;

    if ((was_active == RT_TRUE) || (g_sort_flow.active == RT_TRUE))
    {
        ret = sort_flow_stop_conveyor();
    }
    else
    {
        ret = RT_EOK;
    }
    g_sort_flow.active = RT_FALSE;
    g_sort_flow.last_error = ret;
    sort_flow_queue_reset(RT_FALSE);
    rt_mutex_release(g_sort_flow_auto.lock);

    if ((was_pc_waiting == RT_TRUE) && (g_sort_flow_auto.pc_sem != RT_NULL))
    {
        rt_sem_release(g_sort_flow_auto.pc_sem);
    }

    detect_app_set_external_run_state(RT_FALSE);
    rt_kprintf("[flow_auto] stop ret=%d\n", ret);
    return ret;
}

/* 自动分拣流程是否运行中。 */
rt_bool_t sort_flow_auto_is_active(void)
{
    rt_bool_t active;

    if (g_sort_flow_auto.ready != RT_TRUE)
    {
        return RT_FALSE;
    }

    rt_mutex_take(g_sort_flow_auto.lock, RT_WAITING_FOREVER);
    active = g_sort_flow_auto.active;
    rt_mutex_release(g_sort_flow_auto.lock);
    return active;
}

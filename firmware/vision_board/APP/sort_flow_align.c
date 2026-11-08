/*
 * 检测前居中对位：根据软件门限给出的工件包围框微调传送带，使工件位于检测区中心。
 */
#include "sort_demo_internal.h"

static const sort_flow_align_config_t s_sort_flow_align_default = {
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

/* 校验居中对位参数范围。 */
static int sort_flow_align_validate_config(const sort_flow_align_config_t *config)
{
    if (config == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (config->target_y > 239U)
    {
        return -RT_EINVAL;
    }
    if (config->tolerance_px > 80U)
    {
        return -RT_EINVAL;
    }
    if (config->pulse_per_px > 200U)
    {
        return -RT_EINVAL;
    }
    if ((config->min_pulses == 0U) ||
        (config->min_pulses > config->max_pulses) ||
        (config->max_pulses > 3000U))
    {
        return -RT_EINVAL;
    }
    if ((config->rpm == 0U) || (config->rpm > 60U))
    {
        return -RT_EINVAL;
    }
    if (config->max_iter > 10U)
    {
        return -RT_EINVAL;
    }
    if (config->settle_ms > 3000U)
    {
        return -RT_EINVAL;
    }

    return RT_EOK;
}

void sort_flow_align_get_config(sort_flow_align_config_t *out_config)
{
    if (out_config == RT_NULL)
    {
        return;
    }

    *out_config = g_sort_flow_align;
}

int sort_flow_align_set_config(const sort_flow_align_config_t *config)
{
    int ret = sort_flow_align_validate_config(config);

    if (ret != RT_EOK)
    {
        return ret;
    }

    g_sort_flow_align = *config;
    g_sort_flow_align.enabled = config->enabled ? RT_TRUE : RT_FALSE;
    g_sort_flow_align.reverse_dir = config->reverse_dir ? RT_TRUE : RT_FALSE;
    return RT_EOK;
}

void sort_flow_align_reset_default(void)
{
    g_sort_flow_align = s_sort_flow_align_default;
}

/* 对位脉冲限幅到配置区间。 */
static uint32_t sort_flow_align_clamp_pulses(uint32_t pulses,
                                             const sort_flow_align_config_t *config)
{
    if (pulses < config->min_pulses)
    {
        return config->min_pulses;
    }
    if (pulses > config->max_pulses)
    {
        return config->max_pulses;
    }
    return pulses;
}

/* 根据 y 方向误差与方向配置决定对位转向。 */
static zdt_motor_dir_t sort_flow_align_dir_from_error(int32_t error_y,
                                                      const sort_flow_align_config_t *config)
{
    rt_bool_t use_cw = (error_y > 0) ? RT_TRUE : RT_FALSE;

    if (config->reverse_dir == RT_TRUE)
    {
        use_cw = (use_cw == RT_TRUE) ? RT_FALSE : RT_TRUE;
    }

    return (use_cw == RT_TRUE) ? ZDT_MOTOR_DIR_CW : ZDT_MOTOR_DIR_CCW;
}

/*
 * 判断画面内是否有“真实工件”。
 * 注意：不能用 software_gate 的 bbox_valid——它在亮像素面积 area>0 时即为真，
 * 空带的噪点/反光也会让它恒真，导致空带/冲出框识别不出来。
 * 这里用亮像素面积是否达到门限触发面积 area_on(真实白卡面积上万,空带≈0)，可靠区分。
 */
static rt_bool_t sort_flow_object_present(const software_gate_snapshot_t *snap)
{
    uint32_t threshold;

    if (snap == RT_NULL)
    {
        return RT_FALSE;
    }
    threshold = snap->config.area_on;
    if (threshold == 0U)
    {
        threshold = 1U;
    }
    return (snap->area >= threshold) ? RT_TRUE : RT_FALSE;
}

/* 检测前居中：工件冲出画面时先反转拉回，再按包围框中心迭代对位。 */
int sort_flow_align_before_detect(rt_bool_t *out_object_present)
{
    sort_flow_align_config_t config;
    software_gate_snapshot_t gate_snapshot;
    int ret = RT_EOK;
    uint8_t iter;
    uint8_t recover;

    /* 默认认为画面内有工件；仅在反转拉回也找不到时才置为无。 */
    if (out_object_present != RT_NULL)
    {
        *out_object_present = RT_TRUE;
    }

    config = g_sort_flow_align;
    if (config.enabled != RT_TRUE)
    {
        return RT_EOK;
    }
    if ((config.max_iter == 0U) || (config.pulse_per_px == 0U))
    {
        rt_kprintf("[flow_align] disabled by iter/gain config\n");
        return RT_EOK;
    }

    /*
     * 反转拉回阶段：停车太慢时工件可能冲出画面(亮像素面积 area 从~16000 骤降到~0)。此时反转传送带把它拉回，
     * 避免拿空帧去判 NG，也保证不漏件。反转方向沿用“居中时减小 y”的方向(尊重 reverse_dir
     * 配置)，每步与居中同量级、最多 max_iter 次；仍找不到则判定空带/无件，跳过本次检测。
     * 这是检测侧的周期内延续，不经过门限去重，与抓取侧的“送回防重复”互不冲突。
     */
    for (recover = 0U; recover <= config.max_iter; recover++)
    {
        uint32_t recover_px;
        uint32_t recover_pulses;
        zdt_motor_dir_t recover_dir;

        if (sort_flow_auto_active_now() != RT_TRUE)
        {
            return -RT_ERROR;
        }

        software_gate_app_get_snapshot(&gate_snapshot);
        if (sort_flow_object_present(&gate_snapshot) == RT_TRUE)
        {
            break; /* 亮像素面积达标，工件确实在画面内，进入居中阶段 */
        }

        /*
         * 能进入本检测周期 = 门限已触发过 = 工件确实进过画面。
         * 此刻面积不足(常因停车稍慢、工件冲出画面，area 会瞬间从~16000 掉到~0) →
         * 反转拉回，而不是当空带跳过。这样才会真正触发回拉重检。
         * 反转 max_iter 次后仍找不到(极少见，如工件掉落/被取走) → 才放弃、跳过本次。
         */
        if (recover >= config.max_iter)
        {
            rt_kprintf("[flow_align] object not recovered after %u reverses (area=%lu<%lu), skip detect\n",
                       (unsigned int)recover,
                       (unsigned long)gate_snapshot.area,
                       (unsigned long)gate_snapshot.config.area_on);
            if (out_object_present != RT_NULL)
            {
                *out_object_present = RT_FALSE;
            }
            return RT_EOK;
        }

        recover_px = (config.target_y < 240U) ? (240U - (uint32_t)config.target_y) : 120U;
        recover_pulses = sort_flow_align_clamp_pulses(recover_px * (uint32_t)config.pulse_per_px, &config);
        /*
         * 反转方向 = 正向运行(sort_flow_start_conveyor 用 CW)的反方向 = CCW。
         * 工件是顺着正向冲出画面的，所以反转(CCW)才能把它往回拉。
         * 直接用 CCW，不用 dir_from_error(它受 reverse_dir 配置影响、容易把方向搞反)。
         */
        recover_dir = ZDT_MOTOR_DIR_CCW;

        rt_kprintf("[flow_align] recover %u/%u reverse dir=%s pulses=%lu (area=%lu<%lu)\n",
                   (unsigned int)(recover + 1U),
                   (unsigned int)config.max_iter,
                   (recover_dir == ZDT_MOTOR_DIR_CW) ? "cw" : "ccw",
                   (unsigned long)recover_pulses,
                   (unsigned long)gate_snapshot.area,
                   (unsigned long)gate_snapshot.config.area_on);

        ret = sort_flow_align_move(recover_dir, config.rpm, config.acc, recover_pulses);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_align] recover move failed ret=%d, skip detect\n", ret);
            if (out_object_present != RT_NULL)
            {
                *out_object_present = RT_FALSE;
            }
            return RT_EOK;
        }
        ret = sort_flow_wait_conveyor_reached_fast(SORT_FLOW_ALIGN_TIMEOUT_MS);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_align] recover wait failed ret=%d, skip detect\n", ret);
            if (out_object_present != RT_NULL)
            {
                *out_object_present = RT_FALSE;
            }
            return RT_EOK;
        }
        rt_thread_mdelay(config.settle_ms);
    }

    for (iter = 0U; iter < config.max_iter; iter++)
    {
        int32_t center_y;
        int32_t error_y;
        uint32_t abs_error;
        uint32_t pulses;
        zdt_motor_dir_t dir;

        if (sort_flow_auto_active_now() != RT_TRUE)
        {
            return -RT_ERROR;
        }

        software_gate_app_get_snapshot(&gate_snapshot);
        if (sort_flow_object_present(&gate_snapshot) != RT_TRUE)
        {
            /* 居中过程中工件又离开画面(面积跌破门限)：按“无有效工件”处理，跳过检测，不拿空帧判 NG。 */
            rt_kprintf("[flow_align] object lost during centering (area=%lu<%lu), skip detect\n",
                       (unsigned long)gate_snapshot.area,
                       (unsigned long)gate_snapshot.config.area_on);
            if (out_object_present != RT_NULL)
            {
                *out_object_present = RT_FALSE;
            }
            return RT_EOK;
        }

        center_y = ((int32_t)gate_snapshot.bbox_y0 + (int32_t)gate_snapshot.bbox_y1) / 2;
        error_y = center_y - (int32_t)config.target_y;
        if ((error_y <= (int32_t)config.tolerance_px) &&
            (error_y >= -(int32_t)config.tolerance_px))
        {
            rt_kprintf("[flow_align] aligned cy=%d target=%u err=%d tol=%u\n",
                       (int)center_y,
                       (unsigned int)config.target_y,
                       (int)error_y,
                       (unsigned int)config.tolerance_px);
            return RT_EOK;
        }

        abs_error = (error_y >= 0) ? (uint32_t)error_y : (uint32_t)(-error_y);
        pulses = sort_flow_align_clamp_pulses(abs_error * (uint32_t)config.pulse_per_px, &config);
        dir = sort_flow_align_dir_from_error(error_y, &config);
        rt_kprintf("[flow_align] iter=%u cy=%d target=%u err=%d dir=%s pulses=%lu bbox=(%u,%u)-(%u,%u)\n",
                   (unsigned int)(iter + 1U),
                   (int)center_y,
                   (unsigned int)config.target_y,
                   (int)error_y,
                   (dir == ZDT_MOTOR_DIR_CW) ? "cw" : "ccw",
                   (unsigned long)pulses,
                   (unsigned int)gate_snapshot.bbox_x0,
                   (unsigned int)gate_snapshot.bbox_y0,
                   (unsigned int)gate_snapshot.bbox_x1,
                   (unsigned int)gate_snapshot.bbox_y1);

        ret = sort_flow_align_move(dir, config.rpm, config.acc, pulses);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_align] move failed ret=%d, continue detect\n", ret);
            return RT_EOK;
        }

        ret = sort_flow_wait_conveyor_reached_fast(SORT_FLOW_ALIGN_TIMEOUT_MS);
        if (ret != RT_EOK)
        {
            rt_kprintf("[flow_align] wait reached failed ret=%d, continue detect\n", ret);
            return RT_EOK;
        }

        rt_thread_mdelay(config.settle_ms);
    }

    software_gate_app_get_snapshot(&gate_snapshot);
    if (gate_snapshot.bbox_valid == RT_TRUE)
    {
        int32_t center_y = ((int32_t)gate_snapshot.bbox_y0 + (int32_t)gate_snapshot.bbox_y1) / 2;
        int32_t error_y = center_y - (int32_t)config.target_y;

        rt_kprintf("[flow_align] max_iter reached cy=%d target=%u err=%d\n",
                   (int)center_y,
                   (unsigned int)config.target_y,
                   (int)error_y);
    }

    return RT_EOK;
}

/* 打印居中对位参数与用法。 */
void sort_flow_align_print(void)
{
    rt_kprintf("[flow_align] enabled=%u reverse_dir=%u target_y=%u tol=%u gain=%u pulse/px\n",
               (unsigned int)g_sort_flow_align.enabled,
               (unsigned int)g_sort_flow_align.reverse_dir,
               (unsigned int)g_sort_flow_align.target_y,
               (unsigned int)g_sort_flow_align.tolerance_px,
               (unsigned int)g_sort_flow_align.pulse_per_px);
    rt_kprintf("[flow_align] pulses=%lu..%lu rpm=%u acc=%u iter=%u settle=%lu\n",
               (unsigned long)g_sort_flow_align.min_pulses,
               (unsigned long)g_sort_flow_align.max_pulses,
               (unsigned int)g_sort_flow_align.rpm,
               (unsigned int)g_sort_flow_align.acc,
               (unsigned int)g_sort_flow_align.max_iter,
               (unsigned long)g_sort_flow_align.settle_ms);
    rt_kprintf("[flow_align] cy>target: object lower, default CW makes y smaller.\n");
    rt_kprintf("[flow_align] use: on/off, target <y>, tol <px>, gain <pulse_per_px>\n");
    rt_kprintf("[flow_align] use: rpm <rpm>, acc <acc>, pulses <min> <max>, iter <n>\n");
    rt_kprintf("[flow_align] use: settle <ms>, dir normal|reverse\n");
}

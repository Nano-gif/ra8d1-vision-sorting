#include "software_gate_internal.h"

static uint32_t swgate_rearm_clear_area(void);
static void swgate_update_entry_band(uint32_t entry_area);
static void swgate_update_entry_track(uint16_t roi_bottom);
static void swgate_set_rearm_guard_from_bbox(void);
static rt_bool_t swgate_rearm_guard_blocked(void);

static uint32_t swgate_luma_threshold_scaled(uint8_t threshold)
{
    return (uint32_t)threshold * 100U;
}

static rt_bool_t swgate_rgb565_luma_ge(uint16_t pixel, uint32_t threshold_scaled)
{
    uint32_t r = ((pixel >> 11) & 0x1FU) << 3;
    uint32_t g = ((pixel >> 5) & 0x3FU) << 2;
    uint32_t b = (pixel & 0x1FU) << 3;
    uint32_t y_scaled = (r * 30U) + (g * 59U) + (b * 11U);

    return (y_scaled >= threshold_scaled) ? RT_TRUE : RT_FALSE;
}

rt_bool_t swgate_elapsed_ms(rt_tick_t start_tick, uint32_t ms)
{
    return (rt_tick_get() - start_tick) >= rt_tick_from_millisecond(ms);
}

static rt_bool_t swgate_is_clear_frame(void)
{
    uint32_t clear_area = swgate_rearm_clear_area();

    if (g_gate.bbox_valid != RT_TRUE)
    {
        return RT_TRUE;
    }

    /*
     * 只有当目标不再是有效候选、且亮区面积远低于触发阈值时才重新布防。
     * 仅用 area_off 判断过于宽松：同一物体仍在 ROI 内时面积可能短暂
     * 低于 area_off，导致重复触发。
     */
    if ((g_gate.area <= clear_area) &&
        (g_gate.candidate != RT_TRUE) &&
        (g_gate.inside != RT_TRUE))
    {
        swgate_reset_entry_track();
        return RT_TRUE;
    }

    return RT_FALSE;
}

void swgate_reset_runtime(void)
{
    g_gate.frame_count = 0U;
    g_gate.area = 0U;
    g_gate.candidate = RT_FALSE;
    g_gate.inside = RT_FALSE;
    g_gate.detectable = RT_FALSE;
    g_gate.bbox_valid = RT_FALSE;
    g_gate.bbox_x0 = 0U;
    g_gate.bbox_y0 = 0U;
    g_gate.bbox_x1 = 0U;
    g_gate.bbox_y1 = 0U;
    g_gate.bbox_cy = 0U;
    swgate_reset_entry_track();
    g_gate.entry_area = 0U;
    g_gate.entry_occupied = RT_FALSE;
    g_gate.entry_empty_count = 0U;
    g_gate.entry_rising = RT_FALSE;
    g_gate.entry_seq = 0U;
    g_gate.last_trigger_entry_seq = 0U;
    g_gate.entry_rearm_pending = RT_FALSE;
    g_gate.entry_rearm_active = RT_FALSE;
    g_gate.stable_count = 0U;
    g_gate.empty_count = 0U;
    g_gate.rearm_guard_valid = RT_FALSE;
    g_gate.rearm_guard_x0 = 0U;
    g_gate.rearm_guard_y0 = 0U;
    g_gate.rearm_guard_x1 = 0U;
    g_gate.rearm_guard_y1 = 0U;
    g_gate.last_trigger_tick = 0U;
    g_gate.last_debug_tick = 0U;
}

void swgate_clear_rearm_guard(void)
{
    g_gate.rearm_guard_valid = RT_FALSE;
    g_gate.rearm_guard_x0 = 0U;
    g_gate.rearm_guard_y0 = 0U;
    g_gate.rearm_guard_x1 = 0U;
    g_gate.rearm_guard_y1 = 0U;
}

void swgate_reset_entry_track(void)
{
    g_gate.entry_state = SOFTWARE_GATE_ENTRY_UNKNOWN;
    g_gate.entry_allowed = (g_gate.entry_filter_enabled == RT_TRUE) ? RT_FALSE : RT_TRUE;
    g_gate.entry_start_y0 = 0U;
    g_gate.entry_start_y1 = 0U;
    g_gate.entry_start_cy = 0U;
}

static void swgate_update_entry_track(uint16_t roi_bottom)
{
    uint16_t entry_y;
    uint16_t entry_margin = g_gate.entry_margin;
    uint16_t roi_h = (uint16_t)(roi_bottom - g_gate.roi_y + 1U);

    if (g_gate.entry_filter_enabled != RT_TRUE)
    {
        g_gate.entry_state = SOFTWARE_GATE_ENTRY_NORMAL;
        g_gate.entry_allowed = RT_TRUE;
        return;
    }

    if (g_gate.candidate != RT_TRUE)
    {
        return;
    }

    if (g_gate.entry_state != SOFTWARE_GATE_ENTRY_UNKNOWN)
    {
        return;
    }

    if (entry_margin == 0U)
    {
        entry_margin = 1U;
    }
    if (entry_margin > roi_h)
    {
        entry_margin = roi_h;
    }

    entry_y = (uint16_t)(roi_bottom - entry_margin + 1U);
    g_gate.entry_start_y0 = g_gate.bbox_y0;
    g_gate.entry_start_y1 = g_gate.bbox_y1;
    g_gate.entry_start_cy = g_gate.bbox_cy;
    if (g_gate.bbox_y1 >= entry_y)
    {
        g_gate.entry_state = SOFTWARE_GATE_ENTRY_NORMAL;
        g_gate.entry_allowed = RT_TRUE;
    }
    else
    {
        g_gate.entry_state = SOFTWARE_GATE_ENTRY_BLOCKED;
        g_gate.entry_allowed = RT_FALSE;
    }
}

static uint32_t swgate_rearm_clear_area(void)
{
    uint32_t area = g_gate.area_off / SWGATE_REARM_CLEAR_RATIO_DIV;

    if (area < SWGATE_REARM_CLEAR_MIN_AREA)
    {
        area = SWGATE_REARM_CLEAR_MIN_AREA;
    }
    if (area >= g_gate.area_on)
    {
        area = (g_gate.area_on > 0U) ? (g_gate.area_on - 1U) : 0U;
    }

    return area;
}

uint32_t swgate_entry_area_on(void)
{
    return g_gate.entry_area_on;
}

static void swgate_update_entry_band(uint32_t entry_area)
{
    rt_bool_t occupied;

    g_gate.entry_area = entry_area;
    g_gate.entry_rising = RT_FALSE;
    occupied = (entry_area >= swgate_entry_area_on()) ? RT_TRUE : RT_FALSE;

    if (occupied == RT_TRUE)
    {
        if ((g_gate.entry_occupied != RT_TRUE) &&
            (g_gate.entry_empty_count >= g_gate.entry_rearm_empty_frames))
        {
            g_gate.entry_rising = RT_TRUE;
            g_gate.entry_seq++;
            /*
             * 反向取料纠偏可能把已见过的物体从 y=0 推回，
             * 已被阻断的轨迹保持阻断，避免误判为新的底部进入目标。
             */
            if (g_gate.entry_state != SOFTWARE_GATE_ENTRY_BLOCKED)
            {
                swgate_reset_entry_track();
                g_gate.entry_rearm_pending = RT_TRUE;
            }
            else
            {
                g_gate.entry_rearm_pending = RT_FALSE;
            }
        }
        g_gate.entry_empty_count = 0U;
    }
    else
    {
        if (g_gate.entry_empty_count < 0xffU)
        {
            g_gate.entry_empty_count++;
        }
    }

    g_gate.entry_occupied = occupied;
}

static void swgate_set_rearm_guard_from_bbox(void)
{
    if (g_gate.enabled != RT_TRUE)
    {
        swgate_clear_rearm_guard();
        return;
    }

    /*
     * 触发一次后锁定整个 ROI。这比只按 bbox 防护更严格：同一物体会在 ROI 内
     * 继续移动，阈值化后的 bbox 可能缩小或漂移，仅按 bbox 判断会在物体离场前
     * 重复触发。
     *
     * 防护仅在 COOLDOWN 状态生效，不能阻塞 WAIT -> SEEN，
     * 否则残留的防护状态会让 ROI 变红却不产生新的触发。
     */
    g_gate.rearm_guard_valid = RT_TRUE;
    g_gate.rearm_guard_x0 = g_gate.roi_x;
    g_gate.rearm_guard_y0 = g_gate.roi_y;
    g_gate.rearm_guard_x1 = (uint16_t)(g_gate.roi_x + g_gate.roi_w - 1U);
    g_gate.rearm_guard_y1 = (uint16_t)(g_gate.roi_y + g_gate.roi_h - 1U);
}

static rt_bool_t swgate_rearm_guard_blocked(void)
{
    if (g_gate.rearm_guard_valid != RT_TRUE)
    {
        return RT_FALSE;
    }

    if (g_gate.bbox_valid != RT_TRUE)
    {
        return RT_FALSE;
    }

    if ((g_gate.bbox_x1 < g_gate.rearm_guard_x0) ||
        (g_gate.bbox_x0 > g_gate.rearm_guard_x1) ||
        (g_gate.bbox_y1 < g_gate.rearm_guard_y0) ||
        (g_gate.bbox_y0 > g_gate.rearm_guard_y1))
    {
        return RT_FALSE;
    }

    return RT_TRUE;
}

void swgate_clear_detection(void)
{
    g_gate.area = 0U;
    g_gate.candidate = RT_FALSE;
    g_gate.inside = RT_FALSE;
    g_gate.detectable = RT_FALSE;
    g_gate.bbox_valid = RT_FALSE;
    g_gate.bbox_x0 = 0U;
    g_gate.bbox_y0 = 0U;
    g_gate.bbox_x1 = 0U;
    g_gate.bbox_y1 = 0U;
    g_gate.bbox_cy = 0U;
    swgate_reset_entry_track();
    g_gate.entry_area = 0U;
    g_gate.entry_occupied = RT_FALSE;
    g_gate.entry_rising = RT_FALSE;
    g_gate.entry_rearm_active = RT_FALSE;
}

static uint32_t swgate_elapsed_tick_to_ms(rt_tick_t ticks)
{
    return (uint32_t)((ticks * 1000U) / RT_TICK_PER_SECOND);
}

void swgate_record_process_time(rt_tick_t start_tick)
{
    uint32_t elapsed_ms = swgate_elapsed_tick_to_ms(rt_tick_get() - start_tick);

    g_gate_perf.process_count++;
    g_gate_perf.last_process_ms = elapsed_ms;
    g_gate_perf.total_process_ms += elapsed_ms;
    if (elapsed_ms > g_gate_perf.max_process_ms)
    {
        g_gate_perf.max_process_ms = elapsed_ms;
    }
}

void swgate_analyze_frame(const uint16_t *framebuffer, uint16_t width, uint16_t height)
{
    uint16_t x;
    uint16_t y;
    uint16_t x_end;
    uint16_t y_end;
    uint16_t min_x = 0xffffU;
    uint16_t min_y = 0xffffU;
    uint16_t max_x = 0U;
    uint16_t max_y = 0U;
    uint32_t area = 0U;
    uint32_t entry_area = 0U;
    uint32_t threshold_scaled = swgate_luma_threshold_scaled(g_gate.bright_threshold);

    if ((framebuffer == RT_NULL) || (width == 0U) || (height == 0U))
    {
        return;
    }

    if ((g_gate.roi_x >= width) || (g_gate.roi_y >= height))
    {
        swgate_clear_detection();
        return;
    }

    x_end = (uint16_t)((uint32_t)g_gate.roi_x + (uint32_t)g_gate.roi_w);
    y_end = (uint16_t)((uint32_t)g_gate.roi_y + (uint32_t)g_gate.roi_h);
    if (x_end > width)
    {
        x_end = width;
    }
    if (y_end > height)
    {
        y_end = height;
    }

    for (y = g_gate.roi_y; y < y_end; y++)
    {
        const uint16_t *row = framebuffer + ((uint32_t)y * (uint32_t)width);
        rt_bool_t in_entry_band = RT_FALSE;

        if (g_gate.entry_filter_enabled == RT_TRUE)
        {
            uint16_t entry_band_h = g_gate.entry_band_h;
            uint16_t roi_h = (uint16_t)(y_end - g_gate.roi_y);
            uint16_t entry_band_y;

            if (entry_band_h == 0U)
            {
                entry_band_h = 1U;
            }
            if (entry_band_h > roi_h)
            {
                entry_band_h = roi_h;
            }
            entry_band_y = (uint16_t)(y_end - entry_band_h);
            in_entry_band = (y >= entry_band_y) ? RT_TRUE : RT_FALSE;
        }

        for (x = g_gate.roi_x; x < x_end; x++)
        {
            if (swgate_rgb565_luma_ge(row[x], threshold_scaled) == RT_TRUE)
            {
                area++;
                if (in_entry_band == RT_TRUE)
                {
                    entry_area++;
                }
                if (x < min_x)
                {
                    min_x = x;
                }
                if (x > max_x)
                {
                    max_x = x;
                }
                if (y < min_y)
                {
                    min_y = y;
                }
                if (y > max_y)
                {
                    max_y = y;
                }
            }
        }
    }

    g_gate.area = area;
    swgate_update_entry_band(entry_area);
    if (area > 0U)
    {
        uint16_t bbox_w = (uint16_t)(max_x - min_x + 1U);
        uint16_t bbox_h = (uint16_t)(max_y - min_y + 1U);
        uint16_t bbox_cy = (uint16_t)(((uint32_t)min_y + (uint32_t)max_y) / 2U);
        uint16_t roi_right = (uint16_t)(x_end - 1U);
        uint16_t roi_bottom = (uint16_t)(y_end - 1U);

        g_gate.bbox_valid = RT_TRUE;
        g_gate.bbox_x0 = min_x;
        g_gate.bbox_y0 = min_y;
        g_gate.bbox_x1 = max_x;
        g_gate.bbox_y1 = max_y;
        g_gate.bbox_cy = bbox_cy;
        {
            uint16_t left_limit = (uint16_t)(g_gate.roi_x + g_gate.side_margin);
            uint16_t right_limit = (uint16_t)(roi_right - g_gate.side_margin);
            uint16_t top_limit = (uint16_t)(g_gate.roi_y + g_gate.top_margin);
            uint16_t bottom_limit = (uint16_t)(roi_bottom - g_gate.bottom_margin);
            uint32_t center_diff = (bbox_cy > g_gate.center_y) ?
                                   ((uint32_t)bbox_cy - (uint32_t)g_gate.center_y) :
                                   ((uint32_t)g_gate.center_y - (uint32_t)bbox_cy);
            rt_bool_t area_ok = ((g_gate.area_max == 0U) || (area <= g_gate.area_max)) ?
                                RT_TRUE : RT_FALSE;
            rt_bool_t center_ok = (center_diff <= (uint32_t)g_gate.center_tol) ?
                                  RT_TRUE : RT_FALSE;

            g_gate.candidate = ((area >= g_gate.area_on) &&
                            (bbox_w >= g_gate.min_w) &&
                            (bbox_h >= g_gate.min_h)) ? RT_TRUE : RT_FALSE;
            g_gate.inside = ((g_gate.candidate == RT_TRUE) &&
                             (min_x >= left_limit) &&
                             (max_x <= right_limit) &&
                             (min_y >= top_limit) &&
                             (max_y <= bottom_limit)) ? RT_TRUE : RT_FALSE;
            g_gate.detectable = ((g_gate.inside == RT_TRUE) &&
                                 (area_ok == RT_TRUE) &&
                                 (center_ok == RT_TRUE)) ? RT_TRUE : RT_FALSE;
            swgate_update_entry_track(roi_bottom);
            if ((g_gate.entry_filter_enabled == RT_TRUE) &&
                (g_gate.entry_allowed != RT_TRUE))
            {
                g_gate.detectable = RT_FALSE;
            }
        }
    }
    else
    {
        swgate_clear_detection();
    }
}

void swgate_update_state(void)
{
    rt_bool_t clear_frame = swgate_is_clear_frame();

    if (clear_frame == RT_TRUE)
    {
        if (g_gate.empty_count < 0xffU)
        {
            g_gate.empty_count++;
        }
    }
    else
    {
        g_gate.empty_count = 0U;
    }

    if (g_gate.enabled != RT_TRUE)
    {
        g_gate.state = SOFTWARE_GATE_STATE_IDLE;
        g_gate.stable_count = 0U;
        g_gate.empty_count = 0U;
        swgate_clear_rearm_guard();
        return;
    }

    switch (g_gate.state)
    {
    case SOFTWARE_GATE_STATE_IDLE:
        g_gate.state = SOFTWARE_GATE_STATE_WAIT;
        g_gate.stable_count = 0U;
        break;

    case SOFTWARE_GATE_STATE_WAIT:
        if (g_gate.candidate == RT_TRUE)
        {
            g_gate.state = SOFTWARE_GATE_STATE_SEEN;
            g_gate.stable_count = 0U;
            g_gate.entry_rearm_active = RT_FALSE;
        }
        break;

    case SOFTWARE_GATE_STATE_SEEN:
        if (clear_frame == RT_TRUE)
        {
            g_gate.state = SOFTWARE_GATE_STATE_WAIT;
            g_gate.stable_count = 0U;
            g_gate.entry_rearm_active = RT_FALSE;
        }
        else if (g_gate.detectable == RT_TRUE)
        {
            if ((g_gate.entry_rearm_active == RT_TRUE) &&
                (g_gate.entry_area < swgate_entry_area_on()))
            {
                g_gate.stable_count = 0U;
            }
            else if (g_gate.stable_count < 0xffU)
            {
                g_gate.stable_count++;
            }
            if (g_gate.stable_count >= g_gate.stable_frames)
            {
                g_gate.state = SOFTWARE_GATE_STATE_TRIGGERED;
                g_gate.trigger_count++;
                g_gate.last_trigger_tick = rt_tick_get();
                rt_kprintf("[gate] trigger #%lu area=%lu bbox=(%u,%u)-(%u,%u) entry=%s entry_seq=%lu entry_area=%lu stable=%u/%u\n",
                           (unsigned long)g_gate.trigger_count,
                           (unsigned long)g_gate.area,
                           (unsigned int)g_gate.bbox_x0,
                           (unsigned int)g_gate.bbox_y0,
                           (unsigned int)g_gate.bbox_x1,
                           (unsigned int)g_gate.bbox_y1,
                           swgate_entry_name(g_gate.entry_state),
                           (unsigned long)g_gate.entry_seq,
                           (unsigned long)g_gate.entry_area,
                           (unsigned int)g_gate.stable_count,
                           (unsigned int)g_gate.stable_frames);
            }
        }
        else
        {
            g_gate.stable_count = 0U;
        }
        break;

    case SOFTWARE_GATE_STATE_TRIGGERED:
        g_gate.state = SOFTWARE_GATE_STATE_COOLDOWN;
        g_gate.stable_count = 0U;
        g_gate.empty_count = 0U;
        g_gate.last_trigger_entry_seq = g_gate.entry_seq;
        g_gate.entry_rearm_pending = RT_FALSE;
        g_gate.entry_rearm_active = RT_FALSE;
        swgate_set_rearm_guard_from_bbox();
        break;

    case SOFTWARE_GATE_STATE_COOLDOWN:
        if (swgate_elapsed_ms(g_gate.last_trigger_tick, g_gate.cooldown_ms) == RT_TRUE)
        {
            /* 近间距快速通道：底部进入带由空变为占用。 */
            if ((g_gate.entry_rearm_pending == RT_TRUE) &&
                (g_gate.entry_seq != g_gate.last_trigger_entry_seq) &&
                (g_gate.entry_allowed == RT_TRUE) &&
                (g_gate.candidate == RT_TRUE))
            {
                g_gate.state = SOFTWARE_GATE_STATE_SEEN;
                g_gate.stable_count = 0U;
                g_gate.entry_rearm_pending = RT_FALSE;
                g_gate.entry_rearm_active = RT_TRUE;
                swgate_clear_rearm_guard();
            }
            else if ((g_gate.empty_count >= g_gate.rearm_empty_frames) &&
                     (swgate_rearm_guard_blocked() == RT_FALSE))
            {
                g_gate.state = SOFTWARE_GATE_STATE_WAIT;
                g_gate.stable_count = 0U;
                g_gate.entry_rearm_pending = RT_FALSE;
                g_gate.entry_rearm_active = RT_FALSE;
                swgate_clear_rearm_guard();
            }
        }
        break;

    default:
        g_gate.state = SOFTWARE_GATE_STATE_WAIT;
        g_gate.stable_count = 0U;
        g_gate.entry_rearm_active = RT_FALSE;
        break;
    }
}

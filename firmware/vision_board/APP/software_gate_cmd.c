#include "software_gate_internal.h"

static int swgate_start_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    swgate_reset_runtime();
    g_gate.enabled = RT_TRUE;
    g_gate.state = SOFTWARE_GATE_STATE_WAIT;
    rt_kprintf("[gate] start debug=%u overlay=%u\n",
               (unsigned int)g_gate.debug,
               (unsigned int)g_gate.overlay);
    return RT_EOK;
}

static int swgate_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    g_gate.enabled = RT_FALSE;
    g_gate.state = SOFTWARE_GATE_STATE_IDLE;
    g_gate.stable_count = 0U;
    swgate_clear_detection();
    swgate_clear_rearm_guard();
    rt_kprintf("[gate] stop\n");
    return RT_EOK;
}

static int swgate_stat_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    swgate_print_status();
    rt_kprintf("[gate] config_file=%s last=%s\n", STORAGE_GATE_CONFIG_FILE, g_gate_last_error);
    return RT_EOK;
}

static int swgate_perf_cmd(int argc, char **argv)
{
    software_gate_perf_t perf;

    if ((argc >= 2) && (rt_strcmp(argv[1], "clear") == 0))
    {
        software_gate_app_reset_perf();
        rt_kprintf("[gate_perf] cleared\n");
        return RT_EOK;
    }

    software_gate_app_get_perf(&perf);
    rt_kprintf("[gate_perf] count=%lu last=%lu ms max=%lu ms avg=%lu.%lu ms total=%lu ms\n",
               (unsigned long)perf.process_count,
               (unsigned long)perf.last_process_ms,
               (unsigned long)perf.max_process_ms,
               (unsigned long)(perf.avg_process_x10_ms / 10U),
               (unsigned long)(perf.avg_process_x10_ms % 10U),
               (unsigned long)perf.total_process_ms);
    rt_kprintf("[gate_perf] usage: swgate_perf [clear]\n");
    return RT_EOK;
}

static int swgate_threshold_cmd(int argc, char **argv)
{
    if (argc != 4)
    {
        rt_kprintf("[gate] usage: swgate_threshold <bright_y> <area_on> <area_off>\n");
        return -RT_EINVAL;
    }

    g_gate.bright_threshold = (uint8_t)swgate_parse_u32(argv[1], g_gate.bright_threshold);
    g_gate.area_on = swgate_parse_u32(argv[2], g_gate.area_on);
    g_gate.area_off = swgate_parse_u32(argv[3], g_gate.area_off);
    g_gate.stable_count = 0U;
    rt_kprintf("[gate] y_th=%u area_on=%lu area_off=%lu\n",
               (unsigned int)g_gate.bright_threshold,
               (unsigned long)g_gate.area_on,
               (unsigned long)g_gate.area_off);
    return RT_EOK;
}

static int swgate_bbox_cmd(int argc, char **argv)
{
    if (argc != 6)
    {
        rt_kprintf("[gate] usage: swgate_bbox <min_w> <min_h> <side_margin> <top_margin> <bottom_margin>\n");
        return -RT_EINVAL;
    }

    g_gate.min_w = (uint16_t)swgate_parse_u32(argv[1], g_gate.min_w);
    g_gate.min_h = (uint16_t)swgate_parse_u32(argv[2], g_gate.min_h);
    g_gate.side_margin = (uint16_t)swgate_parse_u32(argv[3], g_gate.side_margin);
    g_gate.top_margin = (uint16_t)swgate_parse_u32(argv[4], g_gate.top_margin);
    g_gate.bottom_margin = (uint16_t)swgate_parse_u32(argv[5], g_gate.bottom_margin);
    swgate_clamp_config();
    g_gate.stable_count = 0U;
    rt_kprintf("[gate] min=%ux%u margins side=%u top=%u bottom=%u\n",
               (unsigned int)g_gate.min_w,
               (unsigned int)g_gate.min_h,
               (unsigned int)g_gate.side_margin,
               (unsigned int)g_gate.top_margin,
               (unsigned int)g_gate.bottom_margin);
    return RT_EOK;
}

static int swgate_param_cmd(int argc, char **argv)
{
    if ((argc != 4) && (argc != 5))
    {
        rt_kprintf("[gate] usage: swgate_param <stable_frames> <cooldown_ms> <debug_period_ms> [rearm_empty_frames]\n");
        return -RT_EINVAL;
    }

    g_gate.stable_frames = (uint8_t)swgate_parse_u32(argv[1], g_gate.stable_frames);
    if (g_gate.stable_frames == 0U)
    {
        g_gate.stable_frames = 1U;
    }
    g_gate.cooldown_ms = swgate_parse_u32(argv[2], g_gate.cooldown_ms);
    g_gate.debug_period_ms = swgate_parse_u32(argv[3], g_gate.debug_period_ms);
    if (g_gate.debug_period_ms == 0U)
    {
        g_gate.debug_period_ms = 1U;
    }
    if (argc == 5)
    {
        g_gate.rearm_empty_frames = (uint8_t)swgate_parse_u32(argv[4], g_gate.rearm_empty_frames);
        if (g_gate.rearm_empty_frames == 0U)
        {
            g_gate.rearm_empty_frames = 1U;
        }
    }
    g_gate.stable_count = 0U;
    g_gate.empty_count = 0U;
    rt_kprintf("[gate] stable=%u cooldown=%lu debug_period=%lu rearm_empty=%u\n",
               (unsigned int)g_gate.stable_frames,
               (unsigned long)g_gate.cooldown_ms,
               (unsigned long)g_gate.debug_period_ms,
               (unsigned int)g_gate.rearm_empty_frames);
    return RT_EOK;
}

static int swgate_entry_cmd(int argc, char **argv)
{
    if ((argc < 1) || (argc > 6))
    {
        rt_kprintf("[gate] usage: swgate_entry [on|off] [entry_margin_px] [entry_band_h] [entry_area_on] [entry_empty_frames]\n");
        return -RT_EINVAL;
    }

    if (argc >= 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            g_gate.entry_filter_enabled = RT_TRUE;
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            g_gate.entry_filter_enabled = RT_FALSE;
        }
        else
        {
            rt_kprintf("[gate] usage: swgate_entry [on|off] [entry_margin_px] [entry_band_h] [entry_area_on] [entry_empty_frames]\n");
            return -RT_EINVAL;
        }
    }
    if (argc >= 3)
    {
        uint32_t value = swgate_parse_u32(argv[2], g_gate.entry_margin);
        g_gate.entry_margin = (value > 65535U) ? 65535U : (uint16_t)value;
    }
    if (argc >= 4)
    {
        uint32_t value = swgate_parse_u32(argv[3], g_gate.entry_band_h);
        g_gate.entry_band_h = (value > 65535U) ? 65535U : (uint16_t)value;
    }
    if (argc >= 5)
    {
        g_gate.entry_area_on = swgate_parse_u32(argv[4], g_gate.entry_area_on);
    }
    if (argc >= 6)
    {
        uint32_t value = swgate_parse_u32(argv[5], g_gate.entry_rearm_empty_frames);
        g_gate.entry_rearm_empty_frames = (value > 255U) ? 255U : (uint8_t)value;
    }

    swgate_clamp_config();
    swgate_reset_entry_track();
    g_gate.stable_count = 0U;
    g_gate.entry_empty_count = 0U;
    g_gate.entry_rearm_pending = RT_FALSE;
    rt_kprintf("[gate] entry_filter=%u entry_margin=%u entry_band=%u entry_area_on=%lu entry_empty=%u\n",
               (unsigned int)g_gate.entry_filter_enabled,
               (unsigned int)g_gate.entry_margin,
               (unsigned int)g_gate.entry_band_h,
               (unsigned long)swgate_entry_area_on(),
               (unsigned int)g_gate.entry_rearm_empty_frames);
    return RT_EOK;
}

static int swgate_debug_cmd(int argc, char **argv)
{
    if (argc > 2)
    {
        rt_kprintf("[gate] usage: swgate_debug [on|off]\n");
        return -RT_EINVAL;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            g_gate.debug = RT_TRUE;
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            g_gate.debug = RT_FALSE;
        }
        else
        {
            rt_kprintf("[gate] usage: swgate_debug [on|off]\n");
            return -RT_EINVAL;
        }
    }
    rt_kprintf("[gate] debug=%u\n", (unsigned int)g_gate.debug);
    return RT_EOK;
}

static int swgate_overlay_cmd(int argc, char **argv)
{
    if (argc > 2)
    {
        rt_kprintf("[gate] usage: swgate_overlay [on|off]\n");
        return -RT_EINVAL;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            g_gate.overlay = RT_TRUE;
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            g_gate.overlay = RT_FALSE;
        }
        else
        {
            rt_kprintf("[gate] usage: swgate_overlay [on|off]\n");
            return -RT_EINVAL;
        }
    }
    rt_kprintf("[gate] overlay=%u\n", (unsigned int)g_gate.overlay);
    return RT_EOK;
}

static int swgate_load_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (software_gate_app_load_config() == RT_EOK)
    {
        rt_kprintf("[gate] load ok: %s\n", STORAGE_GATE_CONFIG_FILE);
        swgate_print_status();
        return RT_EOK;
    }

    rt_kprintf("[gate] load failed: %s\n", g_gate_last_error);
    return -RT_ERROR;
}

static int swgate_save_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (software_gate_app_save_config() == RT_EOK)
    {
        rt_kprintf("[gate] save ok: %s\n", STORAGE_GATE_CONFIG_FILE);
        return RT_EOK;
    }

    rt_kprintf("[gate] save failed: %s\n", g_gate_last_error);
    return -RT_ERROR;
}

static int swgate_default_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    software_gate_app_reset_default();
    rt_kprintf("[gate] default restored\n");
    swgate_print_status();
    return RT_EOK;
}

MSH_CMD_EXPORT_ALIAS(swgate_start_cmd, swgate_start, start software ROI gate monitor);
MSH_CMD_EXPORT_ALIAS(swgate_stop_cmd, swgate_stop, stop software ROI gate monitor);
MSH_CMD_EXPORT_ALIAS(swgate_stat_cmd, swgate_stat, show software ROI gate state);
MSH_CMD_EXPORT_ALIAS(swgate_perf_cmd, swgate_perf, show software ROI gate performance);
MSH_CMD_EXPORT_ALIAS(swgate_threshold_cmd, swgate_threshold, set software ROI gate bright and area thresholds);
MSH_CMD_EXPORT_ALIAS(swgate_bbox_cmd, swgate_bbox, set software ROI gate bbox filters);
MSH_CMD_EXPORT_ALIAS(swgate_param_cmd, swgate_param, set software ROI gate stable and timing params);
MSH_CMD_EXPORT_ALIAS(swgate_entry_cmd, swgate_entry, set software ROI gate entry direction filter);
MSH_CMD_EXPORT_ALIAS(swgate_debug_cmd, swgate_debug, enable software ROI gate debug print);
MSH_CMD_EXPORT_ALIAS(swgate_overlay_cmd, swgate_overlay, enable software ROI gate LCD overlay);
MSH_CMD_EXPORT_ALIAS(swgate_load_cmd, swgate_load, load software ROI gate config from TF card);
MSH_CMD_EXPORT_ALIAS(swgate_save_cmd, swgate_save, save software ROI gate config to TF card);
MSH_CMD_EXPORT_ALIAS(swgate_default_cmd, swgate_default, restore software ROI gate defaults);

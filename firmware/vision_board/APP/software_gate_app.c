#include "software_gate_internal.h"

software_gate_context_t g_gate =
{
    .enabled = RT_FALSE,
    .debug = RT_FALSE,
    .overlay = RT_TRUE,
    .state = SOFTWARE_GATE_STATE_IDLE,
    .roi_x = SWGATE_DEFAULT_ROI_X,
    .roi_y = SWGATE_DEFAULT_ROI_Y,
    .roi_w = SWGATE_DEFAULT_ROI_W,
    .roi_h = SWGATE_DEFAULT_ROI_H,
    .bright_threshold = SWGATE_DEFAULT_BRIGHT_THRESHOLD,
    .area_on = SWGATE_DEFAULT_AREA_ON,
    .area_off = SWGATE_DEFAULT_AREA_OFF,
    .area_max = SWGATE_DEFAULT_AREA_MAX,
    .min_w = SWGATE_DEFAULT_MIN_W,
    .min_h = SWGATE_DEFAULT_MIN_H,
    .side_margin = SWGATE_DEFAULT_SIDE_MARGIN,
    .top_margin = SWGATE_DEFAULT_TOP_MARGIN,
    .bottom_margin = SWGATE_DEFAULT_BOTTOM_MARGIN,
    .center_y = SWGATE_DEFAULT_CENTER_Y,
    .center_tol = SWGATE_DEFAULT_CENTER_TOL,
    .entry_filter_enabled = SWGATE_DEFAULT_ENTRY_FILTER,
    .entry_margin = SWGATE_DEFAULT_ENTRY_MARGIN,
    .entry_band_h = SWGATE_DEFAULT_ENTRY_BAND_H,
    .entry_area_on = SWGATE_DEFAULT_ENTRY_AREA_ON,
    .entry_rearm_empty_frames = SWGATE_DEFAULT_ENTRY_REARM_EMPTY,
    .stable_frames = SWGATE_DEFAULT_STABLE_FRAMES,
    .rearm_empty_frames = SWGATE_DEFAULT_REARM_EMPTY_FRAMES,
    .cooldown_ms = SWGATE_DEFAULT_COOLDOWN_MS,
    .debug_period_ms = SWGATE_DEFAULT_DEBUG_PERIOD_MS,
};

software_gate_perf_context_t g_gate_perf;
const char *g_gate_last_error = "not loaded";
static char s_gate_config_save_buffer[SWGATE_CONFIG_BUFFER_SIZE];
static char s_gate_config_load_buffer[SWGATE_CONFIG_BUFFER_SIZE];

uint32_t swgate_parse_u32(const char *text, uint32_t default_value)
{
    char *end_ptr;
    unsigned long value;

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

static char *swgate_trim(char *text)
{
    char *end;

    if (text == RT_NULL)
    {
        return text;
    }

    while ((*text != '\0') && isspace((unsigned char)*text))
    {
        text++;
    }

    end = text + rt_strlen(text);
    while ((end > text) && isspace((unsigned char)*(end - 1)))
    {
        end--;
    }
    *end = '\0';

    return text;
}

static void swgate_apply_default_config(rt_bool_t keep_enabled, rt_bool_t keep_roi)
{
    rt_bool_t enabled = g_gate.enabled;
    uint16_t roi_x = g_gate.roi_x;
    uint16_t roi_y = g_gate.roi_y;
    uint16_t roi_w = g_gate.roi_w;
    uint16_t roi_h = g_gate.roi_h;

    g_gate.enabled = keep_enabled ? enabled : RT_FALSE;
    g_gate.debug = RT_FALSE;
    g_gate.overlay = RT_TRUE;
    g_gate.roi_x = keep_roi ? roi_x : SWGATE_DEFAULT_ROI_X;
    g_gate.roi_y = keep_roi ? roi_y : SWGATE_DEFAULT_ROI_Y;
    g_gate.roi_w = keep_roi ? roi_w : SWGATE_DEFAULT_ROI_W;
    g_gate.roi_h = keep_roi ? roi_h : SWGATE_DEFAULT_ROI_H;
    g_gate.bright_threshold = SWGATE_DEFAULT_BRIGHT_THRESHOLD;
    g_gate.area_on = SWGATE_DEFAULT_AREA_ON;
    g_gate.area_off = SWGATE_DEFAULT_AREA_OFF;
    g_gate.area_max = SWGATE_DEFAULT_AREA_MAX;
    g_gate.min_w = SWGATE_DEFAULT_MIN_W;
    g_gate.min_h = SWGATE_DEFAULT_MIN_H;
    g_gate.side_margin = SWGATE_DEFAULT_SIDE_MARGIN;
    g_gate.top_margin = SWGATE_DEFAULT_TOP_MARGIN;
    g_gate.bottom_margin = SWGATE_DEFAULT_BOTTOM_MARGIN;
    g_gate.center_y = SWGATE_DEFAULT_CENTER_Y;
    g_gate.center_tol = SWGATE_DEFAULT_CENTER_TOL;
    g_gate.entry_filter_enabled = SWGATE_DEFAULT_ENTRY_FILTER;
    g_gate.entry_margin = SWGATE_DEFAULT_ENTRY_MARGIN;
    g_gate.entry_band_h = SWGATE_DEFAULT_ENTRY_BAND_H;
    g_gate.entry_area_on = SWGATE_DEFAULT_ENTRY_AREA_ON;
    g_gate.entry_rearm_empty_frames = SWGATE_DEFAULT_ENTRY_REARM_EMPTY;
    g_gate.stable_frames = SWGATE_DEFAULT_STABLE_FRAMES;
    g_gate.rearm_empty_frames = SWGATE_DEFAULT_REARM_EMPTY_FRAMES;
    g_gate.cooldown_ms = SWGATE_DEFAULT_COOLDOWN_MS;
    g_gate.debug_period_ms = SWGATE_DEFAULT_DEBUG_PERIOD_MS;
    swgate_clamp_config();
}

static void swgate_parse_pair(char *key, char *value)
{
    uint32_t parsed;

    key = swgate_trim(key);
    value = swgate_trim(value);
    if ((key == RT_NULL) || (value == RT_NULL) || (*key == '\0'))
    {
        return;
    }

    parsed = swgate_parse_u32(value, 0U);
    if (rt_strcmp(key, "enabled") == 0)
    {
        g_gate.enabled = (parsed != 0U) ? RT_TRUE : RT_FALSE;
    }
    else if (rt_strcmp(key, "debug") == 0)
    {
        g_gate.debug = (parsed != 0U) ? RT_TRUE : RT_FALSE;
    }
    else if (rt_strcmp(key, "overlay") == 0)
    {
        g_gate.overlay = (parsed != 0U) ? RT_TRUE : RT_FALSE;
    }
    else if (rt_strcmp(key, "bright") == 0)
    {
        g_gate.bright_threshold = (parsed > 255U) ? 255U : (uint8_t)parsed;
    }
    else if (rt_strcmp(key, "area_on") == 0)
    {
        g_gate.area_on = parsed;
    }
    else if (rt_strcmp(key, "area_off") == 0)
    {
        g_gate.area_off = parsed;
    }
    else if (rt_strcmp(key, "area_max") == 0)
    {
        g_gate.area_max = parsed;
    }
    else if (rt_strcmp(key, "min_w") == 0)
    {
        g_gate.min_w = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "min_h") == 0)
    {
        g_gate.min_h = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "side_margin") == 0)
    {
        g_gate.side_margin = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "top_margin") == 0)
    {
        g_gate.top_margin = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "bottom_margin") == 0)
    {
        g_gate.bottom_margin = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "center_y") == 0)
    {
        g_gate.center_y = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "center_tol") == 0)
    {
        g_gate.center_tol = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "entry_filter") == 0)
    {
        g_gate.entry_filter_enabled = (parsed != 0U) ? RT_TRUE : RT_FALSE;
    }
    else if (rt_strcmp(key, "entry_margin") == 0)
    {
        g_gate.entry_margin = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "entry_band_h") == 0)
    {
        g_gate.entry_band_h = (parsed > 65535U) ? 65535U : (uint16_t)parsed;
    }
    else if (rt_strcmp(key, "entry_area_on") == 0)
    {
        g_gate.entry_area_on = parsed;
    }
    else if (rt_strcmp(key, "entry_rearm_empty_frames") == 0)
    {
        if (parsed == 0U)
        {
            parsed = 1U;
        }
        g_gate.entry_rearm_empty_frames = (parsed > 255U) ? 255U : (uint8_t)parsed;
    }
    else if (rt_strcmp(key, "stable_frames") == 0)
    {
        if (parsed == 0U)
        {
            parsed = 1U;
        }
        g_gate.stable_frames = (parsed > 255U) ? 255U : (uint8_t)parsed;
    }
    else if (rt_strcmp(key, "rearm_empty_frames") == 0)
    {
        if (parsed == 0U)
        {
            parsed = 1U;
        }
        g_gate.rearm_empty_frames = (parsed > 255U) ? 255U : (uint8_t)parsed;
    }
    else if (rt_strcmp(key, "cooldown_ms") == 0)
    {
        g_gate.cooldown_ms = parsed;
    }
    else if (rt_strcmp(key, "debug_period_ms") == 0)
    {
        g_gate.debug_period_ms = (parsed == 0U) ? 1U : parsed;
    }
}

static void swgate_parse_text(char *text)
{
    char *line = text;
    char *next;

    while ((line != RT_NULL) && (*line != '\0'))
    {
        char *eq;

        next = strchr(line, '\n');
        if (next != RT_NULL)
        {
            *next = '\0';
            next++;
        }

        line = swgate_trim(line);
        if ((*line != '\0') && (*line != '#') && (*line != ';'))
        {
            eq = strchr(line, '=');
            if (eq != RT_NULL)
            {
                *eq = '\0';
                swgate_parse_pair(line, eq + 1);
            }
        }

        line = next;
    }
}

static const char *swgate_state_name(software_gate_state_t state)
{
    switch (state)
    {
    case SOFTWARE_GATE_STATE_IDLE:
        return "IDLE";
    case SOFTWARE_GATE_STATE_WAIT:
        return "WAIT";
    case SOFTWARE_GATE_STATE_SEEN:
        return "SEEN";
    case SOFTWARE_GATE_STATE_TRIGGERED:
        return "TRIGGERED";
    case SOFTWARE_GATE_STATE_COOLDOWN:
        return "COOLDOWN";
    default:
        return "UNKNOWN";
    }
}

const char *swgate_entry_name(software_gate_entry_state_t state)
{
    switch (state)
    {
    case SOFTWARE_GATE_ENTRY_UNKNOWN:
        return "unknown";
    case SOFTWARE_GATE_ENTRY_NORMAL:
        return "normal";
    case SOFTWARE_GATE_ENTRY_BLOCKED:
        return "blocked";
    default:
        return "?";
    }
}

void swgate_clamp_config(void)
{
    if (g_gate.roi_x >= SWGATE_CAMERA_MAX_WIDTH)
    {
        g_gate.roi_x = SWGATE_CAMERA_MAX_WIDTH - 1U;
    }
    if (g_gate.roi_y >= SWGATE_CAMERA_MAX_HEIGHT)
    {
        g_gate.roi_y = SWGATE_CAMERA_MAX_HEIGHT - 1U;
    }
    if (g_gate.roi_w == 0U)
    {
        g_gate.roi_w = 1U;
    }
    if (g_gate.roi_h == 0U)
    {
        g_gate.roi_h = 1U;
    }
    if ((uint32_t)g_gate.roi_x + (uint32_t)g_gate.roi_w > SWGATE_CAMERA_MAX_WIDTH)
    {
        g_gate.roi_w = (uint16_t)(SWGATE_CAMERA_MAX_WIDTH - g_gate.roi_x);
    }
    if ((uint32_t)g_gate.roi_y + (uint32_t)g_gate.roi_h > SWGATE_CAMERA_MAX_HEIGHT)
    {
        g_gate.roi_h = (uint16_t)(SWGATE_CAMERA_MAX_HEIGHT - g_gate.roi_y);
    }
    if ((uint32_t)g_gate.side_margin * 2U >= g_gate.roi_w)
    {
        g_gate.side_margin = (uint16_t)((g_gate.roi_w > 2U) ? ((g_gate.roi_w - 2U) / 2U) : 0U);
    }
    if ((uint32_t)g_gate.top_margin + (uint32_t)g_gate.bottom_margin >= g_gate.roi_h)
    {
        g_gate.top_margin = 0U;
        g_gate.bottom_margin = (uint16_t)((g_gate.roi_h > 2U) ? 1U : 0U);
    }
    if ((g_gate.area_max != 0U) && (g_gate.area_max < g_gate.area_on))
    {
        g_gate.area_max = g_gate.area_on;
    }
    if (g_gate.center_y >= SWGATE_CAMERA_MAX_HEIGHT)
    {
        g_gate.center_y = SWGATE_CAMERA_MAX_HEIGHT - 1U;
    }
    if (g_gate.center_tol > SWGATE_CAMERA_MAX_HEIGHT)
    {
        g_gate.center_tol = SWGATE_CAMERA_MAX_HEIGHT;
    }
    if (g_gate.entry_margin == 0U)
    {
        g_gate.entry_margin = 1U;
    }
    if (g_gate.entry_margin > g_gate.roi_h)
    {
        g_gate.entry_margin = g_gate.roi_h;
    }
    if (g_gate.entry_band_h == 0U)
    {
        g_gate.entry_band_h = 1U;
    }
    if (g_gate.entry_band_h > g_gate.roi_h)
    {
        g_gate.entry_band_h = g_gate.roi_h;
    }
    if (g_gate.entry_area_on == 0U)
    {
        g_gate.entry_area_on = 1U;
    }
    {
        uint32_t entry_area_max = (uint32_t)g_gate.roi_w * (uint32_t)g_gate.entry_band_h;
        if ((entry_area_max > 0U) && (g_gate.entry_area_on > entry_area_max))
        {
            g_gate.entry_area_on = entry_area_max;
        }
    }
    if (g_gate.entry_rearm_empty_frames == 0U)
    {
        g_gate.entry_rearm_empty_frames = 1U;
    }
}

void swgate_print_status(void)
{
    rt_kprintf("[gate] enabled=%u debug=%u overlay=%u state=%s frame=%lu trigger=%lu\n",
               (unsigned int)g_gate.enabled,
               (unsigned int)g_gate.debug,
               (unsigned int)g_gate.overlay,
               swgate_state_name(g_gate.state),
               (unsigned long)g_gate.frame_count,
               (unsigned long)g_gate.trigger_count);
    rt_kprintf("[gate] roi=%u,%u,%u,%u y_th=%u area_on=%lu area_off=%lu area_max=%lu min=%ux%u margin l/r=%u top=%u bottom=%u\n",
               (unsigned int)g_gate.roi_x,
               (unsigned int)g_gate.roi_y,
               (unsigned int)g_gate.roi_w,
               (unsigned int)g_gate.roi_h,
               (unsigned int)g_gate.bright_threshold,
               (unsigned long)g_gate.area_on,
               (unsigned long)g_gate.area_off,
               (unsigned long)g_gate.area_max,
               (unsigned int)g_gate.min_w,
               (unsigned int)g_gate.min_h,
               (unsigned int)g_gate.side_margin,
               (unsigned int)g_gate.top_margin,
               (unsigned int)g_gate.bottom_margin);
    rt_kprintf("[gate] center_y=%u center_tol=%u entry=%u margin=%u band=%u entry_area_on=%lu entry_empty=%u stable=%u rearm_empty=%u cooldown=%lu\n",
               (unsigned int)g_gate.center_y,
               (unsigned int)g_gate.center_tol,
               (unsigned int)g_gate.entry_filter_enabled,
               (unsigned int)g_gate.entry_margin,
               (unsigned int)g_gate.entry_band_h,
               (unsigned long)swgate_entry_area_on(),
               (unsigned int)g_gate.entry_rearm_empty_frames,
               (unsigned int)g_gate.stable_frames,
               (unsigned int)g_gate.rearm_empty_frames,
               (unsigned long)g_gate.cooldown_ms);
    if (g_gate.bbox_valid == RT_TRUE)
    {
        rt_kprintf("[gate] area=%lu bbox=(%u,%u)-(%u,%u) size=%ux%u cy=%u candidate=%u inside=%u detectable=%u entry=%s allowed=%u start=(%u,%u,%u) entry_area=%lu occ=%u empty=%u/%u rise=%u seq=%lu pending=%u active=%u stable=%u/%u empty=%u/%u\n",
                   (unsigned long)g_gate.area,
                   (unsigned int)g_gate.bbox_x0,
                   (unsigned int)g_gate.bbox_y0,
                   (unsigned int)g_gate.bbox_x1,
                   (unsigned int)g_gate.bbox_y1,
                   (unsigned int)(g_gate.bbox_x1 - g_gate.bbox_x0 + 1U),
                   (unsigned int)(g_gate.bbox_y1 - g_gate.bbox_y0 + 1U),
                   (unsigned int)g_gate.bbox_cy,
                   (unsigned int)g_gate.candidate,
                   (unsigned int)g_gate.inside,
                   (unsigned int)g_gate.detectable,
                   swgate_entry_name(g_gate.entry_state),
                   (unsigned int)g_gate.entry_allowed,
                   (unsigned int)g_gate.entry_start_y0,
                   (unsigned int)g_gate.entry_start_y1,
                   (unsigned int)g_gate.entry_start_cy,
                   (unsigned long)g_gate.entry_area,
                   (unsigned int)g_gate.entry_occupied,
                   (unsigned int)g_gate.entry_empty_count,
                   (unsigned int)g_gate.entry_rearm_empty_frames,
                   (unsigned int)g_gate.entry_rising,
                   (unsigned long)g_gate.entry_seq,
                   (unsigned int)g_gate.entry_rearm_pending,
                   (unsigned int)g_gate.entry_rearm_active,
                   (unsigned int)g_gate.stable_count,
                   (unsigned int)g_gate.stable_frames,
                   (unsigned int)g_gate.empty_count,
                   (unsigned int)g_gate.rearm_empty_frames);
    }
    else
    {
        rt_kprintf("[gate] area=%lu bbox=none candidate=%u inside=%u detectable=%u entry=%s allowed=%u entry_area=%lu occ=%u empty=%u/%u rise=%u seq=%lu pending=%u active=%u stable=%u/%u empty=%u/%u\n",
                   (unsigned long)g_gate.area,
                   (unsigned int)g_gate.candidate,
                   (unsigned int)g_gate.inside,
                   (unsigned int)g_gate.detectable,
                   swgate_entry_name(g_gate.entry_state),
                   (unsigned int)g_gate.entry_allowed,
                   (unsigned long)g_gate.entry_area,
                   (unsigned int)g_gate.entry_occupied,
                   (unsigned int)g_gate.entry_empty_count,
                   (unsigned int)g_gate.entry_rearm_empty_frames,
                   (unsigned int)g_gate.entry_rising,
                   (unsigned long)g_gate.entry_seq,
                   (unsigned int)g_gate.entry_rearm_pending,
                   (unsigned int)g_gate.entry_rearm_active,
                   (unsigned int)g_gate.stable_count,
                   (unsigned int)g_gate.stable_frames,
                   (unsigned int)g_gate.empty_count,
                   (unsigned int)g_gate.rearm_empty_frames);
    }
}

static void swgate_maybe_debug_print(void)
{
    if (g_gate.debug != RT_TRUE)
    {
        return;
    }

    if ((g_gate.last_debug_tick != 0U) &&
        (swgate_elapsed_ms(g_gate.last_debug_tick, g_gate.debug_period_ms) != RT_TRUE))
    {
        return;
    }

    g_gate.last_debug_tick = rt_tick_get();
    swgate_print_status();
}

void software_gate_app_process_rgb565(const uint16_t *framebuffer, uint16_t width, uint16_t height)
{
    rt_tick_t start_tick;

    if (g_gate.enabled != RT_TRUE)
    {
        return;
    }

    start_tick = rt_tick_get();
    g_gate.frame_count++;
    swgate_analyze_frame(framebuffer, width, height);
    swgate_update_state();
    swgate_maybe_debug_print();
    swgate_record_process_time(start_tick);
}

void software_gate_app_set_roi(uint16_t roi_x, uint16_t roi_y, uint16_t roi_w, uint16_t roi_h)
{
    g_gate.roi_x = roi_x;
    g_gate.roi_y = roi_y;
    g_gate.roi_w = roi_w;
    g_gate.roi_h = roi_h;
    swgate_clamp_config();
    g_gate.stable_count = 0U;
    swgate_clear_detection();
    swgate_clear_rearm_guard();
}

void software_gate_app_get_overlay(software_gate_overlay_t *out_overlay)
{
    if (out_overlay == RT_NULL)
    {
        return;
    }

    memset(out_overlay, 0, sizeof(*out_overlay));
    out_overlay->visible = ((g_gate.overlay == RT_TRUE) && (g_gate.enabled == RT_TRUE)) ? RT_TRUE : RT_FALSE;
    out_overlay->roi_x = g_gate.roi_x;
    out_overlay->roi_y = g_gate.roi_y;
    out_overlay->roi_w = g_gate.roi_w;
    out_overlay->roi_h = g_gate.roi_h;
    out_overlay->bbox_valid = g_gate.bbox_valid;
    out_overlay->bbox_x0 = g_gate.bbox_x0;
    out_overlay->bbox_y0 = g_gate.bbox_y0;
    out_overlay->bbox_x1 = g_gate.bbox_x1;
    out_overlay->bbox_y1 = g_gate.bbox_y1;
    out_overlay->state = g_gate.state;
}

void software_gate_app_get_config(software_gate_config_t *out_config)
{
    if (out_config == RT_NULL)
    {
        return;
    }

    memset(out_config, 0, sizeof(*out_config));
    out_config->enabled = g_gate.enabled;
    out_config->debug = g_gate.debug;
    out_config->overlay = g_gate.overlay;
    out_config->roi_x = g_gate.roi_x;
    out_config->roi_y = g_gate.roi_y;
    out_config->roi_w = g_gate.roi_w;
    out_config->roi_h = g_gate.roi_h;
    out_config->bright_threshold = g_gate.bright_threshold;
    out_config->area_on = g_gate.area_on;
    out_config->area_off = g_gate.area_off;
    out_config->area_max = g_gate.area_max;
    out_config->min_w = g_gate.min_w;
    out_config->min_h = g_gate.min_h;
    out_config->side_margin = g_gate.side_margin;
    out_config->top_margin = g_gate.top_margin;
    out_config->bottom_margin = g_gate.bottom_margin;
    out_config->center_y = g_gate.center_y;
    out_config->center_tol = g_gate.center_tol;
    out_config->entry_filter_enabled = g_gate.entry_filter_enabled;
    out_config->entry_margin = g_gate.entry_margin;
    out_config->entry_band_h = g_gate.entry_band_h;
    out_config->entry_area_on = g_gate.entry_area_on;
    out_config->entry_rearm_empty_frames = g_gate.entry_rearm_empty_frames;
    out_config->stable_frames = g_gate.stable_frames;
    out_config->rearm_empty_frames = g_gate.rearm_empty_frames;
    out_config->cooldown_ms = g_gate.cooldown_ms;
    out_config->debug_period_ms = g_gate.debug_period_ms;
}

int software_gate_app_set_config(const software_gate_config_t *config)
{
    if (config == RT_NULL)
    {
        g_gate_last_error = "invalid config";
        return -RT_EINVAL;
    }

    g_gate.enabled = config->enabled ? RT_TRUE : RT_FALSE;
    g_gate.debug = config->debug ? RT_TRUE : RT_FALSE;
    g_gate.overlay = config->overlay ? RT_TRUE : RT_FALSE;
    g_gate.roi_x = config->roi_x;
    g_gate.roi_y = config->roi_y;
    g_gate.roi_w = config->roi_w;
    g_gate.roi_h = config->roi_h;
    g_gate.bright_threshold = config->bright_threshold;
    g_gate.area_on = config->area_on;
    g_gate.area_off = config->area_off;
    g_gate.area_max = config->area_max;
    g_gate.min_w = config->min_w;
    g_gate.min_h = config->min_h;
    g_gate.side_margin = config->side_margin;
    g_gate.top_margin = config->top_margin;
    g_gate.bottom_margin = config->bottom_margin;
    g_gate.center_y = config->center_y;
    g_gate.center_tol = config->center_tol;
    g_gate.entry_filter_enabled = config->entry_filter_enabled ? RT_TRUE : RT_FALSE;
    g_gate.entry_margin = config->entry_margin;
    g_gate.entry_band_h = config->entry_band_h;
    g_gate.entry_area_on = config->entry_area_on;
    g_gate.entry_rearm_empty_frames = (config->entry_rearm_empty_frames == 0U) ? 1U : config->entry_rearm_empty_frames;
    g_gate.stable_frames = (config->stable_frames == 0U) ? 1U : config->stable_frames;
    g_gate.rearm_empty_frames = (config->rearm_empty_frames == 0U) ? 1U : config->rearm_empty_frames;
    g_gate.cooldown_ms = config->cooldown_ms;
    g_gate.debug_period_ms = (config->debug_period_ms == 0U) ? 1U : config->debug_period_ms;

    swgate_clamp_config();
    g_gate.stable_count = 0U;
    swgate_clear_detection();
    swgate_clear_rearm_guard();
    swgate_reset_entry_track();
    if (g_gate.enabled == RT_TRUE)
    {
        if (g_gate.state == SOFTWARE_GATE_STATE_IDLE)
        {
            g_gate.state = SOFTWARE_GATE_STATE_WAIT;
        }
    }
    else
    {
        g_gate.state = SOFTWARE_GATE_STATE_IDLE;
    }

    g_gate_last_error = "ok";
    return RT_EOK;
}

void software_gate_app_get_snapshot(software_gate_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    software_gate_app_get_config(&out_snapshot->config);
    out_snapshot->state = g_gate.state;
    out_snapshot->frame_count = g_gate.frame_count;
    out_snapshot->trigger_count = g_gate.trigger_count;
    out_snapshot->area = g_gate.area;
    out_snapshot->candidate = g_gate.candidate;
    out_snapshot->inside = g_gate.inside;
    out_snapshot->detectable = g_gate.detectable;
    out_snapshot->bbox_valid = g_gate.bbox_valid;
    out_snapshot->bbox_x0 = g_gate.bbox_x0;
    out_snapshot->bbox_y0 = g_gate.bbox_y0;
    out_snapshot->bbox_x1 = g_gate.bbox_x1;
    out_snapshot->bbox_y1 = g_gate.bbox_y1;
    out_snapshot->bbox_cy = g_gate.bbox_cy;
    out_snapshot->entry_state = g_gate.entry_state;
    out_snapshot->entry_allowed = g_gate.entry_allowed;
    out_snapshot->entry_start_y0 = g_gate.entry_start_y0;
    out_snapshot->entry_start_y1 = g_gate.entry_start_y1;
    out_snapshot->entry_start_cy = g_gate.entry_start_cy;
    out_snapshot->entry_area = g_gate.entry_area;
    out_snapshot->entry_occupied = g_gate.entry_occupied;
    out_snapshot->entry_empty_count = g_gate.entry_empty_count;
    out_snapshot->entry_rising = g_gate.entry_rising;
    out_snapshot->entry_seq = g_gate.entry_seq;
    out_snapshot->entry_rearm_pending = g_gate.entry_rearm_pending;
    out_snapshot->stable_count = g_gate.stable_count;
    out_snapshot->empty_count = g_gate.empty_count;
    out_snapshot->last_error = g_gate_last_error;
}

void software_gate_app_get_perf(software_gate_perf_t *out_perf)
{
    if (out_perf == RT_NULL)
    {
        return;
    }

    memset(out_perf, 0, sizeof(*out_perf));
    out_perf->process_count = g_gate_perf.process_count;
    out_perf->last_process_ms = g_gate_perf.last_process_ms;
    out_perf->max_process_ms = g_gate_perf.max_process_ms;
    out_perf->total_process_ms = g_gate_perf.total_process_ms;
    if (g_gate_perf.process_count > 0U)
    {
        out_perf->avg_process_x10_ms =
            (g_gate_perf.total_process_ms * 10U + (g_gate_perf.process_count / 2U)) /
            g_gate_perf.process_count;
    }
}

void software_gate_app_reset_perf(void)
{
    memset(&g_gate_perf, 0, sizeof(g_gate_perf));
}

void software_gate_app_reset_default(void)
{
    swgate_apply_default_config(RT_TRUE, RT_TRUE);
    g_gate.stable_count = 0U;
    swgate_clear_detection();
    swgate_clear_rearm_guard();
    if (g_gate.enabled == RT_TRUE)
    {
        if (g_gate.state == SOFTWARE_GATE_STATE_IDLE)
        {
            g_gate.state = SOFTWARE_GATE_STATE_WAIT;
        }
    }
    else
    {
        g_gate.state = SOFTWARE_GATE_STATE_IDLE;
    }
    g_gate_last_error = "default";
}

void software_gate_app_rearm(void)
{
    g_gate.stable_count = 0U;
    g_gate.empty_count = 0U;
    g_gate.last_trigger_tick = 0U;
    swgate_clear_detection();
    swgate_clear_rearm_guard();
    g_gate.state = (g_gate.enabled == RT_TRUE) ? SOFTWARE_GATE_STATE_WAIT : SOFTWARE_GATE_STATE_IDLE;
}

int software_gate_app_save_config(void)
{
    char *text = s_gate_config_save_buffer;
    int fd;
    int len;
    int written;

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            g_gate_last_error = storage_app_last_error();
            return -RT_ERROR;
        }
    }

    len = snprintf(text,
                   SWGATE_CONFIG_BUFFER_SIZE,
                   "# Vision Board software gate config\r\n"
                   "enabled=%u\r\n"
                   "debug=%u\r\n"
                   "overlay=%u\r\n"
                   "bright=%u\r\n"
                   "area_on=%lu\r\n"
                   "area_off=%lu\r\n"
                   "area_max=%lu\r\n"
                   "min_w=%u\r\n"
                   "min_h=%u\r\n"
                   "side_margin=%u\r\n"
                   "top_margin=%u\r\n"
                   "bottom_margin=%u\r\n"
                   "center_y=%u\r\n"
                   "center_tol=%u\r\n"
                   "entry_filter=%u\r\n"
                   "entry_margin=%u\r\n"
                   "entry_band_h=%u\r\n"
                   "entry_area_on=%lu\r\n"
                   "entry_rearm_empty_frames=%u\r\n"
                   "stable_frames=%u\r\n"
                   "rearm_empty_frames=%u\r\n"
                   "cooldown_ms=%lu\r\n"
                   "debug_period_ms=%lu\r\n",
                   (unsigned int)g_gate.enabled,
                   (unsigned int)g_gate.debug,
                   (unsigned int)g_gate.overlay,
                   (unsigned int)g_gate.bright_threshold,
                   (unsigned long)g_gate.area_on,
                   (unsigned long)g_gate.area_off,
                   (unsigned long)g_gate.area_max,
                   (unsigned int)g_gate.min_w,
                   (unsigned int)g_gate.min_h,
                   (unsigned int)g_gate.side_margin,
                   (unsigned int)g_gate.top_margin,
                   (unsigned int)g_gate.bottom_margin,
                   (unsigned int)g_gate.center_y,
                   (unsigned int)g_gate.center_tol,
                   (unsigned int)g_gate.entry_filter_enabled,
                   (unsigned int)g_gate.entry_margin,
                   (unsigned int)g_gate.entry_band_h,
                   (unsigned long)g_gate.entry_area_on,
                   (unsigned int)g_gate.entry_rearm_empty_frames,
                   (unsigned int)g_gate.stable_frames,
                   (unsigned int)g_gate.rearm_empty_frames,
                   (unsigned long)g_gate.cooldown_ms,
                   (unsigned long)g_gate.debug_period_ms);
    if ((len <= 0) || (len >= (int)SWGATE_CONFIG_BUFFER_SIZE))
    {
        g_gate_last_error = "format gate config failed";
        return -RT_ERROR;
    }

    fd = open(STORAGE_GATE_CONFIG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        g_gate_last_error = "open gate config write failed";
        return -RT_ERROR;
    }

    written = write(fd, text, len);
    if (written != len)
    {
        close(fd);
        g_gate_last_error = "write gate config failed";
        return -RT_ERROR;
    }

    if (fsync(fd) != 0)
    {
        close(fd);
        g_gate_last_error = "sync gate config failed";
        return -RT_ERROR;
    }

    close(fd);
    g_gate_last_error = "ok";
    return RT_EOK;
}

int software_gate_app_load_config(void)
{
    char *buffer = s_gate_config_load_buffer;
    int fd;
    int bytes_read;

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            g_gate_last_error = storage_app_last_error();
            return -RT_ERROR;
        }
    }

    fd = open(STORAGE_GATE_CONFIG_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        swgate_apply_default_config(RT_FALSE, RT_FALSE);
        swgate_reset_runtime();
        g_gate.state = SOFTWARE_GATE_STATE_IDLE;
        if (software_gate_app_save_config() == RT_EOK)
        {
            g_gate_last_error = "default saved";
            return RT_EOK;
        }
        return -RT_ERROR;
    }

    bytes_read = read(fd, buffer, SWGATE_CONFIG_BUFFER_SIZE - 1U);
    close(fd);
    if (bytes_read < 0)
    {
        g_gate_last_error = "read gate config failed";
        return -RT_ERROR;
    }

    buffer[bytes_read] = '\0';
    swgate_apply_default_config(RT_FALSE, RT_FALSE);
    swgate_parse_text(buffer);
    swgate_clamp_config();
    swgate_reset_runtime();
    g_gate.state = (g_gate.enabled == RT_TRUE) ? SOFTWARE_GATE_STATE_WAIT : SOFTWARE_GATE_STATE_IDLE;
    g_gate_last_error = "ok";
    return RT_EOK;
}

const char *software_gate_app_last_error(void)
{
    return g_gate_last_error;
}

const char *software_gate_app_state_name(software_gate_state_t state)
{
    return swgate_state_name(state);
}

static void swgate_auto_load_entry(void *parameter)
{
    RT_UNUSED(parameter);

    /*
     * TF 卡挂载晚于应用初始化，需等根文件系统就绪后再访问 /vision，
     * 否则 DFS 会打印大量错误。
     */
    for (uint8_t i = 0U; i < 20U; i++)
    {
        if (storage_app_filesystem_is_mounted() == RT_TRUE)
        {
            if (software_gate_app_load_config() == RT_EOK)
            {
                rt_kprintf("[gate] auto load: %s (%s)\n", STORAGE_GATE_CONFIG_FILE, g_gate_last_error);
                return;
            }
        }
        rt_thread_mdelay(500U);
    }

    rt_kprintf("[gate] auto load skipped: %s\n", g_gate_last_error);
}

static int swgate_auto_load_start(void)
{
    rt_thread_t thread = rt_thread_create("swgateini",
                                          swgate_auto_load_entry,
                                          RT_NULL,
                                          SWGATE_AUTO_LOAD_STACK_SIZE,
                                          SWGATE_AUTO_LOAD_PRIORITY,
                                          SWGATE_AUTO_LOAD_TICK);
    if (thread == RT_NULL)
    {
        rt_kprintf("[gate] auto load thread create failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}

INIT_APP_EXPORT(swgate_auto_load_start);

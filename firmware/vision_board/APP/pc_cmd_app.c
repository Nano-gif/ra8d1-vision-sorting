#include "pc_cmd_app.h"

#include "pc_status_app.h"
#include "ai_app.h"
#include "camera_app.h"
#include "device_comm_app.h"
#include "detect_app.h"
#include "software_gate_app.h"
#include "sort_demo_app.h"
#include "storage_app.h"
#include "w5500_eth_app.h"

#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#define PC_CMD_THREAD_STACK_SIZE      8192U
#define PC_CMD_THREAD_PRIORITY        26U
#define PC_CMD_THREAD_TICK            10U
#define PC_CMD_POLL_MS                20U
#define PC_CMD_RX_BUFFER_SIZE         1473U
#define PC_CMD_ACK_BUFFER_SIZE        2048U
#define PC_CMD_MAX_TOKENS             40U

typedef struct
{
    rt_thread_t thread;
    rt_mutex_t lock;
    rt_bool_t ready;
    rt_bool_t running;
    uint16_t listen_port;
    uint32_t received_packets;
    uint32_t ack_packets;
    uint32_t errors;
    uint32_t last_seq;
    rt_tick_t last_rx_tick;
    int last_result;
} pc_cmd_context_t;

typedef struct
{
    const char *key;
    const char *value;
} pc_cmd_pair_t;

static pc_cmd_context_t s_pc_cmd;

static char s_pc_cmd_rx_buffer[PC_CMD_RX_BUFFER_SIZE];
static char s_pc_cmd_ack_buffer[PC_CMD_ACK_BUFFER_SIZE];
static char *s_pc_cmd_tokens[PC_CMD_MAX_TOKENS];
static char *s_pc_cmd_pair_tokens[PC_CMD_MAX_TOKENS];
static pc_cmd_pair_t s_pc_cmd_pairs[PC_CMD_MAX_TOKENS];
static uint8_t s_pc_cmd_heatmap[CAMERA_APP_HEATMAP_MAX_CELLS];
static void pc_cmd_thread_entry(void *parameter);
static void pc_cmd_handle_packet(char *text, int len, const uint8_t source_ip[4], uint16_t source_port);
static int pc_cmd_send_ack(const uint8_t dest_ip[4], uint16_t dest_port, const char *ack);

static rt_bool_t pc_cmd_parse_u32(const char *text, uint32_t *out_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0') || (text[0] == '-'))
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

static rt_bool_t pc_cmd_parse_i32(const char *text, int32_t *out_value)
{
    long value;
    char *end_ptr;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0'))
    {
        return RT_FALSE;
    }

    value = strtol(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return RT_FALSE;
    }

    *out_value = (int32_t)value;
    return RT_TRUE;
}

static const char *pc_cmd_get_value(const pc_cmd_pair_t *pairs, uint8_t pair_count, const char *key)
{
    uint8_t i;

    if ((pairs == RT_NULL) || (key == RT_NULL))
    {
        return RT_NULL;
    }

    for (i = 0U; i < pair_count; i++)
    {
        if ((pairs[i].key != RT_NULL) && (rt_strcmp(pairs[i].key, key) == 0))
        {
            return pairs[i].value;
        }
    }

    return RT_NULL;
}

static uint32_t pc_cmd_value_u32(const pc_cmd_pair_t *pairs,
                                 uint8_t pair_count,
                                 const char *key,
                                 uint32_t default_value)
{
    const char *value = pc_cmd_get_value(pairs, pair_count, key);
    uint32_t parsed;

    if (pc_cmd_parse_u32(value, &parsed) == RT_TRUE)
    {
        return parsed;
    }

    return default_value;
}

static int32_t pc_cmd_value_i32(const pc_cmd_pair_t *pairs,
                                uint8_t pair_count,
                                const char *key,
                                int32_t default_value)
{
    const char *value = pc_cmd_get_value(pairs, pair_count, key);
    int32_t parsed;

    if (pc_cmd_parse_i32(value, &parsed) == RT_TRUE)
    {
        return parsed;
    }

    return default_value;
}

static void pc_cmd_parse_pairs(char **tokens,
                               int token_count,
                               int start_index,
                               pc_cmd_pair_t *pairs,
                               uint8_t *out_pair_count)
{
    uint8_t pair_count = 0U;
    int i;

    if ((tokens == RT_NULL) || (pairs == RT_NULL) || (out_pair_count == RT_NULL))
    {
        return;
    }

    for (i = start_index; (i < token_count) && (pair_count < PC_CMD_MAX_TOKENS); i++)
    {
        char *eq = strchr(tokens[i], '=');
        if (eq == RT_NULL)
        {
            continue;
        }

        *eq = '\0';
        pairs[pair_count].key = tokens[i];
        pairs[pair_count].value = eq + 1;
        pair_count++;
    }

    *out_pair_count = pair_count;
}

static int pc_cmd_build_gate_ack(char *ack,
                                 rt_size_t ack_size,
                                 uint32_t seq,
                                 rt_bool_t ok,
                                 const char *message)
{
    software_gate_snapshot_t snapshot;
    sort_flow_align_config_t align_config;

    if ((ack == RT_NULL) || (ack_size == 0U))
    {
        return -RT_EINVAL;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    memset(&align_config, 0, sizeof(align_config));
    software_gate_app_get_snapshot(&snapshot);
    sort_flow_align_get_config(&align_config);

    return rt_snprintf(ack,
                       ack_size,
                       "vack ver=1 seq=%lu ok=%u msg=%s "
                       "gate_en=%u gate_debug=%u gate_overlay=%u gate_state=%u "
                       "gate_area=%lu gate_trig=%lu gate_candidate=%u gate_inside=%u gate_detectable=%u "
                       "gate_bright=%u gate_on=%lu gate_off=%lu gate_area_max=%lu gate_min_w=%u gate_min_h=%u "
                       "gate_side=%u gate_top=%u gate_bottom=%u gate_center_y=%u gate_center_tol=%u gate_bbox_cy=%u "
                       "gate_entry=%u gate_entry_margin=%u gate_entry_band=%u gate_entry_area_on=%lu gate_entry_rearm=%u "
                       "gate_entry_state=%u gate_entry_allowed=%u gate_entry_area=%lu gate_entry_occ=%u "
                       "gate_entry_empty=%u gate_entry_rise=%u gate_entry_seq=%lu gate_entry_pending=%u gate_stable=%u "
                       "gate_rearm=%u gate_cool=%lu gate_debug_ms=%lu "
                       "align_en=%u align_rev=%u align_target=%u align_tol=%u align_gain=%u "
                       "align_min=%lu align_max=%lu align_rpm=%u align_acc=%u align_iter=%u "
                       "align_settle=%lu",
                       (unsigned long)seq,
                       (unsigned int)ok,
                       (message != RT_NULL) ? message : "ok",
                       (unsigned int)snapshot.config.enabled,
                       (unsigned int)snapshot.config.debug,
                       (unsigned int)snapshot.config.overlay,
                       (unsigned int)snapshot.state,
                       (unsigned long)snapshot.area,
                       (unsigned long)snapshot.trigger_count,
                       (unsigned int)snapshot.candidate,
                       (unsigned int)snapshot.inside,
                       (unsigned int)snapshot.detectable,
                       (unsigned int)snapshot.config.bright_threshold,
                       (unsigned long)snapshot.config.area_on,
                       (unsigned long)snapshot.config.area_off,
                       (unsigned long)snapshot.config.area_max,
                       (unsigned int)snapshot.config.min_w,
                       (unsigned int)snapshot.config.min_h,
                       (unsigned int)snapshot.config.side_margin,
                       (unsigned int)snapshot.config.top_margin,
                       (unsigned int)snapshot.config.bottom_margin,
                       (unsigned int)snapshot.config.center_y,
                       (unsigned int)snapshot.config.center_tol,
                       (unsigned int)snapshot.bbox_cy,
                       (unsigned int)snapshot.config.entry_filter_enabled,
                       (unsigned int)snapshot.config.entry_margin,
                       (unsigned int)snapshot.config.entry_band_h,
                       (unsigned long)snapshot.config.entry_area_on,
                       (unsigned int)snapshot.config.entry_rearm_empty_frames,
                       (unsigned int)snapshot.entry_state,
                       (unsigned int)snapshot.entry_allowed,
                       (unsigned long)snapshot.entry_area,
                       (unsigned int)snapshot.entry_occupied,
                       (unsigned int)snapshot.entry_empty_count,
                       (unsigned int)snapshot.entry_rising,
                       (unsigned long)snapshot.entry_seq,
                       (unsigned int)snapshot.entry_rearm_pending,
                       (unsigned int)snapshot.config.stable_frames,
                       (unsigned int)snapshot.config.rearm_empty_frames,
                       (unsigned long)snapshot.config.cooldown_ms,
                       (unsigned long)snapshot.config.debug_period_ms,
                       (unsigned int)align_config.enabled,
                       (unsigned int)align_config.reverse_dir,
                       (unsigned int)align_config.target_y,
                       (unsigned int)align_config.tolerance_px,
                       (unsigned int)align_config.pulse_per_px,
                       (unsigned long)align_config.min_pulses,
                       (unsigned long)align_config.max_pulses,
                       (unsigned int)align_config.rpm,
                       (unsigned int)align_config.acc,
                       (unsigned int)align_config.max_iter,
                       (unsigned long)align_config.settle_ms);
}

static int pc_cmd_apply_gate_set(const pc_cmd_pair_t *pairs, uint8_t pair_count)
{
    software_gate_config_t config;

    software_gate_app_get_config(&config);
    config.bright_threshold = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "bright", config.bright_threshold);
    config.area_on = pc_cmd_value_u32(pairs, pair_count, "area_on", config.area_on);
    config.area_off = pc_cmd_value_u32(pairs, pair_count, "area_off", config.area_off);
    config.area_max = pc_cmd_value_u32(pairs, pair_count, "area_max", config.area_max);
    config.min_w = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "min_w", config.min_w);
    config.min_h = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "min_h", config.min_h);
    config.side_margin = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "side", config.side_margin);
    config.top_margin = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "top", config.top_margin);
    config.bottom_margin = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "bottom", config.bottom_margin);
    config.center_y = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "center_y", config.center_y);
    config.center_tol = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "center_tol", config.center_tol);
    config.entry_filter_enabled = pc_cmd_value_u32(pairs, pair_count, "entry", config.entry_filter_enabled) ? RT_TRUE : RT_FALSE;
    config.entry_margin = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "entry_margin", config.entry_margin);
    config.entry_band_h = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "entry_band", config.entry_band_h);
    config.entry_area_on = pc_cmd_value_u32(pairs, pair_count, "entry_area_on", config.entry_area_on);
    config.entry_rearm_empty_frames = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "entry_rearm", config.entry_rearm_empty_frames);
    config.stable_frames = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "stable", config.stable_frames);
    config.rearm_empty_frames = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "rearm", config.rearm_empty_frames);
    config.cooldown_ms = pc_cmd_value_u32(pairs, pair_count, "cooldown", config.cooldown_ms);
    config.debug_period_ms = pc_cmd_value_u32(pairs, pair_count, "debug_ms", config.debug_period_ms);
    config.debug = pc_cmd_value_u32(pairs, pair_count, "debug", config.debug) ? RT_TRUE : RT_FALSE;
    config.overlay = pc_cmd_value_u32(pairs, pair_count, "overlay", config.overlay) ? RT_TRUE : RT_FALSE;
    config.enabled = pc_cmd_value_u32(pairs, pair_count, "enabled", config.enabled) ? RT_TRUE : RT_FALSE;

    return software_gate_app_set_config(&config);
}

static int pc_cmd_apply_align_set(const pc_cmd_pair_t *pairs, uint8_t pair_count)
{
    sort_flow_align_config_t config;

    sort_flow_align_get_config(&config);
    config.enabled = pc_cmd_value_u32(pairs, pair_count, "enabled", config.enabled) ? RT_TRUE : RT_FALSE;
    config.reverse_dir = pc_cmd_value_u32(pairs, pair_count, "reverse", config.reverse_dir) ? RT_TRUE : RT_FALSE;
    config.target_y = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "target", config.target_y);
    config.tolerance_px = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "tol", config.tolerance_px);
    config.pulse_per_px = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "gain", config.pulse_per_px);
    config.min_pulses = pc_cmd_value_u32(pairs, pair_count, "min", config.min_pulses);
    config.max_pulses = pc_cmd_value_u32(pairs, pair_count, "max", config.max_pulses);
    config.rpm = (uint16_t)pc_cmd_value_u32(pairs, pair_count, "rpm", config.rpm);
    config.acc = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "acc", config.acc);
    config.max_iter = (uint8_t)pc_cmd_value_u32(pairs, pair_count, "iter", config.max_iter);
    config.settle_ms = pc_cmd_value_u32(pairs, pair_count, "settle", config.settle_ms);

    return sort_flow_align_set_config(&config);
}

static void pc_cmd_append_device_fields(char *ack, rt_size_t ack_size)
{
    device_comm_snapshot_t snapshot;
    int used;

    if ((ack == RT_NULL) || (ack_size == 0U))
    {
        return;
    }

    used = (int)rt_strlen(ack);
    if ((used <= 0) || ((rt_size_t)used >= ack_size))
    {
        return;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    (void)device_comm_app_get_snapshot(&snapshot);

    (void)rt_snprintf(ack + used,
                      ack_size - (rt_size_t)used,
                      " dev_run=%u dev_bus=%u dev_arm=%u dev_conv=%u"
                      " dev_rpm=%u dev_acc=%u dev_speed=%d dev_pos=%ld dev_last=%d"
                      " dev_poll=%lu dev_cmd=%lu"
                      " arm_ready=%u arm_homed=%u arm_moving=%u arm_err=%u arm_estop=%u"
                      " arm_mode=%u action=%u action_step=%u action_total=%u action_prog=%u"
                      " last_seq=%u last_code=%u last_result=%u",
                      (unsigned int)snapshot.service_running,
                      (unsigned int)snapshot.bus_open,
                      (unsigned int)snapshot.arm_online,
                      (unsigned int)snapshot.conveyor_online,
                      (unsigned int)snapshot.conveyor_set_rpm,
                      (unsigned int)snapshot.conveyor_acc,
                      (int)snapshot.conveyor.speed_rpm,
                      (long)snapshot.conveyor.position_raw,
                      snapshot.last_error,
                      (unsigned long)snapshot.poll_count,
                      (unsigned long)snapshot.command_count,
                      (unsigned int)((snapshot.arm_base.status_flags >> 0U) & 0x01U),
                      (unsigned int)((snapshot.arm_base.status_flags >> 3U) & 0x01U),
                      (unsigned int)((snapshot.arm_base.status_flags >> 2U) & 0x01U),
                      (unsigned int)((snapshot.arm_base.status_flags >> 6U) & 0x01U),
                      (unsigned int)((snapshot.arm_base.status_flags >> 7U) & 0x01U),
                      (unsigned int)snapshot.arm_base.control_mode,
                      (unsigned int)snapshot.arm_action.state,
                      (unsigned int)snapshot.arm_action.current_step,
                      (unsigned int)snapshot.arm_action.step_count,
                      (unsigned int)snapshot.arm_action.playback_progress,
                      (unsigned int)snapshot.arm_base.last_command_seq,
                      (unsigned int)snapshot.arm_base.last_command_code,
                      (unsigned int)snapshot.arm_base.last_command_result);
}

static int pc_cmd_parse_heatmap_csv(const char *text,
                                    uint8_t *heatmap,
                                    uint32_t expected_cells)
{
    const char *cursor;
    char *end_ptr;
    unsigned long value;
    uint32_t index = 0U;

    if ((text == RT_NULL) || (heatmap == RT_NULL) || (expected_cells == 0U))
    {
        return -RT_EINVAL;
    }

    cursor = text;
    while ((*cursor != '\0') && (index < expected_cells))
    {
        if (*cursor == ',')
        {
            return -RT_EINVAL;
        }

        value = strtoul(cursor, &end_ptr, 10);
        if (end_ptr == cursor)
        {
            return -RT_EINVAL;
        }
        if (value > 255UL)
        {
            value = 255UL;
        }
        heatmap[index++] = (uint8_t)value;

        if (*end_ptr == '\0')
        {
            cursor = end_ptr;
            break;
        }
        if (*end_ptr != ',')
        {
            return -RT_EINVAL;
        }
        cursor = end_ptr + 1;
    }

    if ((index != expected_cells) || (*cursor != '\0'))
    {
        return -RT_EINVAL;
    }

    return RT_EOK;
}

static int pc_cmd_apply_pc_result(const pc_cmd_pair_t *pairs, uint8_t pair_count)
{
    uint32_t req;
    uint32_t ok;
    uint32_t score;
    uint32_t mean;
    uint32_t hot;
    uint32_t grid_w;
    uint32_t grid_h;
    uint32_t duration;
    uint32_t cells;
    int32_t angle;
    rt_bool_t anomaly;
    uint16_t flow_duration_ms;
    uint8_t *heatmap = s_pc_cmd_heatmap;
    const char *heat_text;
    int ret;

    req = pc_cmd_value_u32(pairs, pair_count, "req", 0U);
    ok = pc_cmd_value_u32(pairs, pair_count, "ok", 0U);
    score = pc_cmd_value_u32(pairs, pair_count, "score", 0U);
    mean = pc_cmd_value_u32(pairs, pair_count, "mean", score);
    hot = pc_cmd_value_u32(pairs, pair_count, "hot", 0U);
    grid_w = pc_cmd_value_u32(pairs, pair_count, "grid_w", AI_APP_GRID_W);
    grid_h = pc_cmd_value_u32(pairs, pair_count, "grid_h", AI_APP_GRID_H);
    duration = pc_cmd_value_u32(pairs, pair_count, "dur", 0U);
    angle = pc_cmd_value_i32(pairs, pair_count, "angle", 0);
    anomaly = ok ? RT_FALSE : RT_TRUE;

    /* 网格尺寸随后按 uint8_t 传递，超过 255 会被截断且乘积可能溢出 */
    if ((grid_w == 0U) || (grid_h == 0U) || (grid_w > 255U) || (grid_h > 255U))
    {
        return -RT_EINVAL;
    }

    cells = grid_w * grid_h;
    if ((cells == 0U) || (cells > CAMERA_APP_HEATMAP_MAX_CELLS))
    {
        return -RT_EINVAL;
    }

    memset(heatmap, 0, CAMERA_APP_HEATMAP_MAX_CELLS);
    heat_text = pc_cmd_get_value(pairs, pair_count, "heat");
    ret = pc_cmd_parse_heatmap_csv(heat_text, heatmap, cells);
    if (ret != RT_EOK)
    {
        return ret;
    }

    if (score > 65535U)
    {
        score = 65535U;
    }
    if (mean > 65535U)
    {
        mean = 65535U;
    }
    if (hot > cells)
    {
        hot = cells;
    }
    if (hot > 255U)
    {
        hot = 255U;
    }
    if (duration > 65535U)
    {
        duration = 65535U;
    }
    if (angle < -1800)
    {
        angle = -1800;
    }
    if (angle > 1799)
    {
        angle = 1799;
    }

    if ((req != 0U) && (sort_flow_auto_is_waiting_for_pc_result(req) != RT_TRUE))
    {
        rt_kprintf("[pc_cmd] stale pc_result req=%lu\n", (unsigned long)req);
        return -RT_ERROR;
    }
    if ((req != 0U) &&
        (sort_flow_auto_get_pc_wait_elapsed(req, &flow_duration_ms) == RT_EOK))
    {
        duration = flow_duration_ms;
    }

    ret = detect_app_inject_pc_result(anomaly,
                                      (uint16_t)score,
                                      (uint16_t)mean,
                                      (uint8_t)hot,
                                      heatmap,
                                      (uint8_t)grid_w,
                                      (uint8_t)grid_h,
                                      (uint16_t)duration);
    if (ret != RT_EOK)
    {
        return ret;
    }

    if (req != 0U)
    {
        ret = sort_flow_auto_submit_pc_result(req, anomaly, (uint16_t)score, (int16_t)angle);
    }
    return ret;
}

static int pc_cmd_apply_device_command(const char *command, const pc_cmd_pair_t *pairs, uint8_t pair_count)
{
    uint32_t rpm;
    uint32_t acc;
    device_comm_cmd_t cmd = DEVICE_COMM_CMD_NONE;

    if (command == RT_NULL)
    {
        return -RT_EINVAL;
    }

    rpm = pc_cmd_value_u32(pairs, pair_count, "rpm", device_comm_app_get_conveyor_rpm());
    acc = pc_cmd_value_u32(pairs, pair_count, "acc", device_comm_app_get_conveyor_acc());
    if (rpm > 65535U)
    {
        rpm = 65535U;
    }
    if (acc > 255U)
    {
        acc = 255U;
    }

    if ((rt_strcmp(command, "conv_set") == 0) ||
        (rt_strcmp(command, "conv_cw") == 0) ||
        (rt_strcmp(command, "conv_ccw") == 0))
    {
        device_comm_app_set_conveyor_rpm((uint16_t)rpm);
        device_comm_app_set_conveyor_acc((uint8_t)acc);
    }

    if (rt_strcmp(command, "dev_get") == 0)
    {
        cmd = DEVICE_COMM_CMD_REFRESH;
    }
    else if (rt_strcmp(command, "conv_set") == 0)
    {
        cmd = DEVICE_COMM_CMD_REFRESH;
    }
    else if (rt_strcmp(command, "conv_cw") == 0)
    {
        cmd = DEVICE_COMM_CMD_CONVEYOR_CW;
    }
    else if (rt_strcmp(command, "conv_ccw") == 0)
    {
        cmd = DEVICE_COMM_CMD_CONVEYOR_CCW;
    }
    else if (rt_strcmp(command, "conv_stop") == 0)
    {
        cmd = DEVICE_COMM_CMD_CONVEYOR_STOP;
    }
    else if (rt_strcmp(command, "arm_refresh") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_REFRESH;
    }
    else if (rt_strcmp(command, "arm_home") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_HOME;
    }
    else if (rt_strcmp(command, "arm_play") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_PLAY;
    }
    else if (rt_strcmp(command, "arm_stop") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_STOP;
    }
    else if (rt_strcmp(command, "arm_estop") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_ESTOP;
    }
    else if (rt_strcmp(command, "arm_clear_estop") == 0)
    {
        cmd = DEVICE_COMM_CMD_ARM_CLEAR_ESTOP;
    }
    else
    {
        return -RT_EINVAL;
    }

    if (cmd != DEVICE_COMM_CMD_NONE)
    {
        device_comm_app_request(cmd);
    }
    return RT_EOK;
}

int pc_cmd_app_init(void)
{
    if (s_pc_cmd.ready == RT_TRUE)
    {
        return RT_EOK;
    }

    memset(&s_pc_cmd, 0, sizeof(s_pc_cmd));
    s_pc_cmd.listen_port = PC_CMD_DEFAULT_PORT;
    s_pc_cmd.lock = rt_mutex_create("pccmd", RT_IPC_FLAG_PRIO);
    if (s_pc_cmd.lock == RT_NULL)
    {
        return -RT_ERROR;
    }

    s_pc_cmd.thread = rt_thread_create("pccmd",
                                       pc_cmd_thread_entry,
                                       RT_NULL,
                                       PC_CMD_THREAD_STACK_SIZE,
                                       PC_CMD_THREAD_PRIORITY,
                                       PC_CMD_THREAD_TICK);
    if (s_pc_cmd.thread == RT_NULL)
    {
        rt_mutex_delete(s_pc_cmd.lock);
        s_pc_cmd.lock = RT_NULL;
        return -RT_ERROR;
    }

    s_pc_cmd.ready = RT_TRUE;
    s_pc_cmd.running = RT_TRUE;
    rt_thread_startup(s_pc_cmd.thread);
    return RT_EOK;
}

int pc_cmd_app_start(void)
{
    if (pc_cmd_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
    s_pc_cmd.running = RT_TRUE;
    rt_mutex_release(s_pc_cmd.lock);
    return RT_EOK;
}

void pc_cmd_app_stop(void)
{
    if (pc_cmd_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
    s_pc_cmd.running = RT_FALSE;
    rt_mutex_release(s_pc_cmd.lock);
}

int pc_cmd_app_get_snapshot(pc_cmd_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (pc_cmd_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
    out_snapshot->running = s_pc_cmd.running ? 1U : 0U;
    out_snapshot->listen_port = s_pc_cmd.listen_port;
    out_snapshot->received_packets = s_pc_cmd.received_packets;
    out_snapshot->ack_packets = s_pc_cmd.ack_packets;
    out_snapshot->errors = s_pc_cmd.errors;
    out_snapshot->last_seq = s_pc_cmd.last_seq;
    out_snapshot->last_rx_age_ms = (s_pc_cmd.last_rx_tick == 0) ?
                                   0xFFFFFFFFUL :
                                   (uint32_t)(((uint64_t)(rt_tick_t)(rt_tick_get() - s_pc_cmd.last_rx_tick) * 1000ULL) / RT_TICK_PER_SECOND);
    out_snapshot->last_result = s_pc_cmd.last_result;
    rt_mutex_release(s_pc_cmd.lock);
    return RT_EOK;
}

static void pc_cmd_thread_entry(void *parameter)
{
    uint8_t source_ip[4];
    uint16_t source_port;

    RT_UNUSED(parameter);

    while (1)
    {
        rt_bool_t running;
        uint16_t listen_port;

        rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
        running = s_pc_cmd.running;
        listen_port = s_pc_cmd.listen_port;
        rt_mutex_release(s_pc_cmd.lock);

        if (running == RT_TRUE)
        {
            w5500_eth_stats_t eth_stats;
            int len = 0;

            memset(&eth_stats, 0, sizeof(eth_stats));
            w5500_eth_get_stats(&eth_stats);
            if ((eth_stats.ready != 0U) && (eth_stats.link_state == W5500_ETH_LINK_ON))
            {
                len = w5500_eth_udp_recv_cmd(listen_port,
                                             s_pc_cmd_rx_buffer,
                                             sizeof(s_pc_cmd_rx_buffer) - 1U,
                                             source_ip,
                                             &source_port);
            }
            if (len > 0)
            {
                s_pc_cmd_rx_buffer[len] = '\0';
                pc_cmd_handle_packet(s_pc_cmd_rx_buffer, len, source_ip, source_port);
            }
            else if (len < 0)
            {
                rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
                s_pc_cmd.errors++;
                s_pc_cmd.last_result = len;
                rt_mutex_release(s_pc_cmd.lock);
            }
        }

        rt_thread_mdelay(PC_CMD_POLL_MS);
    }
}

static void pc_cmd_handle_packet(char *text, int len, const uint8_t source_ip[4], uint16_t source_port)
{
    char **tokens = s_pc_cmd_tokens;
    char **pair_tokens = s_pc_cmd_pair_tokens;
    pc_cmd_pair_t *pairs = s_pc_cmd_pairs;
    uint8_t pair_count = 0U;
    uint32_t seq = 0U;
    uint32_t ack_port = PC_STATUS_DEFAULT_PORT;
    char *ack = s_pc_cmd_ack_buffer;
    int ack_len;
    int token_count = 0;
    int pair_token_count = 0;
    char *cursor;
    char *command = RT_NULL;
    int ret = -RT_EINVAL;
    const char *msg = "bad_cmd";
    static char s_pc_cmd_collect_path[STORAGE_PATH_MAX]; /* capture_begin/one 路径回传缓冲 */

    RT_UNUSED(len);
    RT_UNUSED(source_port);

    cursor = strtok(text, " \r\n");
    while ((cursor != RT_NULL) && (token_count < PC_CMD_MAX_TOKENS))
    {
        tokens[token_count++] = cursor;
        cursor = strtok(RT_NULL, " \r\n");
    }

    if ((token_count < 2) || (rt_strcmp(tokens[0], "vcmd") != 0))
    {
        return;
    }

    for (int i = 1; i < token_count; i++)
    {
        if (strchr(tokens[i], '=') == RT_NULL)
        {
            if (command == RT_NULL)
            {
                command = tokens[i];
            }
        }
        else if (pair_token_count < PC_CMD_MAX_TOKENS)
        {
            pair_tokens[pair_token_count++] = tokens[i];
        }
    }

    if (command == RT_NULL)
    {
        return;
    }

    pc_cmd_parse_pairs(pair_tokens, pair_token_count, 0, pairs, &pair_count);
    seq = pc_cmd_value_u32(pairs, pair_count, "seq", 0U);
    ack_port = pc_cmd_value_u32(pairs, pair_count, "ack_port", PC_STATUS_DEFAULT_PORT);
    if ((ack_port == 0U) || (ack_port > 65535U))
    {
        ack_port = PC_STATUS_DEFAULT_PORT;
    }

    if (rt_strcmp(command, "gate_get") == 0)
    {
        ret = RT_EOK;
        msg = "gate";
    }
    else if (rt_strcmp(command, "gate_set") == 0)
    {
        ret = pc_cmd_apply_gate_set(pairs, pair_count);
        msg = (ret == RT_EOK) ? "gate_set" : "gate_set_failed";
    }
    else if (rt_strcmp(command, "gate_save") == 0)
    {
        ret = software_gate_app_save_config();
        msg = (ret == RT_EOK) ? "gate_saved" : "gate_save_failed";
    }
    else if (rt_strcmp(command, "gate_load") == 0)
    {
        ret = software_gate_app_load_config();
        msg = (ret == RT_EOK) ? "gate_loaded" : "gate_load_failed";
    }
    else if (rt_strcmp(command, "gate_default") == 0)
    {
        software_gate_app_reset_default();
        ret = RT_EOK;
        msg = "gate_default";
    }
    else if (rt_strcmp(command, "align_get") == 0)
    {
        ret = RT_EOK;
        msg = "align";
    }
    else if (rt_strcmp(command, "align_set") == 0)
    {
        ret = pc_cmd_apply_align_set(pairs, pair_count);
        msg = (ret == RT_EOK) ? "align_set" : "align_set_failed";
    }
    else if (rt_strcmp(command, "align_default") == 0)
    {
        sort_flow_align_reset_default();
        ret = RT_EOK;
        msg = "align_default";
    }
    else if (rt_strcmp(command, "pc_result") == 0)
    {
        ret = pc_cmd_apply_pc_result(pairs, pair_count);
        msg = (ret == RT_EOK) ? "pc_result" : "pc_result_failed";
    }
    else if (rt_strcmp(command, "flow_start") == 0)
    {
        /* 启动自动分拣流程(传送带+检测+分拣) */
        ret = sort_flow_auto_start();
        msg = (ret == RT_EOK) ? "flow_start" : "flow_start_failed";
    }
    else if (rt_strcmp(command, "flow_stop") == 0)
    {
        /* 停止自动分拣流程 */
        ret = sort_flow_auto_stop();
        msg = (ret == RT_EOK) ? "flow_stop" : "flow_stop_failed";
    }
    else if (rt_strcmp(command, "detect_once") == 0)
    {
        /* 单次检测(只推理一帧，不跑传送带)；底层为 void 返回，直接置成功 */
        detect_app_handle_command_from(UI_DETECT_CMD_REFRESH, "pc");
        ret = RT_EOK;
        msg = "detect_once";
    }
    else if (rt_strcmp(command, "capture_begin") == 0)
    {
        /* 开始采集会话(创建 /sd/collect/session_NNN)，成功回传会话目录路径 */
        s_pc_cmd_collect_path[0] = '\0';
        ret = storage_app_collect_begin_session(s_pc_cmd_collect_path,
                                                sizeof(s_pc_cmd_collect_path));
        msg = (ret == RT_EOK) ? s_pc_cmd_collect_path : "capture_begin_failed";
    }
    else if (rt_strcmp(command, "capture_one") == 0)
    {
        /* 采集当前帧存SD，无活动会话则先自动开会话，成功回传保存文件路径 */
        s_pc_cmd_collect_path[0] = '\0';
        ret = RT_EOK;
        if (storage_app_collect_is_active() != RT_TRUE)
        {
            ret = storage_app_collect_begin_session(s_pc_cmd_collect_path,
                                                    sizeof(s_pc_cmd_collect_path));
        }
        if (ret == RT_EOK)
        {
            s_pc_cmd_collect_path[0] = '\0';
            ret = camera_app_collect_save_current_frame(s_pc_cmd_collect_path,
                                                        sizeof(s_pc_cmd_collect_path),
                                                        3000U);
        }
        msg = (ret == RT_EOK) ? s_pc_cmd_collect_path : "capture_failed";
    }
    else if (rt_strcmp(command, "capture_end") == 0)
    {
        /* 结束当前采集会话；底层为 void 返回，直接置成功 */
        storage_app_collect_end_session();
        ret = RT_EOK;
        msg = "capture_end";
    }
    else if ((rt_strcmp(command, "dev_get") == 0) ||
             (rt_strcmp(command, "conv_set") == 0) ||
             (rt_strcmp(command, "conv_cw") == 0) ||
             (rt_strcmp(command, "conv_ccw") == 0) ||
             (rt_strcmp(command, "conv_stop") == 0) ||
             (rt_strcmp(command, "arm_refresh") == 0) ||
             (rt_strcmp(command, "arm_home") == 0) ||
             (rt_strcmp(command, "arm_play") == 0) ||
             (rt_strcmp(command, "arm_stop") == 0) ||
             (rt_strcmp(command, "arm_estop") == 0) ||
             (rt_strcmp(command, "arm_clear_estop") == 0))
    {
        ret = pc_cmd_apply_device_command(command, pairs, pair_count);
        msg = (ret == RT_EOK) ? command : "device_cmd_failed";
    }

    ack_len = pc_cmd_build_gate_ack(ack,
                                    sizeof(s_pc_cmd_ack_buffer),
                                    seq,
                                    (ret == RT_EOK) ? RT_TRUE : RT_FALSE,
                                    msg);
    if ((ack_len > 0) && (ack_len < (int)sizeof(s_pc_cmd_ack_buffer)))
    {
        rt_size_t used;

        pc_cmd_append_device_fields(ack, sizeof(s_pc_cmd_ack_buffer));
        used = rt_strlen(ack);
        if ((used + 1U) < sizeof(s_pc_cmd_ack_buffer))
        {
            ack[used] = '\n';
            ack[used + 1U] = '\0';
            ack_len = (int)(used + 1U);
        }
        else
        {
            ack_len = -RT_ERROR;
        }
    }
    if ((ack_len > 0) && (ack_len < (int)sizeof(s_pc_cmd_ack_buffer)))
    {
        ret = pc_cmd_send_ack(source_ip, (uint16_t)ack_port, ack);
    }

    rt_mutex_take(s_pc_cmd.lock, RT_WAITING_FOREVER);
    s_pc_cmd.received_packets++;
    s_pc_cmd.last_seq = seq;
    s_pc_cmd.last_rx_tick = rt_tick_get();
    s_pc_cmd.last_result = ret;
    if (ret == RT_EOK)
    {
        s_pc_cmd.ack_packets++;
    }
    else
    {
        s_pc_cmd.errors++;
    }
    rt_mutex_release(s_pc_cmd.lock);
}

static int pc_cmd_send_ack(const uint8_t dest_ip[4], uint16_t dest_port, const char *ack)
{
    if ((dest_ip == RT_NULL) || (ack == RT_NULL))
    {
        return -RT_EINVAL;
    }

    return w5500_eth_udp_send_to(dest_ip, dest_port, ack, (rt_size_t)rt_strlen(ack));
}

static int pc_cmd_cmd(int argc, char **argv)
{
    pc_cmd_snapshot_t snapshot;

    if (pc_cmd_app_init() != RT_EOK)
    {
        rt_kprintf("[pc_cmd] init failed\n");
        return -RT_ERROR;
    }

    if (argc == 2)
    {
        if (rt_strcmp(argv[1], "on") == 0)
        {
            (void)pc_cmd_app_start();
        }
        else if (rt_strcmp(argv[1], "off") == 0)
        {
            pc_cmd_app_stop();
        }
        else
        {
            rt_kprintf("[pc_cmd] usage: pc_cmd [on|off]\n");
            return -RT_EINVAL;
        }
    }
    else if (argc > 2)
    {
        rt_kprintf("[pc_cmd] usage: pc_cmd [on|off]\n");
        return -RT_EINVAL;
    }

    (void)pc_cmd_app_get_snapshot(&snapshot);
    rt_kprintf("[pc_cmd] running=%u port=%u rx=%lu ack=%lu err=%lu last_seq=%lu age=%lu last=%d\n",
               (unsigned int)snapshot.running,
               (unsigned int)snapshot.listen_port,
               (unsigned long)snapshot.received_packets,
               (unsigned long)snapshot.ack_packets,
               (unsigned long)snapshot.errors,
               (unsigned long)snapshot.last_seq,
               (unsigned long)snapshot.last_rx_age_ms,
               snapshot.last_result);
    return RT_EOK;
}

static int pc_cmd_auto_start(void)
{
    if (pc_cmd_app_init() != RT_EOK)
    {
        rt_kprintf("[pc_cmd] auto start failed\n");
        return -RT_ERROR;
    }

    rt_kprintf("[pc_cmd] service auto started port=%u\n", (unsigned int)PC_CMD_DEFAULT_PORT);
    return RT_EOK;
}

INIT_APP_EXPORT(pc_cmd_auto_start);
MSH_CMD_EXPORT_ALIAS(pc_cmd_cmd, pc_cmd, configure UDP command listener for PC HMI);

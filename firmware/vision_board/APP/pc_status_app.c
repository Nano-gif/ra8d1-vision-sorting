#include "pc_status_app.h"

#include "detect_app.h"
#include "device_comm_app.h"
#include "model_app.h"
#include "software_gate_app.h"
#include "sort_demo_app.h"
#include "w5500_eth_app.h"

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

#define PC_STATUS_THREAD_STACK_SIZE     4096U
#define PC_STATUS_THREAD_PRIORITY       25U
#define PC_STATUS_THREAD_TICK           10U
#define PC_STATUS_TEXT_MAX              2048U
#define PC_STATUS_PERIOD_MIN_MS         200U
#define PC_STATUS_PERIOD_MAX_MS         10000U
#define PC_STATUS_RESULT_OK             0U
#define PC_STATUS_RESULT_NG             1U
#define PC_STATUS_RESULT_NONE           2U

typedef struct
{
    rt_thread_t thread;
    rt_mutex_t lock;
    rt_mutex_t packet_lock;
    rt_bool_t ready;
    rt_bool_t running;
    uint8_t dest_ip[4];
    uint16_t dest_port;
    uint32_t period_ms;
    uint32_t seq;
    uint32_t sent_packets;
    uint32_t send_errors;
    int last_error;
} pc_status_context_t;

static pc_status_context_t s_pc_status;
static char s_pc_status_packet[PC_STATUS_TEXT_MAX];

static void pc_status_thread_entry(void *parameter);
static int pc_status_build_packet(char *buffer, rt_size_t buffer_size);
static int pc_status_parse_ipv4(const char *text, uint8_t ip[4]);
static rt_bool_t pc_status_parse_u32(const char *text, uint32_t *out_value);
static const char *pc_status_safe_text(const char *text);
static void pc_status_make_token(const char *text, char *out_text, rt_size_t out_size);
static int pc_status_cmd(int argc, char **argv);

int pc_status_app_init(void)
{
    if (s_pc_status.ready == RT_TRUE)
    {
        return RT_EOK;
    }

    memset(&s_pc_status, 0, sizeof(s_pc_status));
    s_pc_status.dest_ip[0] = 192U;
    s_pc_status.dest_ip[1] = 168U;
    s_pc_status.dest_ip[2] = 1U;
    s_pc_status.dest_ip[3] = 10U;
    s_pc_status.dest_port = PC_STATUS_DEFAULT_PORT;
    s_pc_status.period_ms = PC_STATUS_DEFAULT_PERIOD_MS;

    s_pc_status.lock = rt_mutex_create("pcstat", RT_IPC_FLAG_PRIO);
    if (s_pc_status.lock == RT_NULL)
    {
        return -RT_ERROR;
    }
    s_pc_status.packet_lock = rt_mutex_create("pcpkt", RT_IPC_FLAG_PRIO);
    if (s_pc_status.packet_lock == RT_NULL)
    {
        rt_mutex_delete(s_pc_status.lock);
        s_pc_status.lock = RT_NULL;
        return -RT_ERROR;
    }

    s_pc_status.thread = rt_thread_create("pcstat",
                                          pc_status_thread_entry,
                                          RT_NULL,
                                          PC_STATUS_THREAD_STACK_SIZE,
                                          PC_STATUS_THREAD_PRIORITY,
                                          PC_STATUS_THREAD_TICK);
    if (s_pc_status.thread == RT_NULL)
    {
        rt_mutex_delete(s_pc_status.packet_lock);
        rt_mutex_delete(s_pc_status.lock);
        s_pc_status.packet_lock = RT_NULL;
        s_pc_status.lock = RT_NULL;
        return -RT_ERROR;
    }

    s_pc_status.ready = RT_TRUE;
    s_pc_status.running = RT_TRUE;
    rt_thread_startup(s_pc_status.thread);
    return RT_EOK;
}

int pc_status_app_start(void)
{
    if (pc_status_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    s_pc_status.running = RT_TRUE;
    rt_mutex_release(s_pc_status.lock);
    return RT_EOK;
}

void pc_status_app_stop(void)
{
    if (pc_status_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    s_pc_status.running = RT_FALSE;
    rt_mutex_release(s_pc_status.lock);
}

int pc_status_app_send_once(void)
{
    uint8_t dest_ip[4];
    uint16_t dest_port;
    int len;
    int ret;

    if (pc_status_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_status.packet_lock, RT_WAITING_FOREVER);
    len = pc_status_build_packet(s_pc_status_packet, sizeof(s_pc_status_packet));
    if (len <= 0)
    {
        rt_mutex_release(s_pc_status.packet_lock);
        return -RT_ERROR;
    }
    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    memcpy(dest_ip, s_pc_status.dest_ip, sizeof(dest_ip));
    dest_port = s_pc_status.dest_port;
    rt_mutex_release(s_pc_status.lock);

    ret = w5500_eth_udp_send_to(dest_ip, dest_port, s_pc_status_packet, (rt_size_t)len);
    rt_mutex_release(s_pc_status.packet_lock);

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    s_pc_status.last_error = ret;
    if (ret == RT_EOK)
    {
        s_pc_status.sent_packets++;
    }
    else
    {
        s_pc_status.send_errors++;
    }
    rt_mutex_release(s_pc_status.lock);

    return ret;
}

int pc_status_app_set_target(const uint8_t ip[4], uint16_t port)
{
    if ((ip == RT_NULL) || (port == 0U))
    {
        return -RT_EINVAL;
    }

    if (pc_status_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    memcpy(s_pc_status.dest_ip, ip, sizeof(s_pc_status.dest_ip));
    s_pc_status.dest_port = port;
    rt_mutex_release(s_pc_status.lock);
    return RT_EOK;
}

int pc_status_app_set_period(uint32_t period_ms)
{
    if (period_ms < PC_STATUS_PERIOD_MIN_MS)
    {
        period_ms = PC_STATUS_PERIOD_MIN_MS;
    }
    else if (period_ms > PC_STATUS_PERIOD_MAX_MS)
    {
        period_ms = PC_STATUS_PERIOD_MAX_MS;
    }

    if (pc_status_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    s_pc_status.period_ms = period_ms;
    rt_mutex_release(s_pc_status.lock);
    return RT_EOK;
}

int pc_status_app_get_snapshot(pc_status_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (pc_status_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    out_snapshot->enabled = (s_pc_status.running == RT_TRUE) ? 1U : 0U;
    memcpy(out_snapshot->dest_ip, s_pc_status.dest_ip, sizeof(out_snapshot->dest_ip));
    out_snapshot->dest_port = s_pc_status.dest_port;
    out_snapshot->period_ms = s_pc_status.period_ms;
    out_snapshot->seq = s_pc_status.seq;
    out_snapshot->sent_packets = s_pc_status.sent_packets;
    out_snapshot->send_errors = s_pc_status.send_errors;
    out_snapshot->last_error = s_pc_status.last_error;
    rt_mutex_release(s_pc_status.lock);
    return RT_EOK;
}

static void pc_status_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        rt_bool_t running;
        uint32_t period_ms;

        rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
        running = s_pc_status.running;
        period_ms = s_pc_status.period_ms;
        rt_mutex_release(s_pc_status.lock);

        if (running == RT_TRUE)
        {
            w5500_eth_stats_t eth_stats;

            memset(&eth_stats, 0, sizeof(eth_stats));
            w5500_eth_get_stats(&eth_stats);
            if ((eth_stats.ready != 0U) && (eth_stats.link_state == W5500_ETH_LINK_ON))
            {
                (void)pc_status_app_send_once();
            }
        }

        rt_thread_mdelay(period_ms);
    }
}

static int pc_status_build_packet(char *buffer, rt_size_t buffer_size)
{
    ui_detect_snapshot_t detect_snapshot;
    detect_app_stats_t detect_stats;
    model_app_state_t model_state;
    device_comm_snapshot_t dev_snapshot;
    w5500_eth_stats_t eth_stats;
    software_gate_snapshot_t gate_snapshot;
    sort_flow_auto_snapshot_t flow_snapshot;
    uint32_t seq;
    const char *model_name = "unloaded";
    char model_token[MODEL_APP_NAME_MAX];
    uint8_t result = PC_STATUS_RESULT_NONE;
    uint8_t model_src = 0U;
    int len;

    if ((buffer == RT_NULL) || (buffer_size == 0U))
    {
        return -RT_EINVAL;
    }

    memset(&detect_snapshot, 0, sizeof(detect_snapshot));
    memset(&detect_stats, 0, sizeof(detect_stats));
    memset(&model_state, 0, sizeof(model_state));
    memset(&dev_snapshot, 0, sizeof(dev_snapshot));
    memset(&eth_stats, 0, sizeof(eth_stats));
    memset(&gate_snapshot, 0, sizeof(gate_snapshot));
    memset(&flow_snapshot, 0, sizeof(flow_snapshot));
    (void)detect_app_get_snapshot(&detect_snapshot);
    (void)detect_app_get_stats(&detect_stats);
    (void)model_app_get_state(&model_state);
    (void)device_comm_app_get_snapshot(&dev_snapshot);
    w5500_eth_get_stats(&eth_stats);
    software_gate_app_get_snapshot(&gate_snapshot);
    (void)sort_flow_auto_get_snapshot(&flow_snapshot);
    if ((model_state.metadata_loaded == RT_TRUE) && (model_state.name != RT_NULL))
    {
        model_name = model_state.name;
        model_src = (model_state.source == MODEL_APP_SOURCE_SDCARD) ? 1U : 0U;
    }

    if (detect_stats.total_count > 0U)
    {
        result = detect_snapshot.ai.anomaly ? PC_STATUS_RESULT_NG : PC_STATUS_RESULT_OK;
    }

    rt_mutex_take(s_pc_status.lock, RT_WAITING_FOREVER);
    seq = s_pc_status.seq++;
    rt_mutex_release(s_pc_status.lock);

    pc_status_make_token(model_name, model_token, sizeof(model_token));

    len = rt_snprintf(buffer,
                      buffer_size,
                      "vstat ver=1 seq=%lu model=%s model_src=%u "
                      "run=%u stage=%u total=%lu ok=%lu ng=%lu result=%u "
                      "score=%u threshold=%u frame=%lu cap_ms=%u ai_ms=%u "
                      "cam=%u link=%u eth_ready=%u "
                      "gate_en=%u gate_state=%u gate_area=%lu gate_trig=%lu "
                      "gate_candidate=%u gate_inside=%u gate_bright=%u gate_on=%lu gate_off=%lu "
                      "gate_min_w=%u gate_min_h=%u gate_side=%u gate_top=%u gate_bottom=%u "
                      "gate_entry=%u gate_entry_margin=%u gate_entry_band=%u gate_entry_area_on=%lu gate_entry_rearm=%u "
                      "gate_entry_state=%u gate_entry_allowed=%u gate_entry_area=%lu gate_entry_occ=%u "
                      "gate_entry_empty=%u gate_entry_rise=%u gate_entry_seq=%lu gate_entry_pending=%u "
                      "gate_stable=%u gate_rearm=%u gate_empty=%u gate_cool=%lu gate_debug_ms=%lu "
                      "flow_auto=%u flow_state=%u flow_pc_wait=%u flow_pc_req=%lu "
                      "flow_pc_timeout=%lu flow_pc_ok=%lu flow_pc_to=%lu flow_pc_fb=%lu "
                      "bus=%u arm=%u conv=%u rpm=%u acc=%u speed=%d pos=%ld "
                      "arm_ready=%u arm_homed=%u arm_moving=%u arm_err=%u arm_estop=%u "
                      "arm_mode=%u action=%u action_step=%u action_total=%u action_prog=%u "
                      "last_seq=%u last_code=%u last_result=%u\n",
                      (unsigned long)seq,
                      model_token,
                      (unsigned int)model_src,
                      (unsigned int)detect_snapshot.run_state,
                      (unsigned int)detect_snapshot.stage,
                      (unsigned long)detect_stats.total_count,
                      (unsigned long)detect_stats.ok_count,
                      (unsigned long)detect_stats.ng_count,
                      (unsigned int)result,
                      (unsigned int)detect_snapshot.ai.score_x10,
                      (unsigned int)detect_snapshot.ai.threshold_x10,
                      (unsigned long)detect_snapshot.camera.frame_id,
                      (unsigned int)detect_stats.capture_ms,
                      (unsigned int)detect_snapshot.ai.ai_time_ms,
                      (unsigned int)detect_app_is_ready(),
                      (unsigned int)eth_stats.link_state,
                      (unsigned int)eth_stats.ready,
                      (unsigned int)gate_snapshot.config.enabled,
                      (unsigned int)gate_snapshot.state,
                      (unsigned long)gate_snapshot.area,
                      (unsigned long)gate_snapshot.trigger_count,
                      (unsigned int)gate_snapshot.candidate,
                      (unsigned int)gate_snapshot.inside,
                      (unsigned int)gate_snapshot.config.bright_threshold,
                      (unsigned long)gate_snapshot.config.area_on,
                      (unsigned long)gate_snapshot.config.area_off,
                      (unsigned int)gate_snapshot.config.min_w,
                      (unsigned int)gate_snapshot.config.min_h,
                      (unsigned int)gate_snapshot.config.side_margin,
                      (unsigned int)gate_snapshot.config.top_margin,
                      (unsigned int)gate_snapshot.config.bottom_margin,
                      (unsigned int)gate_snapshot.config.entry_filter_enabled,
                      (unsigned int)gate_snapshot.config.entry_margin,
                      (unsigned int)gate_snapshot.config.entry_band_h,
                      (unsigned long)gate_snapshot.config.entry_area_on,
                      (unsigned int)gate_snapshot.config.entry_rearm_empty_frames,
                      (unsigned int)gate_snapshot.entry_state,
                      (unsigned int)gate_snapshot.entry_allowed,
                      (unsigned long)gate_snapshot.entry_area,
                      (unsigned int)gate_snapshot.entry_occupied,
                      (unsigned int)gate_snapshot.entry_empty_count,
                      (unsigned int)gate_snapshot.entry_rising,
                      (unsigned long)gate_snapshot.entry_seq,
                      (unsigned int)gate_snapshot.entry_rearm_pending,
                      (unsigned int)gate_snapshot.config.stable_frames,
                      (unsigned int)gate_snapshot.config.rearm_empty_frames,
                      (unsigned int)gate_snapshot.empty_count,
                      (unsigned long)gate_snapshot.config.cooldown_ms,
                      (unsigned long)gate_snapshot.config.debug_period_ms,
                      (unsigned int)flow_snapshot.active,
                      (unsigned int)flow_snapshot.state,
                      (unsigned int)flow_snapshot.pc_waiting,
                      (unsigned long)flow_snapshot.pc_req_id,
                      (unsigned long)flow_snapshot.pc_timeout_ms,
                      (unsigned long)flow_snapshot.pc_accepted_count,
                      (unsigned long)flow_snapshot.pc_timeout_count,
                      (unsigned long)flow_snapshot.pc_fallback_count,
                      (unsigned int)dev_snapshot.bus_open,
                      (unsigned int)dev_snapshot.arm_online,
                      (unsigned int)dev_snapshot.conveyor_online,
                      (unsigned int)dev_snapshot.conveyor_set_rpm,
                      (unsigned int)dev_snapshot.conveyor_acc,
                      (int)dev_snapshot.conveyor.speed_rpm,
                      (long)dev_snapshot.conveyor.position_raw,
                      (unsigned int)((dev_snapshot.arm_base.status_flags >> 0U) & 0x01U),
                      (unsigned int)((dev_snapshot.arm_base.status_flags >> 3U) & 0x01U),
                      (unsigned int)((dev_snapshot.arm_base.status_flags >> 2U) & 0x01U),
                      (unsigned int)((dev_snapshot.arm_base.status_flags >> 6U) & 0x01U),
                      (unsigned int)((dev_snapshot.arm_base.status_flags >> 7U) & 0x01U),
                      (unsigned int)dev_snapshot.arm_base.control_mode,
                      (unsigned int)dev_snapshot.arm_action.state,
                      (unsigned int)dev_snapshot.arm_action.current_step,
                      (unsigned int)dev_snapshot.arm_action.step_count,
                      (unsigned int)dev_snapshot.arm_action.playback_progress,
                      (unsigned int)dev_snapshot.arm_base.last_command_seq,
                      (unsigned int)dev_snapshot.arm_base.last_command_code,
                      (unsigned int)dev_snapshot.arm_base.last_command_result);

    if ((len <= 0) || ((rt_size_t)len >= buffer_size))
    {
        return -RT_ERROR;
    }

    return len;
}

static int pc_status_parse_ipv4(const char *text, uint8_t ip[4])
{
    uint32_t values[4] = {0U, 0U, 0U, 0U};
    uint32_t value = 0U;
    uint8_t index = 0U;
    rt_bool_t has_digit = RT_FALSE;
    const char *cursor;

    if ((text == RT_NULL) || (ip == RT_NULL))
    {
        return -RT_EINVAL;
    }

    cursor = text;
    if (*cursor == '\0')
    {
        return -RT_EINVAL;
    }

    while (*cursor != '\0')
    {
        if ((*cursor >= '0') && (*cursor <= '9'))
        {
            value = (value * 10U) + (uint32_t)(*cursor - '0');
            has_digit = RT_TRUE;
            if (value > 255U)
            {
                return -RT_EINVAL;
            }
        }
        else if (*cursor == '.')
        {
            if ((has_digit != RT_TRUE) || (index >= 3U))
            {
                return -RT_EINVAL;
            }
            values[index++] = value;
            value = 0U;
            has_digit = RT_FALSE;
        }
        else
        {
            return -RT_EINVAL;
        }

        cursor++;
    }

    if ((index != 3U) || (has_digit != RT_TRUE))
    {
        return -RT_EINVAL;
    }
    values[index] = value;

    ip[0] = (uint8_t)values[0];
    ip[1] = (uint8_t)values[1];
    ip[2] = (uint8_t)values[2];
    ip[3] = (uint8_t)values[3];
    return RT_EOK;
}

static rt_bool_t pc_status_parse_u32(const char *text, uint32_t *out_value)
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

static const char *pc_status_safe_text(const char *text)
{
    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return "unknown";
    }
    return text;
}

static void pc_status_make_token(const char *text, char *out_text, rt_size_t out_size)
{
    const char *src;
    rt_size_t dst_index = 0U;

    if ((out_text == RT_NULL) || (out_size == 0U))
    {
        return;
    }

    src = pc_status_safe_text(text);
    while ((src[0] != '\0') && (dst_index + 1U < out_size))
    {
        char ch = src[0];

        if (((ch >= '0') && (ch <= '9')) ||
            ((ch >= 'A') && (ch <= 'Z')) ||
            ((ch >= 'a') && (ch <= 'z')) ||
            (ch == '_') || (ch == '-') || (ch == '.'))
        {
            out_text[dst_index++] = ch;
        }
        else
        {
            out_text[dst_index++] = '_';
        }
        src++;
    }

    if (dst_index == 0U)
    {
        rt_strncpy(out_text, "unknown", out_size - 1U);
        out_text[out_size - 1U] = '\0';
        return;
    }

    out_text[dst_index] = '\0';
}

static int pc_status_cmd(int argc, char **argv)
{
    pc_status_snapshot_t snapshot;

    if (pc_status_app_init() != RT_EOK)
    {
        rt_kprintf("[pc_status] init failed\n");
        return -RT_ERROR;
    }

    if (argc >= 2)
    {
        if (rt_strcmp(argv[1], "on") == 0)
        {
            (void)pc_status_app_start();
        }
        else if (rt_strcmp(argv[1], "off") == 0)
        {
            pc_status_app_stop();
        }
        else if (rt_strcmp(argv[1], "once") == 0)
        {
            int ret = pc_status_app_send_once();
            rt_kprintf("[pc_status] send once ret=%d\n", ret);
        }
        else if (rt_strcmp(argv[1], "target") == 0)
        {
            uint8_t ip[4];
            uint32_t port;

            if ((argc != 4) ||
                (pc_status_parse_ipv4(argv[2], ip) != RT_EOK) ||
                (pc_status_parse_u32(argv[3], &port) != RT_TRUE) ||
                (port == 0U) || (port > 65535U))
            {
                rt_kprintf("[pc_status] usage: pc_status target <ip> <port>\n");
                return -RT_EINVAL;
            }
            (void)pc_status_app_set_target(ip, (uint16_t)port);
        }
        else if (rt_strcmp(argv[1], "period") == 0)
        {
            uint32_t period_ms;

            if ((argc != 3) || (pc_status_parse_u32(argv[2], &period_ms) != RT_TRUE))
            {
                rt_kprintf("[pc_status] usage: pc_status period <ms>\n");
                return -RT_EINVAL;
            }
            (void)pc_status_app_set_period(period_ms);
        }
        else
        {
            rt_kprintf("[pc_status] usage: pc_status [on|off|once|target <ip> <port>|period <ms>]\n");
            return -RT_EINVAL;
        }
    }

    if (pc_status_app_get_snapshot(&snapshot) != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_kprintf("[pc_status] enabled=%u target=%u.%u.%u.%u:%u period=%lu seq=%lu sent=%lu err=%lu last=%d\n",
               (unsigned int)snapshot.enabled,
               (unsigned int)snapshot.dest_ip[0],
               (unsigned int)snapshot.dest_ip[1],
               (unsigned int)snapshot.dest_ip[2],
               (unsigned int)snapshot.dest_ip[3],
               (unsigned int)snapshot.dest_port,
               (unsigned long)snapshot.period_ms,
               (unsigned long)snapshot.seq,
               (unsigned long)snapshot.sent_packets,
               (unsigned long)snapshot.send_errors,
               snapshot.last_error);
    return RT_EOK;
}

static int pc_status_auto_start(void)
{
    if (pc_status_app_init() != RT_EOK)
    {
        rt_kprintf("[pc_status] auto start failed\n");
        return -RT_ERROR;
    }

    rt_kprintf("[pc_status] service auto started\n");
    return RT_EOK;
}

INIT_APP_EXPORT(pc_status_auto_start);
MSH_CMD_EXPORT_ALIAS(pc_status_cmd, pc_status, configure UDP status packets for PC HMI);

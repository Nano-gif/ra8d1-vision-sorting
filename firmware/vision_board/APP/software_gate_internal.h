#ifndef SOFTWARE_GATE_INTERNAL_H
#define SOFTWARE_GATE_INTERNAL_H

#include "software_gate_app.h"

#include "storage_app.h"

#include <ctype.h>
#include <dfs_file.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SWGATE_DEFAULT_ROI_X              40U
#define SWGATE_DEFAULT_ROI_Y              0U
#define SWGATE_DEFAULT_ROI_W              240U
#define SWGATE_DEFAULT_ROI_H              240U
#define SWGATE_DEFAULT_BRIGHT_THRESHOLD   130U
#define SWGATE_DEFAULT_AREA_ON            1800U
#define SWGATE_DEFAULT_AREA_OFF           12000U
#define SWGATE_DEFAULT_AREA_MAX           45000U
#define SWGATE_DEFAULT_MIN_W              40U
#define SWGATE_DEFAULT_MIN_H              30U
#define SWGATE_DEFAULT_SIDE_MARGIN        5U
#define SWGATE_DEFAULT_TOP_MARGIN         5U
#define SWGATE_DEFAULT_BOTTOM_MARGIN      15U
#define SWGATE_DEFAULT_CENTER_Y           120U
#define SWGATE_DEFAULT_CENTER_TOL         55U
#define SWGATE_DEFAULT_ENTRY_FILTER       RT_TRUE
#define SWGATE_DEFAULT_ENTRY_MARGIN       45U
#define SWGATE_DEFAULT_ENTRY_BAND_H       45U
#define SWGATE_DEFAULT_ENTRY_AREA_ON      800U
#define SWGATE_DEFAULT_ENTRY_REARM_EMPTY  2U
#define SWGATE_DEFAULT_STABLE_FRAMES      3U
#define SWGATE_DEFAULT_REARM_EMPTY_FRAMES 8U
#define SWGATE_DEFAULT_COOLDOWN_MS        500U
#define SWGATE_DEFAULT_DEBUG_PERIOD_MS    500U
#define SWGATE_REARM_CLEAR_MIN_AREA       400U
#define SWGATE_REARM_CLEAR_RATIO_DIV      24U
#define SWGATE_CAMERA_MAX_WIDTH           320U
#define SWGATE_CAMERA_MAX_HEIGHT          240U
#define SWGATE_CONFIG_BUFFER_SIZE         896U
#define SWGATE_AUTO_LOAD_STACK_SIZE       4096U
#define SWGATE_AUTO_LOAD_PRIORITY         29U
#define SWGATE_AUTO_LOAD_TICK             10U

typedef struct
{
    rt_bool_t enabled;
    rt_bool_t debug;
    rt_bool_t overlay;
    software_gate_state_t state;

    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;
    uint8_t bright_threshold;
    uint32_t area_on;
    uint32_t area_off;
    uint32_t area_max;
    uint16_t min_w;
    uint16_t min_h;
    uint16_t side_margin;
    uint16_t top_margin;
    uint16_t bottom_margin;
    uint16_t center_y;
    uint16_t center_tol;
    rt_bool_t entry_filter_enabled;
    uint16_t entry_margin;
    uint16_t entry_band_h;
    uint32_t entry_area_on;
    uint8_t entry_rearm_empty_frames;
    uint8_t stable_frames;
    uint8_t rearm_empty_frames;
    uint32_t cooldown_ms;
    uint32_t debug_period_ms;

    uint32_t frame_count;
    uint32_t trigger_count;
    uint32_t area;
    rt_bool_t candidate;
    rt_bool_t inside;
    rt_bool_t detectable;
    rt_bool_t bbox_valid;
    uint16_t bbox_x0;
    uint16_t bbox_y0;
    uint16_t bbox_x1;
    uint16_t bbox_y1;
    uint16_t bbox_cy;
    software_gate_entry_state_t entry_state;
    rt_bool_t entry_allowed;
    uint16_t entry_start_y0;
    uint16_t entry_start_y1;
    uint16_t entry_start_cy;
    uint32_t entry_area;
    rt_bool_t entry_occupied;
    uint8_t entry_empty_count;
    rt_bool_t entry_rising;
    uint32_t entry_seq;
    uint32_t last_trigger_entry_seq;
    rt_bool_t entry_rearm_pending;
    rt_bool_t entry_rearm_active;
    uint8_t stable_count;
    uint8_t empty_count;
    rt_bool_t rearm_guard_valid;
    uint16_t rearm_guard_x0;
    uint16_t rearm_guard_y0;
    uint16_t rearm_guard_x1;
    uint16_t rearm_guard_y1;
    rt_tick_t last_trigger_tick;
    rt_tick_t last_debug_tick;
} software_gate_context_t;

typedef struct
{
    uint32_t process_count;
    uint32_t last_process_ms;
    uint32_t max_process_ms;
    uint32_t total_process_ms;
} software_gate_perf_context_t;

/* 模块内部共享接口，仅供本模块的拆分源文件使用。 */
extern software_gate_context_t g_gate;
extern software_gate_perf_context_t g_gate_perf;
extern const char *g_gate_last_error;
uint32_t swgate_parse_u32(const char *text, uint32_t default_value);
const char *swgate_entry_name(software_gate_entry_state_t state);
void swgate_clamp_config(void);
void swgate_print_status(void);
rt_bool_t swgate_elapsed_ms(rt_tick_t start_tick, uint32_t ms);
void swgate_reset_runtime(void);
void swgate_clear_rearm_guard(void);
void swgate_reset_entry_track(void);
uint32_t swgate_entry_area_on(void);
void swgate_clear_detection(void);
void swgate_record_process_time(rt_tick_t start_tick);
void swgate_analyze_frame(const uint16_t *framebuffer, uint16_t width, uint16_t height);
void swgate_update_state(void);

#endif /* SOFTWARE_GATE_INTERNAL_H */

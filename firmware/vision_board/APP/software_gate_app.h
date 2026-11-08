#ifndef SOFTWARE_GATE_APP_H
#define SOFTWARE_GATE_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SOFTWARE_GATE_STATE_IDLE = 0,
    SOFTWARE_GATE_STATE_WAIT,
    SOFTWARE_GATE_STATE_SEEN,
    SOFTWARE_GATE_STATE_TRIGGERED,
    SOFTWARE_GATE_STATE_COOLDOWN,
} software_gate_state_t;

typedef enum
{
    SOFTWARE_GATE_ENTRY_UNKNOWN = 0,
    SOFTWARE_GATE_ENTRY_NORMAL,
    SOFTWARE_GATE_ENTRY_BLOCKED,
} software_gate_entry_state_t;

typedef struct
{
    rt_bool_t visible;
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;
    rt_bool_t bbox_valid;
    uint16_t bbox_x0;
    uint16_t bbox_y0;
    uint16_t bbox_x1;
    uint16_t bbox_y1;
    software_gate_state_t state;
} software_gate_overlay_t;

typedef struct
{
    rt_bool_t enabled;
    rt_bool_t debug;
    rt_bool_t overlay;
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
} software_gate_config_t;

typedef struct
{
    software_gate_config_t config;
    software_gate_state_t state;
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
    rt_bool_t entry_rearm_pending;
    uint8_t stable_count;
    uint8_t empty_count;
    const char *last_error;
} software_gate_snapshot_t;

typedef struct
{
    uint32_t process_count;
    uint32_t last_process_ms;
    uint32_t max_process_ms;
    uint32_t total_process_ms;
    uint32_t avg_process_x10_ms;
} software_gate_perf_t;

void software_gate_app_process_rgb565(const uint16_t *framebuffer, uint16_t width, uint16_t height);
void software_gate_app_set_roi(uint16_t roi_x, uint16_t roi_y, uint16_t roi_w, uint16_t roi_h);
void software_gate_app_get_overlay(software_gate_overlay_t *out_overlay);
void software_gate_app_get_config(software_gate_config_t *out_config);
int software_gate_app_set_config(const software_gate_config_t *config);
void software_gate_app_get_snapshot(software_gate_snapshot_t *out_snapshot);
void software_gate_app_get_perf(software_gate_perf_t *out_perf);
void software_gate_app_reset_perf(void);
void software_gate_app_reset_default(void);
void software_gate_app_rearm(void);
int software_gate_app_load_config(void);
int software_gate_app_save_config(void);
const char *software_gate_app_last_error(void);
const char *software_gate_app_state_name(software_gate_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SOFTWARE_GATE_APP_H */

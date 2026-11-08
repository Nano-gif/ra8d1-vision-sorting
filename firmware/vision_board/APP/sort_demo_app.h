#ifndef SORT_DEMO_APP_H
#define SORT_DEMO_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int sort_demo_run_once(uint16_t seq, uint16_t rpm, uint32_t pulses, uint8_t acc);
int sort_demo_run_auto(uint32_t pulses);
int sort_flow_submit_result(rt_bool_t ng, uint16_t score_x10);
int sort_flow_auto_start(void);
int sort_flow_auto_stop(void);
rt_bool_t sort_flow_auto_is_active(void);
int sort_flow_set_transfer_comp_pulses(int32_t comp_pulses);
int32_t sort_flow_get_transfer_comp_pulses(void);
uint32_t sort_flow_get_transfer_base_pulses(void);
uint32_t sort_flow_get_transfer_pulses(void);

typedef struct
{
    rt_bool_t ready;
    rt_bool_t active;
    uint8_t state;
    uint32_t handled_count;
    uint32_t last_gate_trigger_count;
    uint32_t settle_ms;
    uint32_t detect_timeout_ms;
    uint32_t pc_timeout_ms;
    rt_bool_t pc_waiting;
    uint32_t pc_req_id;
    uint32_t pc_accepted_count;
    uint32_t pc_timeout_count;
    uint32_t pc_fallback_count;
    int last_error;
} sort_flow_auto_snapshot_t;

int sort_flow_auto_get_snapshot(sort_flow_auto_snapshot_t *out_snapshot);
rt_bool_t sort_flow_auto_is_waiting_for_pc_result(uint32_t req_id);
int sort_flow_auto_get_pc_wait_elapsed(uint32_t req_id, uint16_t *out_elapsed_ms);
int sort_flow_auto_submit_pc_result(uint32_t req_id, rt_bool_t ng, uint16_t score_x10, int16_t angle_x10);
typedef struct
{
    rt_bool_t enabled;
    rt_bool_t reverse_dir;
    uint16_t target_y;
    uint16_t tolerance_px;
    uint16_t pulse_per_px;
    uint32_t min_pulses;
    uint32_t max_pulses;
    uint16_t rpm;
    uint8_t acc;
    uint8_t max_iter;
    uint32_t settle_ms;
} sort_flow_align_config_t;

void sort_flow_align_get_config(sort_flow_align_config_t *out_config);
int sort_flow_align_set_config(const sort_flow_align_config_t *config);
void sort_flow_align_reset_default(void);

#ifdef __cplusplus
}
#endif

#endif /* SORT_DEMO_APP_H */

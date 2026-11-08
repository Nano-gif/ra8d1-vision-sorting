#ifndef SORT_DEMO_INTERNAL_H
#define SORT_DEMO_INTERNAL_H

#include "sort_demo_app.h"

#include "device_comm_app.h"
#include "detect_app.h"
#include "robot_arm_app.h"
#include "software_gate_app.h"
#include "zdt_motor_app.h"

#include <rtthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * 分拣流程模块私有头：常量、内部类型、共享状态与跨文件内部函数声明。
 * 仅供 sort_demo_app.c / sort_flow_*.c / sort_demo_cmd.c 使用，对外接口见 sort_demo_app.h。
 */

#define SORT_DEMO_ARM_ADDR              ROBOT_ARM_DEFAULT_ADDR
#define SORT_DEMO_ZDT_ADDR              ZDT_MOTOR_DEFAULT_ADDR
#define SORT_DEMO_DEFAULT_RPM           10U
#define SORT_DEMO_DEFAULT_ACC           0U
#define SORT_DEMO_TRANSFER_BASE_PULSES  4050U
#define SORT_FLOW_TRANSFER_MIN_PULSES   1000U
#define SORT_FLOW_TRANSFER_MAX_PULSES   12000U
#define SORT_FLOW_TRANSFER_COMP_MIN     (-3000)
#define SORT_FLOW_TRANSFER_COMP_MAX     3000
#define SORT_FLOW_OK_SCORE_X10          32U
#define SORT_FLOW_NG_SCORE_X10          120U
#define SORT_DEMO_ARM_POLL_MS           200U
#define SORT_DEMO_ARM_TIMEOUT_MS        20000U
#define SORT_DEMO_ZDT_POLL_MS           200U
#define SORT_DEMO_ZDT_TIMEOUT_MS        20000U
#define SORT_DEMO_BUS_CMD_RETRIES       2U
#define SORT_DEMO_BUS_RETRY_GAP_MS      80U
#define SORT_DEMO_READ_MISS_LIMIT       3U
#define SORT_FLOW_AUTO_THREAD_STACK     4096U
#define SORT_FLOW_AUTO_THREAD_PRIORITY  25U
#define SORT_FLOW_AUTO_THREAD_TICK      10U
#define SORT_FLOW_AUTO_POLL_MS          50U
#define SORT_FLOW_AUTO_SETTLE_MS        250U
#define SORT_FLOW_AUTO_DETECT_TIMEOUT_MS 35000U
#define SORT_FLOW_AUTO_PC_TIMEOUT_MS     5000U
#define SORT_FLOW_PC_WAIT_STEP_MS        100U
#define SORT_FLOW_ALIGN_DEFAULT_ENABLED RT_TRUE
#define SORT_FLOW_ALIGN_TARGET_Y        120U
#define SORT_FLOW_ALIGN_TOLERANCE_PX    15U
#define SORT_FLOW_ALIGN_PULSE_PER_PX    5U
#define SORT_FLOW_ALIGN_MIN_PULSES      30U
#define SORT_FLOW_ALIGN_MAX_PULSES      300U
#define SORT_FLOW_ALIGN_RPM             3U
#define SORT_FLOW_ALIGN_ACC             0U
#define SORT_FLOW_ALIGN_MAX_ITER        5U
#define SORT_FLOW_ALIGN_SETTLE_MS       350U
#define SORT_FLOW_ALIGN_TIMEOUT_MS      5000U
#define SORT_FLOW_ALIGN_START_DELAY_MS  80U
#define SORT_FLOW_PICK_ALIGN_TOL_RAW    1200
#define SORT_FLOW_PICK_ALIGN_MAX_RAW    50000
#define SORT_FLOW_PICK_ALIGN_RPM        3U
#define SORT_FLOW_PICK_ALIGN_ACC        0U
#define SORT_FLOW_PICK_ALIGN_MAX_ITER   2U
#define SORT_FLOW_PICK_ALIGN_SETTLE_MS  80U
#define SORT_FLOW_PICK_ALIGN_TIMEOUT_MS 5000U
#define SORT_FLOW_NG_QUEUE_SIZE         4U
#define SORT_FLOW_POSITION_SCALE_RAW    20
#define SORT_FLOW_POSITION_LEAD_RAW     5000
#define SORT_FLOW_QUEUE_GUARD_RAW       30000
#define SORT_FLOW_QUEUE_GUARD_POLL_MS   10U
#define SORT_FLOW_QUEUE_DEFAULT_CYCLE_MS 1200U
#define SORT_FLOW_QUEUE_TRANSFER_MM     200U
#define SORT_FLOW_QUEUE_ROLLER_DIAMETER_MM 35U
#define SORT_FLOW_QUEUE_PI_X1000        3142U
#define SORT_FLOW_QUEUE_GUARD_MIN_DIV   5
#define SORT_FLOW_QUEUE_GUARD_MAX_NUM   9
#define SORT_FLOW_QUEUE_GUARD_MAX_DEN   20
#define SORT_FLOW_QUEUE_GUARD_MARGIN_DIV 10
#define SORT_FLOW_GATE_DUP_RAW          15000

#define SORT_DEMO_STATUS_READY          (1U << 0)
#define SORT_DEMO_STATUS_HMI_ACTIVE     (1U << 4)
#define SORT_DEMO_STATUS_ERROR          (1U << 6)
#define SORT_DEMO_STATUS_ESTOP          (1U << 7)
#define SORT_DEMO_ZDT_REACHED           (1U << 1)

#define SORT_DEMO_ACTION_READY          0U
#define SORT_DEMO_ACTION_PLAYING        2U
#define SORT_DEMO_ACTION_DONE           4U
#define SORT_DEMO_ACTION_ERROR          5U

typedef struct
{
    rt_bool_t active;
    rt_bool_t busy;
    uint32_t ok_count;
    uint32_t ng_count;
    uint16_t last_score_x10;
    rt_bool_t last_ng;
    int last_error;
} sort_flow_context_t;

typedef enum
{
    SORT_FLOW_AUTO_IDLE = 0,
    SORT_FLOW_AUTO_WAIT_GATE,
    SORT_FLOW_AUTO_SETTLE,
    SORT_FLOW_AUTO_ALIGN,
    SORT_FLOW_AUTO_PC_WAIT,
    SORT_FLOW_AUTO_DETECT,
    SORT_FLOW_AUTO_SORT,
    SORT_FLOW_AUTO_ERROR,
} sort_flow_auto_state_t;

typedef struct
{
    uint32_t id;
    uint16_t score_x10;
    int16_t angle_x10;
    uint32_t transfer_pulses;
    int32_t detect_pos;
    int32_t target_pos;
} sort_flow_ng_item_t;

typedef struct
{
    sort_flow_ng_item_t items[SORT_FLOW_NG_QUEUE_SIZE];
    uint8_t head;
    uint8_t count;
    uint32_t next_id;
    uint32_t queued_count;
    uint32_t picked_count;
    uint32_t dropped_count;
    int32_t last_position;
    rt_bool_t position_valid;
    rt_tick_t last_position_tick;
    uint32_t speed_raw_per_s;
    uint32_t last_cycle_ms;
    int32_t guard_current_raw;
    int32_t guard_min_raw;
    int32_t guard_max_raw;
} sort_flow_ng_queue_t;

typedef struct
{
    rt_thread_t thread;
    rt_mutex_t lock;
    rt_sem_t sem;
    rt_bool_t ready;
    rt_bool_t active;
    sort_flow_auto_state_t state;
    uint32_t last_gate_trigger_count;
    uint32_t handled_count;
    int32_t last_gate_position;
    rt_bool_t last_gate_position_valid;
    uint32_t settle_ms;
    uint32_t detect_timeout_ms;
    rt_sem_t pc_sem;
    uint32_t pc_timeout_ms;
    uint32_t pc_next_req_id;
    uint32_t pc_wait_req_id;
    rt_tick_t pc_wait_start_tick;
    rt_bool_t pc_waiting;
    rt_bool_t pc_result_ready;
    rt_bool_t pc_result_ng;
    uint16_t pc_result_score_x10;
    int16_t pc_result_angle_x10;
    uint32_t pc_accepted_count;
    uint32_t pc_timeout_count;
    uint32_t pc_fallback_count;
    int last_error;
} sort_flow_auto_context_t;

/* 模块内共享状态（定义见各 .c） */
extern sort_flow_context_t g_sort_flow;
extern sort_flow_auto_context_t g_sort_flow_auto;
extern sort_flow_ng_queue_t g_sort_flow_ng_queue;
extern sort_flow_align_config_t g_sort_flow_align;
extern int32_t g_sort_flow_position_scale_raw;
extern int32_t g_sort_flow_arrive_tolerance_raw;
extern int32_t g_sort_flow_queue_guard_raw;
extern rt_bool_t g_sort_flow_queue_guard_auto;
extern uint32_t g_sort_flow_queue_guard_log_id;

/* 模块内共享函数 */
int sort_flow_read_conveyor_position(int32_t *out_position);
int32_t sort_flow_abs_i32(int32_t value);
int32_t sort_flow_queue_get_guard_raw(void);
void sort_flow_queue_note_cycle_ms(uint32_t cycle_ms);
rt_bool_t sort_flow_gate_candidate_pending(const software_gate_snapshot_t *gate_snapshot);
void sort_flow_queue_reset(rt_bool_t reset_stats);
rt_bool_t sort_flow_queue_peek(sort_flow_ng_item_t *out_item);
void sort_flow_queue_pop(void);
int sort_flow_queue_push_ng(uint16_t score_x10, int16_t angle_x10);
rt_bool_t sort_flow_queue_item_arrived(const sort_flow_ng_item_t *item,
                                       int32_t current_position,
                                       int32_t *out_remaining);
rt_bool_t sort_flow_queue_item_guarded(const sort_flow_ng_item_t *item,
                                       int32_t current_position,
                                       int32_t *out_remaining);
int sort_flow_run_arm_at_pick_position(const sort_flow_ng_item_t *item);
void sort_flow_queue_print_status(void);
int sort_flow_gate_set_enabled(rt_bool_t enabled);
uint16_t sort_demo_next_seq(void);
void sort_demo_log_retry(const char *op, uint8_t attempt, int ret);
int sort_flow_start_conveyor(void);
int sort_flow_stop_conveyor(void);
int sort_flow_inject_result(rt_bool_t ng, uint16_t score_x10);
int sort_demo_check_arm_ready(void);
int sort_demo_wait_arm_done(void);
int sort_demo_play_arm_with_confirm(uint16_t seq, uint16_t *out_result);
int sort_flow_wait_conveyor_reached_fast(uint32_t timeout_ms);
int sort_flow_align_move(zdt_motor_dir_t dir,
                         uint16_t rpm,
                         uint8_t acc,
                         uint32_t pulses);
int sort_flow_align_before_detect(rt_bool_t *out_object_present);
int sort_flow_auto_init(void);
const char *sort_flow_auto_state_name(sort_flow_auto_state_t state);
rt_bool_t sort_flow_auto_active_now(void);
uint16_t sort_flow_auto_ticks_to_ms(rt_tick_t ticks);
void sort_flow_align_print(void);

#endif /* SORT_DEMO_INTERNAL_H */

#ifndef ARM_ACTION_APP_H_
#define ARM_ACTION_APP_H_

#include "hal_data.h"
#include "arm_control_app.h"
#include <stdbool.h>
#include <stdint.h>

#define ARM_ACTION_SLOT_INDEX       (0U)
#define ARM_ACTION_MAX_STEPS        (20U)
#define ARM_ACTION_MIN_DURATION_MS  (100U)
#define ARM_ACTION_MAX_DURATION_MS  (10000U)
#define ARM_ACTION_HOME_DURATION_MS (1000U)
#define ARM_ACTION_MOVE_DURATION_MS (1000U)

typedef enum e_arm_action_state
{
    ARM_ACTION_STATE_READY = 0,
    ARM_ACTION_STATE_RECORD,
    ARM_ACTION_STATE_PLAYING,
    ARM_ACTION_STATE_STOPPED,
    ARM_ACTION_STATE_DONE,
    ARM_ACTION_STATE_ERROR,
} arm_action_state_t;

typedef struct st_arm_action_info
{
    uint8_t slot_index;
    uint8_t step_count;
    uint8_t current_step;
    uint8_t last_step;
    arm_action_state_t state;
    uint16_t playback_progress;
    bool playing;
} arm_action_info_t;

fsp_err_t arm_action_init(void);
fsp_err_t arm_action_add_step(uint16_t duration_ms);
fsp_err_t arm_action_delete_last_step(void);
fsp_err_t arm_action_clear(void);
fsp_err_t arm_action_play(void);
fsp_err_t arm_action_play_ng(int16_t object_yaw_x10, bool param_valid);
fsp_err_t arm_action_stop(void);
fsp_err_t arm_action_home(uint16_t duration_ms, arm_control_source_t source);
fsp_err_t arm_action_move_to_targets(const uint16_t target_us[ARM_SERVO_COUNT],
                                     uint16_t duration_ms,
                                     arm_control_source_t source);
fsp_err_t arm_action_load_from_flash(void);
fsp_err_t arm_action_save_to_flash(void);
bool arm_action_has_steps(void);
void arm_action_task(void);
void arm_action_get_info(arm_action_info_t *p_info);

#endif

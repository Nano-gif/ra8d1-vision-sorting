#ifndef ARM_CONFIG_APP_H_
#define ARM_CONFIG_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

#define ARM_CONFIG_HOME_SERVO_COUNT (6U)
#define ARM_CONFIG_ACTION_SERVO_COUNT (6U)
#define ARM_CONFIG_ACTION_MAX_STEPS (20U)

typedef struct st_arm_config_action_step
{
    uint16_t servo_us[ARM_CONFIG_ACTION_SERVO_COUNT];
    uint16_t duration_ms;
} arm_config_action_step_t;

fsp_err_t arm_config_init(void);
fsp_err_t arm_config_load_home(uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT]);
fsp_err_t arm_config_save_home(const uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT]);
bool arm_config_home_is_valid(void);
fsp_err_t arm_config_load_action(arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                 uint8_t *p_step_count);
fsp_err_t arm_config_save_action(const arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                 uint8_t step_count);
bool arm_config_action_is_valid(void);

#endif

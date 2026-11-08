#ifndef ARM_CONTROL_APP_H_
#define ARM_CONTROL_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

#define ARM_SERVO_COUNT (6U)

typedef enum e_arm_control_source
{
    ARM_CONTROL_SOURCE_NONE = 0,
    ARM_CONTROL_SOURCE_DEBUG,
    ARM_CONTROL_SOURCE_HMI,
    ARM_CONTROL_SOURCE_MODBUS,
    ARM_CONTROL_SOURCE_PLAYBACK,
} arm_control_source_t;

fsp_err_t arm_control_init(void);
fsp_err_t arm_control_set_target_us(uint8_t channel, uint16_t pulse_us, arm_control_source_t source);
fsp_err_t arm_control_home(arm_control_source_t source);
fsp_err_t arm_control_save_home_from_current_targets(void);
fsp_err_t arm_control_set_home_us_all(const uint16_t home_us[ARM_SERVO_COUNT]);
void arm_control_get_home_us_all(uint16_t home_us[ARM_SERVO_COUNT]);
uint16_t arm_control_get_target_us(uint8_t channel);
uint16_t arm_control_get_current_us(uint8_t channel);
uint16_t arm_control_get_min_us(uint8_t channel);
uint16_t arm_control_get_max_us(uint8_t channel);
uint16_t arm_control_get_home_us(uint8_t channel);
arm_control_source_t arm_control_get_last_source(uint8_t channel);
bool arm_control_is_ready(void);
void arm_control_task(void);

#endif

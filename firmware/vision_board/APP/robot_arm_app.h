#ifndef ROBOT_ARM_APP_H
#define ROBOT_ARM_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_ARM_DEFAULT_ADDR          1U
#define ROBOT_ARM_AXIS_COUNT            6U

typedef enum
{
    ROBOT_ARM_COMMAND_NONE = 0,
    ROBOT_ARM_COMMAND_ESTOP = 1,
    ROBOT_ARM_COMMAND_CLEAR_ESTOP = 2,
    ROBOT_ARM_COMMAND_RECORD_START = 3,
    ROBOT_ARM_COMMAND_RECORD_STOP = 4,
    ROBOT_ARM_COMMAND_PLAY = 5,
    ROBOT_ARM_COMMAND_STOP_PLAY = 6,
    ROBOT_ARM_COMMAND_SAVE_ACTION = 7,
    ROBOT_ARM_COMMAND_CLEAR_ACTION = 8,
    ROBOT_ARM_COMMAND_HOME = 9,
} robot_arm_command_t;

typedef enum
{
    ROBOT_ARM_APPLY_NONE = 0,
    ROBOT_ARM_APPLY_AXIS = 1,
    ROBOT_ARM_APPLY_RAW_US = 2,
} robot_arm_apply_t;

typedef enum
{
    ROBOT_ARM_RESULT_NONE = 0,
    ROBOT_ARM_RESULT_ACCEPTED = 1,
    ROBOT_ARM_RESULT_BUSY = 2,
    ROBOT_ARM_RESULT_INVALID_PARAM = 3,
    ROBOT_ARM_RESULT_ESTOP = 4,
    ROBOT_ARM_RESULT_HMI_ACTIVE = 5,
    ROBOT_ARM_RESULT_ACTION_EMPTY = 6,
    ROBOT_ARM_RESULT_FAILED = 7,
    ROBOT_ARM_RESULT_UNSUPPORTED = 8,
    ROBOT_ARM_RESULT_DUPLICATE = 9,
} robot_arm_result_t;

typedef struct
{
    uint16_t device_id;
    uint16_t firmware_version;
    uint16_t status_flags;
    uint16_t error_code;
    uint16_t servo_count;
    uint16_t heartbeat_counter;
    uint16_t comm_error_count;
    uint16_t control_mode;
    uint16_t protocol_version;
    uint16_t last_command_seq;
    uint16_t last_command_code;
    uint16_t last_command_result;
} robot_arm_base_status_t;

typedef struct
{
    int16_t axis_x10[ROBOT_ARM_AXIS_COUNT];
} robot_arm_axis_status_t;

typedef struct
{
    uint16_t slot;
    uint16_t step_count;
    uint16_t current_step;
    uint16_t state;
    uint16_t playback_progress;
} robot_arm_action_status_t;

int robot_arm_read_base_status(uint8_t addr, robot_arm_base_status_t *status);
int robot_arm_read_axes(uint8_t addr, robot_arm_axis_status_t *axes);
int robot_arm_read_action_status(uint8_t addr, robot_arm_action_status_t *status);
/*
 * NG 分拣角度补偿的任务参数寄存器。
 * 0x00D0/0x00D1 相邻，通过一次 0x10 功能码（count=2）同时写入。
 * 对应的回读寄存器 0x0065~0x0067 仅用于调试，本端不读取。
 */
#define ROBOT_ARM_REG_OBJECT_YAW        0x00D0U   /* 写：object_yaw_x10（int16，单位 0.1°） */
#define ROBOT_ARM_REG_TASK_PARAM_SEQ    0x00D1U   /* 写：task_param_seq（uint16） */

int robot_arm_read_last_command(uint8_t addr, uint16_t *seq, uint16_t *code, uint16_t *result);
int robot_arm_send_command(uint8_t addr, robot_arm_command_t command, uint16_t seq);
int robot_arm_set_task_param(uint8_t addr, int16_t object_yaw_x10, uint16_t seq);
int robot_arm_emergency_stop(uint8_t addr);
int robot_arm_clear_emergency_stop(uint8_t addr);
int robot_arm_write_axis_targets(uint8_t addr, const int16_t axis_x10[ROBOT_ARM_AXIS_COUNT]);
int robot_arm_apply_axis_targets(uint8_t addr, uint16_t seq);
int robot_arm_write_raw_us_targets(uint8_t addr, const uint16_t raw_us[ROBOT_ARM_AXIS_COUNT]);
int robot_arm_apply_raw_us_targets(uint8_t addr, uint16_t seq);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_ARM_APP_H */

#ifndef DEVICE_COMM_APP_H
#define DEVICE_COMM_APP_H

#include "robot_arm_app.h"
#include "zdt_motor_app.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_COMM_DEFAULT_CONVEYOR_RPM 10U
#define DEVICE_COMM_DEFAULT_CONVEYOR_ACC 0U

typedef enum
{
    DEVICE_COMM_CMD_NONE = 0,
    DEVICE_COMM_CMD_REFRESH,
    DEVICE_COMM_CMD_CONVEYOR_CW,
    DEVICE_COMM_CMD_CONVEYOR_CCW,
    DEVICE_COMM_CMD_CONVEYOR_STOP,
    DEVICE_COMM_CMD_ARM_REFRESH,
    DEVICE_COMM_CMD_ARM_HOME,
    DEVICE_COMM_CMD_ARM_PLAY,
    DEVICE_COMM_CMD_ARM_STOP,
    DEVICE_COMM_CMD_ARM_ESTOP,
    DEVICE_COMM_CMD_ARM_CLEAR_ESTOP,
} device_comm_cmd_t;

typedef struct
{
    uint8_t service_running;
    uint8_t bus_open;
    uint8_t arm_online;
    uint8_t conveyor_online;
    uint16_t conveyor_set_rpm;
    uint8_t conveyor_acc;
    int last_error;
    uint32_t poll_count;
    uint32_t command_count;
    robot_arm_base_status_t arm_base;
    robot_arm_action_status_t arm_action;
    zdt_motor_status_t conveyor;
} device_comm_snapshot_t;

int device_comm_app_init(void);
int device_comm_app_get_snapshot(device_comm_snapshot_t *out_snapshot);
void device_comm_app_request(device_comm_cmd_t cmd);
void device_comm_app_set_conveyor_rpm(uint16_t rpm);
uint16_t device_comm_app_get_conveyor_rpm(void);
void device_comm_app_set_conveyor_acc(uint8_t acc);
uint8_t device_comm_app_get_conveyor_acc(void);
uint16_t device_comm_app_next_arm_seq(void);
const char *device_comm_arm_mode_name(uint16_t mode);
const char *device_comm_arm_action_name(uint16_t state);
const char *device_comm_arm_result_name(uint16_t result);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_COMM_APP_H */

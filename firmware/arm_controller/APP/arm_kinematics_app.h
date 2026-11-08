#ifndef ARM_KINEMATICS_APP_H_
#define ARM_KINEMATICS_APP_H_

#include "hal_data.h"
#include "arm_control_app.h"
#include <stdint.h>

typedef struct st_arm_kinematics_geometry
{
    float hbase_mm;
    float l0_mm;
    float d0_mm;
    float l1_mm;
    float l2_mm;
    float l3a_mm;
    float l3b_mm;
    float l3_total_mm;
    float grip_open_width_mm;
    float grip_close_width_mm;
} arm_kinematics_geometry_t;

typedef struct st_arm_kinematics_joint
{
    float base_deg;
    float shoulder_deg;
    float elbow_deg;
    float pitch_deg;
    float roll_deg;
    float grip_open_percent;
} arm_kinematics_joint_t;

typedef struct st_arm_kinematics_pose
{
    float x_mm;
    float y_mm;
    float z_mm;
    float pitch_deg;
    float roll_deg;
    float grip_open_percent;
} arm_kinematics_pose_t;

const arm_kinematics_geometry_t * arm_kinematics_get_geometry(void);
fsp_err_t arm_kinematics_pwm_to_joints(const uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_joint_t *p_joints);
fsp_err_t arm_kinematics_joints_to_pwm(const arm_kinematics_joint_t *p_joints,
                                       uint16_t pulse_us[ARM_SERVO_COUNT]);
fsp_err_t arm_kinematics_forward(const arm_kinematics_joint_t *p_joints,
                                 arm_kinematics_pose_t *p_pose);
fsp_err_t arm_kinematics_inverse(const arm_kinematics_pose_t *p_pose,
                                 const arm_kinematics_joint_t *p_seed,
                                 arm_kinematics_joint_t *p_joints);
fsp_err_t arm_kinematics_pose_from_pwm(const uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_pose_t *p_pose,
                                       arm_kinematics_joint_t *p_joints);
fsp_err_t arm_kinematics_pwm_from_pose(const arm_kinematics_pose_t *p_pose,
                                       const uint16_t seed_us[ARM_SERVO_COUNT],
                                       uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_joint_t *p_joints);

#endif

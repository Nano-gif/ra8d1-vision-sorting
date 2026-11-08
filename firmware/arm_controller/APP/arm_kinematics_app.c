#include "arm_kinematics_app.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define ARM_KINEMATICS_RAD_TO_DEG     (57.295779513082320876f)
#define ARM_KINEMATICS_DEG_TO_RAD     (0.017453292519943295f)
#define ARM_KINEMATICS_EPSILON        (0.0001f)
#define ARM_KINEMATICS_IK_CANDIDATES  (2U)

static const arm_kinematics_geometry_t g_arm_kinematics_geometry =
{
    80.0f,
    12.0f,
    8.8f,
    103.2f,
    127.2f,
    64.0f,
    100.0f,
    164.0f,
    55.0f,
    0.0f,
};

static const uint16_t g_arm_kinematics_min_us[ARM_SERVO_COUNT] =
{
    500U, 500U, 500U, 1500U, 500U, 500U,
};

static const uint16_t g_arm_kinematics_max_us[ARM_SERVO_COUNT] =
{
    2500U, 2327U, 2356U, 2443U, 2415U, 1500U,
};

static float arm_kinematics_deg_to_rad(float deg)
{
    return deg * ARM_KINEMATICS_DEG_TO_RAD;
}

static float arm_kinematics_rad_to_deg(float rad)
{
    return rad * ARM_KINEMATICS_RAD_TO_DEG;
}

static uint16_t arm_kinematics_float_to_u16(float value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }

    return (uint16_t)(value + 0.5f);
}

static bool arm_kinematics_float_is_finite(float value)
{
    return ((value <= 1000000.0f) && (value >= -1000000.0f));
}

static bool arm_kinematics_us_in_range(uint8_t channel, uint16_t pulse_us)
{
    return ((pulse_us >= g_arm_kinematics_min_us[channel]) &&
            (pulse_us <= g_arm_kinematics_max_us[channel]));
}

static bool arm_kinematics_us_float_in_range(uint8_t channel, float pulse_us)
{
    return ((pulse_us >= ((float)g_arm_kinematics_min_us[channel] - 0.5f)) &&
            (pulse_us <= ((float)g_arm_kinematics_max_us[channel] + 0.5f)));
}

static float arm_kinematics_absf(float value)
{
    return (value < 0.0f) ? (0.0f - value) : value;
}

static float arm_kinematics_angle_error(float a_deg, float b_deg)
{
    float diff = a_deg - b_deg;

    while (diff > 180.0f)
    {
        diff -= 360.0f;
    }

    while (diff < -180.0f)
    {
        diff += 360.0f;
    }

    return arm_kinematics_absf(diff);
}

static fsp_err_t arm_kinematics_candidate_to_pwm(const arm_kinematics_joint_t *p_joints,
                                                 uint16_t pulse_us[ARM_SERVO_COUNT])
{
    float us_value[ARM_SERVO_COUNT];

    if ((NULL == p_joints) || (NULL == pulse_us))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!arm_kinematics_float_is_finite(p_joints->base_deg) ||
        !arm_kinematics_float_is_finite(p_joints->shoulder_deg) ||
        !arm_kinematics_float_is_finite(p_joints->elbow_deg) ||
        !arm_kinematics_float_is_finite(p_joints->pitch_deg) ||
        !arm_kinematics_float_is_finite(p_joints->roll_deg) ||
        !arm_kinematics_float_is_finite(p_joints->grip_open_percent))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    us_value[0] = 1500.0f + ((p_joints->base_deg * 1000.0f) / 90.0f);
    us_value[1] = 2327.0f - ((p_joints->shoulder_deg * 1015.0f) / 90.0f);

    if (p_joints->elbow_deg <= 0.0f)
    {
        us_value[2] = 1500.0f + ((p_joints->elbow_deg * 1000.0f) / 90.0f);
    }
    else
    {
        us_value[2] = 1500.0f + ((p_joints->elbow_deg * 856.0f) / 90.0f);
    }

    us_value[3] = 1500.0f - ((p_joints->pitch_deg * 943.0f) / 90.0f);

    if (p_joints->roll_deg <= 0.0f)
    {
        us_value[4] = 1457.0f + ((p_joints->roll_deg * 957.0f) / 90.0f);
    }
    else
    {
        us_value[4] = 1457.0f + ((p_joints->roll_deg * 958.0f) / 90.0f);
    }

    us_value[5] = 1500.0f - ((p_joints->grip_open_percent * 1000.0f) / 100.0f);

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (!arm_kinematics_us_float_in_range(i, us_value[i]))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }
        pulse_us[i] = arm_kinematics_float_to_u16(us_value[i]);
        if (!arm_kinematics_us_in_range(i, pulse_us[i]))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }
    }

    return FSP_SUCCESS;
}

static float arm_kinematics_candidate_score(const arm_kinematics_joint_t *p_candidate,
                                            const arm_kinematics_joint_t *p_seed)
{
    float score;

    score = arm_kinematics_angle_error(p_candidate->base_deg, p_seed->base_deg);
    score += arm_kinematics_angle_error(p_candidate->shoulder_deg, p_seed->shoulder_deg);
    score += arm_kinematics_angle_error(p_candidate->elbow_deg, p_seed->elbow_deg);
    score += arm_kinematics_angle_error(p_candidate->pitch_deg, p_seed->pitch_deg);
    score += arm_kinematics_angle_error(p_candidate->roll_deg, p_seed->roll_deg);
    score += arm_kinematics_absf(p_candidate->grip_open_percent - p_seed->grip_open_percent) * 0.1f;

    return score;
}

const arm_kinematics_geometry_t * arm_kinematics_get_geometry(void)
{
    return &g_arm_kinematics_geometry;
}

fsp_err_t arm_kinematics_pwm_to_joints(const uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_joint_t *p_joints)
{
    uint16_t us;

    if ((NULL == pulse_us) || (NULL == p_joints))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (!arm_kinematics_us_in_range(i, pulse_us[i]))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }
    }

    p_joints->base_deg = (((float)pulse_us[0] - 1500.0f) * 90.0f) / 1000.0f;
    p_joints->shoulder_deg = (2327.0f - (float)pulse_us[1]) * 90.0f / 1015.0f;

    us = pulse_us[2];
    if (us <= 1500U)
    {
        p_joints->elbow_deg = (((float)us - 1500.0f) * 90.0f) / 1000.0f;
    }
    else
    {
        p_joints->elbow_deg = (((float)us - 1500.0f) * 90.0f) / 856.0f;
    }

    p_joints->pitch_deg = 0.0f - ((((float)pulse_us[3] - 1500.0f) * 90.0f) / 943.0f);

    us = pulse_us[4];
    if (us <= 1457U)
    {
        p_joints->roll_deg = (((float)us - 1457.0f) * 90.0f) / 957.0f;
    }
    else
    {
        p_joints->roll_deg = (((float)us - 1457.0f) * 90.0f) / 958.0f;
    }

    p_joints->grip_open_percent = (1500.0f - (float)pulse_us[5]) * 100.0f / 1000.0f;

    return FSP_SUCCESS;
}

fsp_err_t arm_kinematics_joints_to_pwm(const arm_kinematics_joint_t *p_joints,
                                       uint16_t pulse_us[ARM_SERVO_COUNT])
{
    return arm_kinematics_candidate_to_pwm(p_joints, pulse_us);
}

fsp_err_t arm_kinematics_forward(const arm_kinematics_joint_t *p_joints,
                                 arm_kinematics_pose_t *p_pose)
{
    float base_rad;
    float shoulder_rad;
    float elbow_rad;
    float tool_pitch_rad;
    float r_mm;

    if ((NULL == p_joints) || (NULL == p_pose))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    base_rad = arm_kinematics_deg_to_rad(p_joints->base_deg);
    shoulder_rad = arm_kinematics_deg_to_rad(p_joints->shoulder_deg);
    elbow_rad = arm_kinematics_deg_to_rad(p_joints->shoulder_deg + p_joints->elbow_deg);
    tool_pitch_rad = arm_kinematics_deg_to_rad(p_joints->shoulder_deg +
                                               p_joints->elbow_deg +
                                               p_joints->pitch_deg);

    r_mm = g_arm_kinematics_geometry.d0_mm;
    r_mm += g_arm_kinematics_geometry.l1_mm * cosf(shoulder_rad);
    r_mm += g_arm_kinematics_geometry.l2_mm * cosf(elbow_rad);
    r_mm += g_arm_kinematics_geometry.l3_total_mm * cosf(tool_pitch_rad);

    p_pose->x_mm = r_mm * cosf(base_rad);
    p_pose->y_mm = r_mm * sinf(base_rad);
    p_pose->z_mm = g_arm_kinematics_geometry.l0_mm;
    p_pose->z_mm += g_arm_kinematics_geometry.l1_mm * sinf(shoulder_rad);
    p_pose->z_mm += g_arm_kinematics_geometry.l2_mm * sinf(elbow_rad);
    p_pose->z_mm += g_arm_kinematics_geometry.l3_total_mm * sinf(tool_pitch_rad);
    p_pose->pitch_deg = p_joints->shoulder_deg + p_joints->elbow_deg + p_joints->pitch_deg;
    p_pose->roll_deg = p_joints->roll_deg;
    p_pose->grip_open_percent = p_joints->grip_open_percent;

    return FSP_SUCCESS;
}

fsp_err_t arm_kinematics_inverse(const arm_kinematics_pose_t *p_pose,
                                 const arm_kinematics_joint_t *p_seed,
                                 arm_kinematics_joint_t *p_joints)
{
    arm_kinematics_joint_t candidate[ARM_KINEMATICS_IK_CANDIDATES];
    arm_kinematics_joint_t seed;
    uint16_t pulse_us[ARM_SERVO_COUNT];
    float r_tcp_mm;
    float tool_pitch_rad;
    float wrist_r_mm;
    float wrist_z_mm;
    float distance_sq;
    float cos_elbow;
    float elbow_abs_rad;
    float elbow_rad[ARM_KINEMATICS_IK_CANDIDATES];
    float shoulder_rad;
    float best_score = 1000000.0f;
    float score;
    bool found = false;
    uint8_t best_index = 0U;

    if ((NULL == p_pose) || (NULL == p_joints))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!arm_kinematics_float_is_finite(p_pose->x_mm) ||
        !arm_kinematics_float_is_finite(p_pose->y_mm) ||
        !arm_kinematics_float_is_finite(p_pose->z_mm) ||
        !arm_kinematics_float_is_finite(p_pose->pitch_deg) ||
        !arm_kinematics_float_is_finite(p_pose->roll_deg) ||
        !arm_kinematics_float_is_finite(p_pose->grip_open_percent))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if ((p_pose->grip_open_percent < 0.0f) || (p_pose->grip_open_percent > 100.0f))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (NULL == p_seed)
    {
        seed.base_deg = 0.0f;
        seed.shoulder_deg = 90.0f;
        seed.elbow_deg = 0.0f;
        seed.pitch_deg = 0.0f;
        seed.roll_deg = 0.0f;
        seed.grip_open_percent = p_pose->grip_open_percent;
    }
    else
    {
        seed = *p_seed;
    }

    r_tcp_mm = sqrtf((p_pose->x_mm * p_pose->x_mm) + (p_pose->y_mm * p_pose->y_mm));
    tool_pitch_rad = arm_kinematics_deg_to_rad(p_pose->pitch_deg);
    wrist_r_mm = r_tcp_mm - g_arm_kinematics_geometry.d0_mm -
                 (g_arm_kinematics_geometry.l3_total_mm * cosf(tool_pitch_rad));
    wrist_z_mm = p_pose->z_mm - g_arm_kinematics_geometry.l0_mm -
                 (g_arm_kinematics_geometry.l3_total_mm * sinf(tool_pitch_rad));

    distance_sq = (wrist_r_mm * wrist_r_mm) + (wrist_z_mm * wrist_z_mm);
    cos_elbow = (distance_sq -
                 (g_arm_kinematics_geometry.l1_mm * g_arm_kinematics_geometry.l1_mm) -
                 (g_arm_kinematics_geometry.l2_mm * g_arm_kinematics_geometry.l2_mm)) /
                (2.0f * g_arm_kinematics_geometry.l1_mm * g_arm_kinematics_geometry.l2_mm);

    if ((cos_elbow < -1.0f - ARM_KINEMATICS_EPSILON) ||
        (cos_elbow > 1.0f + ARM_KINEMATICS_EPSILON))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (cos_elbow > 1.0f)
    {
        cos_elbow = 1.0f;
    }
    else if (cos_elbow < -1.0f)
    {
        cos_elbow = -1.0f;
    }

    elbow_abs_rad = acosf(cos_elbow);
    elbow_rad[0] = elbow_abs_rad;
    elbow_rad[1] = 0.0f - elbow_abs_rad;

    for (uint8_t i = 0; i < ARM_KINEMATICS_IK_CANDIDATES; i++)
    {
        shoulder_rad = atan2f(wrist_z_mm, wrist_r_mm) -
                       atan2f(g_arm_kinematics_geometry.l2_mm * sinf(elbow_rad[i]),
                              g_arm_kinematics_geometry.l1_mm +
                              (g_arm_kinematics_geometry.l2_mm * cosf(elbow_rad[i])));

        candidate[i].base_deg = arm_kinematics_rad_to_deg(atan2f(p_pose->y_mm, p_pose->x_mm));
        candidate[i].shoulder_deg = arm_kinematics_rad_to_deg(shoulder_rad);
        candidate[i].elbow_deg = arm_kinematics_rad_to_deg(elbow_rad[i]);
        candidate[i].pitch_deg = p_pose->pitch_deg -
                                 candidate[i].shoulder_deg -
                                 candidate[i].elbow_deg;
        candidate[i].roll_deg = p_pose->roll_deg;
        candidate[i].grip_open_percent = p_pose->grip_open_percent;

        if (FSP_SUCCESS == arm_kinematics_candidate_to_pwm(&candidate[i], pulse_us))
        {
            score = arm_kinematics_candidate_score(&candidate[i], &seed);
            if ((!found) || (score < best_score))
            {
                found = true;
                best_score = score;
                best_index = i;
            }
        }
    }

    if (!found)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    *p_joints = candidate[best_index];
    return FSP_SUCCESS;
}

fsp_err_t arm_kinematics_pose_from_pwm(const uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_pose_t *p_pose,
                                       arm_kinematics_joint_t *p_joints)
{
    arm_kinematics_joint_t joints;
    fsp_err_t err;

    if ((NULL == pulse_us) || (NULL == p_pose))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = arm_kinematics_pwm_to_joints(pulse_us, &joints);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = arm_kinematics_forward(&joints, p_pose);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (NULL != p_joints)
    {
        *p_joints = joints;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_kinematics_pwm_from_pose(const arm_kinematics_pose_t *p_pose,
                                       const uint16_t seed_us[ARM_SERVO_COUNT],
                                       uint16_t pulse_us[ARM_SERVO_COUNT],
                                       arm_kinematics_joint_t *p_joints)
{
    arm_kinematics_joint_t seed;
    arm_kinematics_joint_t joints;
    fsp_err_t err;

    if ((NULL == p_pose) || (NULL == seed_us) || (NULL == pulse_us))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = arm_kinematics_pwm_to_joints(seed_us, &seed);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = arm_kinematics_inverse(p_pose, &seed, &joints);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = arm_kinematics_joints_to_pwm(&joints, pulse_us);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (NULL != p_joints)
    {
        *p_joints = joints;
    }

    return FSP_SUCCESS;
}

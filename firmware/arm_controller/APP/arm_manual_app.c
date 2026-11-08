#include "arm_manual_app.h"
#include "mydefine.h"
#include <math.h>

#define ARM_MANUAL_ERROR_PRINT_MS (500U)
#define ARM_MANUAL_PITCH_RELAX_DEG (20)
#define ARM_MANUAL_GRIP_OPEN_US    (500U)
#define ARM_MANUAL_GRIP_CLOSE_US   (1500U)
#define ARM_MANUAL_DEG_TO_RAD      (0.017453292519943295f)

typedef struct st_arm_manual_axis_config
{
    int32_t min_value;
    int32_t max_value;
    int32_t home_value;
    int32_t max_speed_per_s;
} arm_manual_axis_config_t;

static const arm_manual_axis_config_t g_arm_manual_axis_config[ARM_MANUAL_AXIS_COUNT] =
{
    {-450, 450, 0, 80},
    {-450, 450, 0, 80},
    {-150, 450, 0, 80},
    {-120, 120, 0, 60},
    {-90, 90, 0, 90},
    {0, 100, 100, 100},
};

static arm_kinematics_pose_t g_arm_manual_target_pose;
static uint16_t g_arm_manual_seed_us[ARM_SERVO_COUNT];
static uint32_t g_arm_manual_last_update_tick = 0;
static uint32_t g_arm_manual_last_error_tick = 0;
static uint32_t g_arm_manual_motion_elapsed_ms = 0;
static uint8_t g_arm_manual_active_axis = ARM_MANUAL_AXIS_ALL;
static uint8_t g_arm_manual_direction = ARM_MANUAL_DIR_STOP;
static uint8_t g_arm_manual_mode = ARM_MANUAL_MODE_CONTINUOUS;
static uint8_t g_arm_manual_coord_mode = ARM_MANUAL_COORD_BASE;
static uint8_t g_arm_manual_speed = 90U;
static uint8_t g_arm_manual_soft = 10U;
static uint8_t g_arm_manual_step = 1U;
static bool g_arm_manual_page_active = false;
static bool g_arm_manual_moving = false;
static int32_t g_arm_manual_motion_speed_q1000_per_s = 0;

static void arm_manual_stop(uint8_t axis);
static int32_t arm_manual_clamp_q1000(uint8_t axis, int32_t value_q1000);
static fsp_err_t arm_manual_apply_pose(bool allow_pitch_relax);
static fsp_err_t arm_manual_apply_grip_percent(void);

static uint32_t arm_manual_smoothstep_q1000(uint32_t elapsed_ms, uint32_t ramp_ms)
{
    uint32_t x;
    uint32_t x2;
    uint32_t x3;
    uint32_t scale_q1000;

    if ((0U == ramp_ms) || (elapsed_ms >= ramp_ms))
    {
        return 1000U;
    }

    x = (elapsed_ms * 1000U) / ramp_ms;
    x2 = x * x;
    x3 = x2 * x;

    scale_q1000 = ((3U * x2) - ((2U * x3) / 1000U)) / 1000U;
    if ((0U == scale_q1000) && (elapsed_ms > 0U))
    {
        scale_q1000 = 1U;
    }

    return scale_q1000;
}

static void arm_manual_print_motion_event(const char *p_tag, uint8_t axis, uint8_t direction)
{
    my_printf(&g_debug_uart,
              "MANUAL %s axis=%u dir=%u mode=%u coord=%u speed=%u soft=%u step=%u x=%ld y=%ld z=%ld pitch=%ld roll=%ld grip=%ld\r\n",
              p_tag,
              (unsigned int)axis,
              (unsigned int)direction,
              (unsigned int)g_arm_manual_mode,
              (unsigned int)g_arm_manual_coord_mode,
              (unsigned int)g_arm_manual_speed,
              (unsigned int)g_arm_manual_soft,
              (unsigned int)g_arm_manual_step,
              (long)g_arm_manual_target_pose.x_mm,
              (long)g_arm_manual_target_pose.y_mm,
              (long)g_arm_manual_target_pose.z_mm,
              (long)g_arm_manual_target_pose.pitch_deg,
              (long)g_arm_manual_target_pose.roll_deg,
              (long)g_arm_manual_target_pose.grip_open_percent);
}

static int32_t arm_manual_clamp_value(uint8_t axis, int32_t value)
{
    if (value < g_arm_manual_axis_config[axis].min_value)
    {
        return g_arm_manual_axis_config[axis].min_value;
    }

    if (value > g_arm_manual_axis_config[axis].max_value)
    {
        return g_arm_manual_axis_config[axis].max_value;
    }

    return value;
}

static int32_t arm_manual_pose_axis_to_q1000(uint8_t axis, const arm_kinematics_pose_t *p_pose)
{
    switch (axis)
    {
        case 0:
            return (int32_t)(p_pose->x_mm * 1000.0f);

        case 1:
            return (int32_t)(p_pose->y_mm * 1000.0f);

        case 2:
            return (int32_t)(p_pose->z_mm * 1000.0f);

        case 3:
            return (int32_t)(p_pose->pitch_deg * 1000.0f);

        case 4:
            return (int32_t)(p_pose->roll_deg * 1000.0f);

        case 5:
            return (int32_t)(p_pose->grip_open_percent * 1000.0f);

        default:
            return 0;
    }
}

static void arm_manual_pose_axis_from_q1000(uint8_t axis,
                                            int32_t value_q1000,
                                            arm_kinematics_pose_t *p_pose)
{
    float value = (float)value_q1000 / 1000.0f;

    switch (axis)
    {
        case 0:
            p_pose->x_mm = value;
            break;

        case 1:
            p_pose->y_mm = value;
            break;

        case 2:
            p_pose->z_mm = value;
            break;

        case 3:
            p_pose->pitch_deg = value;
            break;

        case 4:
            p_pose->roll_deg = value;
            break;

        case 5:
            p_pose->grip_open_percent = value;
            break;

        default:
            break;
    }
}

static float arm_manual_pose_radius_xy(const arm_kinematics_pose_t *p_pose)
{
    return sqrtf((p_pose->x_mm * p_pose->x_mm) + (p_pose->y_mm * p_pose->y_mm));
}

static void arm_manual_apply_tool_translation(uint8_t axis,
                                              int32_t delta_q1000,
                                              arm_kinematics_pose_t *p_pose)
{
    float delta_mm = (float)delta_q1000 / 1000.0f;
    float yaw_rad;
    float pitch_rad;
    float roll_rad;
    float x_axis_r;
    float x_axis_z;
    float y0_x;
    float y0_y;
    float z0_r;
    float z0_z;
    float y_axis_x;
    float y_axis_y;
    float y_axis_z;
    float z_axis_x;
    float z_axis_y;
    float z_axis_z;
    float cos_roll;
    float sin_roll;
    float radius;

    if (NULL == p_pose)
    {
        return;
    }

    yaw_rad = atan2f(p_pose->y_mm, p_pose->x_mm);
    pitch_rad = p_pose->pitch_deg * ARM_MANUAL_DEG_TO_RAD;
    roll_rad = p_pose->roll_deg * ARM_MANUAL_DEG_TO_RAD;
    cos_roll = cosf(roll_rad);
    sin_roll = sinf(roll_rad);

    x_axis_r = cosf(pitch_rad);
    x_axis_z = sinf(pitch_rad);
    y0_x = 0.0f - sinf(yaw_rad);
    y0_y = cosf(yaw_rad);
    z0_r = 0.0f - sinf(pitch_rad);
    z0_z = cosf(pitch_rad);
    y_axis_x = (cos_roll * y0_x) + (sin_roll * z0_r * cosf(yaw_rad));
    y_axis_y = (cos_roll * y0_y) + (sin_roll * z0_r * sinf(yaw_rad));
    y_axis_z = sin_roll * z0_z;
    z_axis_x = (0.0f - sin_roll * y0_x) + (cos_roll * z0_r * cosf(yaw_rad));
    z_axis_y = (0.0f - sin_roll * y0_y) + (cos_roll * z0_r * sinf(yaw_rad));
    z_axis_z = cos_roll * z0_z;

    switch (axis)
    {
        case ARM_MANUAL_AXIS_X:
            p_pose->x_mm += delta_mm * x_axis_r * cosf(yaw_rad);
            p_pose->y_mm += delta_mm * x_axis_r * sinf(yaw_rad);
            p_pose->z_mm += delta_mm * x_axis_z;
            break;

        case ARM_MANUAL_AXIS_Y:
            p_pose->x_mm += delta_mm * y_axis_x;
            p_pose->y_mm += delta_mm * y_axis_y;
            p_pose->z_mm += delta_mm * y_axis_z;
            break;

        case ARM_MANUAL_AXIS_Z:
            p_pose->x_mm += delta_mm * z_axis_x;
            p_pose->y_mm += delta_mm * z_axis_y;
            p_pose->z_mm += delta_mm * z_axis_z;
            break;

        default:
            break;
    }

    radius = arm_manual_pose_radius_xy(p_pose);
    if (radius > 450.0f)
    {
        p_pose->x_mm = (p_pose->x_mm * 450.0f) / radius;
        p_pose->y_mm = (p_pose->y_mm * 450.0f) / radius;
    }
    else if ((radius < 0.001f) && ((ARM_MANUAL_AXIS_X == axis) || (ARM_MANUAL_AXIS_Y == axis)))
    {
        p_pose->x_mm = 0.001f;
        p_pose->y_mm = 0.0f;
    }

    if (p_pose->z_mm < (float)g_arm_manual_axis_config[ARM_MANUAL_AXIS_Z].min_value)
    {
        p_pose->z_mm = (float)g_arm_manual_axis_config[ARM_MANUAL_AXIS_Z].min_value;
    }
    else if (p_pose->z_mm > (float)g_arm_manual_axis_config[ARM_MANUAL_AXIS_Z].max_value)
    {
        p_pose->z_mm = (float)g_arm_manual_axis_config[ARM_MANUAL_AXIS_Z].max_value;
    }
}

static fsp_err_t arm_manual_apply_axis_delta(uint8_t axis, int32_t delta_q1000, bool allow_pitch_relax)
{
    int32_t old_q1000;
    int32_t new_q1000;
    arm_kinematics_pose_t old_pose;
    fsp_err_t err;

    old_pose = g_arm_manual_target_pose;

    if ((ARM_MANUAL_COORD_TOOL == g_arm_manual_coord_mode) && (axis <= ARM_MANUAL_AXIS_Z))
    {
        arm_manual_apply_tool_translation(axis, delta_q1000, &g_arm_manual_target_pose);
    }
    else
    {
        old_q1000 = arm_manual_pose_axis_to_q1000(axis, &g_arm_manual_target_pose);
        new_q1000 = arm_manual_clamp_q1000(axis, old_q1000 + delta_q1000);
        arm_manual_pose_axis_from_q1000(axis, new_q1000, &g_arm_manual_target_pose);
    }

    if (ARM_MANUAL_AXIS_GRIP == axis)
    {
        err = arm_manual_apply_grip_percent();
    }
    else
    {
        err = arm_manual_apply_pose(allow_pitch_relax);
    }
    if (FSP_SUCCESS != err)
    {
        g_arm_manual_target_pose = old_pose;
        return err;
    }

    return FSP_SUCCESS;
}

static int32_t arm_manual_clamp_q1000(uint8_t axis, int32_t value_q1000)
{
    int32_t min_q1000 = g_arm_manual_axis_config[axis].min_value * 1000;
    int32_t max_q1000 = g_arm_manual_axis_config[axis].max_value * 1000;

    if (value_q1000 < min_q1000)
    {
        return min_q1000;
    }

    if (value_q1000 > max_q1000)
    {
        return max_q1000;
    }

    return value_q1000;
}

static bool arm_manual_axis_float_in_range(uint8_t axis, float value)
{
    return ((value >= (float)g_arm_manual_axis_config[axis].min_value) &&
            (value <= (float)g_arm_manual_axis_config[axis].max_value));
}

static void arm_manual_capture_seed_from_control(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_manual_seed_us[i] = arm_control_get_target_us(i);
    }
}

static fsp_err_t arm_manual_sync_pose_from_control(void)
{
    arm_manual_capture_seed_from_control();

    return arm_kinematics_pose_from_pwm(g_arm_manual_seed_us,
                                        &g_arm_manual_target_pose,
                                        NULL);
}

static fsp_err_t arm_manual_calc_pose_pwm(const arm_kinematics_pose_t *p_pose,
                                          uint16_t target_us[ARM_SERVO_COUNT])
{
    arm_kinematics_joint_t joints;

    return arm_kinematics_pwm_from_pose(p_pose,
                                        g_arm_manual_seed_us,
                                        target_us,
                                        &joints);
}

static fsp_err_t arm_manual_apply_pwm(const uint16_t target_us[ARM_SERVO_COUNT])
{
    fsp_err_t err;

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        err = arm_control_set_target_us(i, target_us[i], ARM_CONTROL_SOURCE_HMI);
        if (FSP_SUCCESS != err)
        {
            arm_manual_stop(ARM_MANUAL_AXIS_ALL);
            return err;
        }
        g_arm_manual_seed_us[i] = target_us[i];
    }

    return FSP_SUCCESS;
}

static fsp_err_t arm_manual_apply_pose(bool allow_pitch_relax)
{
    uint16_t target_us[ARM_SERVO_COUNT];
    arm_kinematics_pose_t apply_pose;
    float base_pitch_deg;
    fsp_err_t err;

    apply_pose = g_arm_manual_target_pose;
    err = arm_manual_calc_pose_pwm(&apply_pose, target_us);

    if ((FSP_SUCCESS != err) && allow_pitch_relax)
    {
        base_pitch_deg = g_arm_manual_target_pose.pitch_deg;

        for (int32_t delta = 1; delta <= ARM_MANUAL_PITCH_RELAX_DEG; delta++)
        {
            apply_pose = g_arm_manual_target_pose;
            apply_pose.pitch_deg = base_pitch_deg + (float)delta;
            if (arm_manual_axis_float_in_range(3U, apply_pose.pitch_deg))
            {
                err = arm_manual_calc_pose_pwm(&apply_pose, target_us);
                if (FSP_SUCCESS == err)
                {
                    break;
                }
            }

            apply_pose = g_arm_manual_target_pose;
            apply_pose.pitch_deg = base_pitch_deg - (float)delta;
            if (arm_manual_axis_float_in_range(3U, apply_pose.pitch_deg))
            {
                err = arm_manual_calc_pose_pwm(&apply_pose, target_us);
                if (FSP_SUCCESS == err)
                {
                    break;
                }
            }
        }
    }

    if (FSP_SUCCESS != err)
    {
        arm_manual_stop(ARM_MANUAL_AXIS_ALL);
        if ((uint32_t)(g_tick - g_arm_manual_last_error_tick) >= ARM_MANUAL_ERROR_PRINT_MS)
        {
            g_arm_manual_last_error_tick = g_tick;
            my_printf(&g_debug_uart,
                      "MANUAL IK ERR x=%ld y=%ld z=%ld pitch=%ld roll=%ld grip=%ld err=%d\r\n",
                      (long)g_arm_manual_target_pose.x_mm,
                      (long)g_arm_manual_target_pose.y_mm,
                      (long)g_arm_manual_target_pose.z_mm,
                      (long)g_arm_manual_target_pose.pitch_deg,
                      (long)g_arm_manual_target_pose.roll_deg,
                      (long)g_arm_manual_target_pose.grip_open_percent,
                      err);
        }
        return err;
    }

    err = arm_manual_apply_pwm(target_us);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_arm_manual_target_pose = apply_pose;
    return FSP_SUCCESS;
}

static fsp_err_t arm_manual_apply_grip_percent(void)
{
    float grip_percent = g_arm_manual_target_pose.grip_open_percent;
    float pulse_f;
    uint16_t pulse_us;
    fsp_err_t err;

    if (grip_percent < 0.0f)
    {
        grip_percent = 0.0f;
    }
    else if (grip_percent > 100.0f)
    {
        grip_percent = 100.0f;
    }

    pulse_f = (float)ARM_MANUAL_GRIP_CLOSE_US -
              ((grip_percent * (float)(ARM_MANUAL_GRIP_CLOSE_US - ARM_MANUAL_GRIP_OPEN_US)) / 100.0f);
    pulse_us = (uint16_t)(pulse_f + 0.5f);

    err = arm_control_set_target_us(5U, pulse_us, ARM_CONTROL_SOURCE_HMI);
    if (FSP_SUCCESS == err)
    {
        g_arm_manual_seed_us[5] = pulse_us;
    }

    return err;
}

static uint8_t arm_manual_clamp_percent(uint8_t value)
{
    if (value > 100U)
    {
        return 100U;
    }

    return value;
}

static uint8_t arm_manual_clamp_step(uint8_t value)
{
    if (value < 1U)
    {
        return 1U;
    }

    if (value > 50U)
    {
        return 50U;
    }

    return value;
}

static void arm_manual_stop(uint8_t axis)
{
    if ((ARM_MANUAL_AXIS_ALL == axis) || (axis == g_arm_manual_active_axis))
    {
        bool was_moving = g_arm_manual_moving;
        uint8_t stop_axis = g_arm_manual_active_axis;
        uint8_t stop_direction = g_arm_manual_direction;

        g_arm_manual_moving = false;
        g_arm_manual_direction = ARM_MANUAL_DIR_STOP;
        g_arm_manual_active_axis = ARM_MANUAL_AXIS_ALL;
        g_arm_manual_motion_speed_q1000_per_s = 0;
        g_arm_manual_motion_elapsed_ms = 0;

        if (was_moving)
        {
            arm_manual_print_motion_event("STOP", stop_axis, stop_direction);
        }
    }
}

static fsp_err_t arm_manual_step_move(uint8_t axis, uint8_t direction, uint8_t step)
{
    int32_t delta = (int32_t)step;

    if (ARM_MANUAL_DIR_NEGATIVE == direction)
    {
        delta = 0 - delta;
    }

    return arm_manual_apply_axis_delta(axis, delta * 1000, axis <= ARM_MANUAL_AXIS_Z);
}

fsp_err_t arm_manual_init(void)
{
    g_arm_manual_last_update_tick = g_tick;
    g_arm_manual_last_error_tick = g_tick;
    g_arm_manual_motion_elapsed_ms = 0;
    g_arm_manual_active_axis = ARM_MANUAL_AXIS_ALL;
    g_arm_manual_direction = ARM_MANUAL_DIR_STOP;
    g_arm_manual_mode = ARM_MANUAL_MODE_CONTINUOUS;
    g_arm_manual_coord_mode = ARM_MANUAL_COORD_BASE;
    g_arm_manual_speed = 90U;
    g_arm_manual_soft = 10U;
    g_arm_manual_step = 1U;
    g_arm_manual_page_active = false;
    g_arm_manual_moving = false;
    g_arm_manual_motion_speed_q1000_per_s = 0;
    memset(g_arm_manual_seed_us, 0, sizeof(g_arm_manual_seed_us));
    memset(&g_arm_manual_target_pose, 0, sizeof(g_arm_manual_target_pose));

    return FSP_SUCCESS;
}

fsp_err_t arm_manual_set_page_active(bool active)
{
    fsp_err_t err;

    if (active)
    {
        if (!arm_control_is_ready())
        {
            g_arm_manual_page_active = false;
            arm_manual_stop(ARM_MANUAL_AXIS_ALL);
            return FSP_ERR_NOT_OPEN;
        }

        err = arm_manual_sync_pose_from_control();
        if (FSP_SUCCESS != err)
        {
            g_arm_manual_page_active = false;
            arm_manual_stop(ARM_MANUAL_AXIS_ALL);
            return err;
        }
        g_arm_manual_page_active = true;
    }
    else
    {
        g_arm_manual_page_active = false;
        arm_manual_stop(ARM_MANUAL_AXIS_ALL);
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_manual_set_params(uint8_t mode, uint8_t speed, uint8_t soft, uint8_t step)
{
    if ((ARM_MANUAL_MODE_CONTINUOUS != mode) && (ARM_MANUAL_MODE_STEP != mode))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    g_arm_manual_mode = mode;
    g_arm_manual_speed = arm_manual_clamp_percent(speed);
    g_arm_manual_soft = arm_manual_clamp_percent(soft);
    g_arm_manual_step = arm_manual_clamp_step(step);

    return FSP_SUCCESS;
}

fsp_err_t arm_manual_set_coord_mode(uint8_t coord_mode)
{
    if ((ARM_MANUAL_COORD_BASE != coord_mode) &&
        (ARM_MANUAL_COORD_TOOL != coord_mode) &&
        (ARM_MANUAL_COORD_WORKPIECE != coord_mode) &&
        (ARM_MANUAL_COORD_CAMERA != coord_mode))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    arm_manual_stop(ARM_MANUAL_AXIS_ALL);

    if ((ARM_MANUAL_COORD_WORKPIECE == coord_mode) ||
        (ARM_MANUAL_COORD_CAMERA == coord_mode))
    {
        g_arm_manual_coord_mode = coord_mode;
        return FSP_SUCCESS;
    }

    g_arm_manual_coord_mode = coord_mode;
    return FSP_SUCCESS;
}

fsp_err_t arm_manual_sync_from_control(void)
{
    if (!arm_control_is_ready())
    {
        return FSP_ERR_NOT_OPEN;
    }

    return arm_manual_sync_pose_from_control();
}

fsp_err_t arm_manual_jog_command(uint8_t axis,
                                 uint8_t direction,
                                 uint8_t mode,
                                 uint8_t speed,
                                 uint8_t soft,
                                 uint8_t step)
{
    fsp_err_t err;

    if ((ARM_MANUAL_DIR_STOP != direction) &&
        (ARM_MANUAL_DIR_NEGATIVE != direction) &&
        (ARM_MANUAL_DIR_POSITIVE != direction))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if ((ARM_MANUAL_AXIS_ALL != axis) && (axis >= ARM_MANUAL_AXIS_COUNT))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = arm_manual_set_params(mode, speed, soft, step);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (ARM_MANUAL_DIR_STOP == direction)
    {
        arm_manual_stop(axis);
        return FSP_SUCCESS;
    }

    if (ARM_MANUAL_AXIS_ALL == axis)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!g_arm_manual_page_active)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if ((ARM_MANUAL_COORD_WORKPIECE == g_arm_manual_coord_mode) ||
        (ARM_MANUAL_COORD_CAMERA == g_arm_manual_coord_mode))
    {
        arm_manual_stop(ARM_MANUAL_AXIS_ALL);
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (ARM_MANUAL_MODE_STEP == mode)
    {
        arm_manual_stop(ARM_MANUAL_AXIS_ALL);
        return arm_manual_step_move(axis, direction, g_arm_manual_step);
    }

    g_arm_manual_active_axis = axis;
    g_arm_manual_direction = direction;
    g_arm_manual_moving = true;
    g_arm_manual_motion_speed_q1000_per_s = 0;
    g_arm_manual_motion_elapsed_ms = 0;
    g_arm_manual_last_update_tick = g_tick;
    arm_manual_print_motion_event("START", axis, direction);

    return FSP_SUCCESS;
}

bool arm_manual_is_page_active(void)
{
    return g_arm_manual_page_active;
}

void arm_manual_task(void)
{
    uint32_t elapsed_ms;
    uint8_t axis;
    int32_t delta_q1000;
    int32_t target_speed_q1000_per_s;
    uint32_t speed_scale_q1000;
    uint32_t ramp_ms;
    int32_t axis_value_q1000;
    fsp_err_t err;

    if (!g_arm_manual_moving)
    {
        g_arm_manual_last_update_tick = g_tick;
        return;
    }

    if ((ARM_MANUAL_AXIS_ALL == g_arm_manual_active_axis) ||
        (g_arm_manual_active_axis >= ARM_MANUAL_AXIS_COUNT))
    {
        arm_manual_stop(ARM_MANUAL_AXIS_ALL);
        return;
    }

    elapsed_ms = (uint32_t)(g_tick - g_arm_manual_last_update_tick);
    if (0U == elapsed_ms)
    {
        return;
    }
    g_arm_manual_last_update_tick = g_tick;

    axis = g_arm_manual_active_axis;
    target_speed_q1000_per_s =
        (g_arm_manual_axis_config[axis].max_speed_per_s * (int32_t)g_arm_manual_speed * 1000) / 100;

    g_arm_manual_motion_elapsed_ms += elapsed_ms;

    if (0U == g_arm_manual_soft)
    {
        g_arm_manual_motion_speed_q1000_per_s = target_speed_q1000_per_s;
    }
    else
    {
        ramp_ms = 100U + ((uint32_t)g_arm_manual_soft * 20U);
        speed_scale_q1000 = arm_manual_smoothstep_q1000(g_arm_manual_motion_elapsed_ms, ramp_ms);
        g_arm_manual_motion_speed_q1000_per_s =
            (target_speed_q1000_per_s * (int32_t)speed_scale_q1000) / 1000;
    }

    delta_q1000 = (g_arm_manual_motion_speed_q1000_per_s * (int32_t)elapsed_ms) / 1000;
    if (ARM_MANUAL_DIR_NEGATIVE == g_arm_manual_direction)
    {
        delta_q1000 = 0 - delta_q1000;
    }
    if (0 == delta_q1000)
    {
        return;
    }

    err = arm_manual_apply_axis_delta(axis, delta_q1000, axis <= ARM_MANUAL_AXIS_Z);
    if (FSP_SUCCESS != err)
    {
        arm_manual_stop(axis);
        return;
    }

    if ((ARM_MANUAL_COORD_BASE == g_arm_manual_coord_mode) ||
        (axis > ARM_MANUAL_AXIS_Z))
    {
        axis_value_q1000 = arm_manual_pose_axis_to_q1000(axis, &g_arm_manual_target_pose);
        if (((ARM_MANUAL_DIR_NEGATIVE == g_arm_manual_direction) &&
             (axis_value_q1000 <= (g_arm_manual_axis_config[axis].min_value * 1000))) ||
            ((ARM_MANUAL_DIR_POSITIVE == g_arm_manual_direction) &&
             (axis_value_q1000 >= (g_arm_manual_axis_config[axis].max_value * 1000))))
        {
            arm_manual_stop(axis);
        }
    }
}

void arm_manual_get_info(arm_manual_info_t *p_info)
{
    if (NULL == p_info)
    {
        return;
    }

    for (uint8_t i = 0; i < ARM_MANUAL_AXIS_COUNT; i++)
    {
        p_info->value[i] = arm_manual_clamp_value(i,
                                                  arm_manual_pose_axis_to_q1000(i, &g_arm_manual_target_pose) / 1000);
    }

    p_info->active_axis = g_arm_manual_active_axis;
    p_info->direction = g_arm_manual_direction;
    p_info->mode = g_arm_manual_mode;
    p_info->coord_mode = g_arm_manual_coord_mode;
    p_info->speed = g_arm_manual_speed;
    p_info->soft = g_arm_manual_soft;
    p_info->step = g_arm_manual_step;
    p_info->page_active = g_arm_manual_page_active;
    p_info->moving = g_arm_manual_moving;
}

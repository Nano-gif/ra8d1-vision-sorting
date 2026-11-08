#include "arm_action_app.h"
#include "mydefine.h"

#define ARM_ACTION_DONE_HOLD_MS         (500U)
#define ARM_ACTION_SMOOTH_Q15_ONE       (32768UL)

typedef struct st_arm_action_step
{
    uint16_t servo_us[ARM_SERVO_COUNT];
    uint16_t duration_ms;
} arm_action_step_t;

typedef char arm_action_step_size_check[(sizeof(arm_action_step_t) == sizeof(arm_config_action_step_t)) ? 1 : -1];

static arm_action_step_t g_arm_action_steps[ARM_ACTION_MAX_STEPS];
static uint16_t g_arm_action_segment_start_us[ARM_SERVO_COUNT];
static uint16_t g_arm_action_segment_target_us[ARM_SERVO_COUNT];
static uint16_t g_arm_action_segment_duration_ms = ARM_ACTION_HOME_DURATION_MS;
static uint32_t g_arm_action_segment_start_tick = 0;
static uint32_t g_arm_action_done_tick = 0;
static uint8_t g_arm_action_step_count = 0;
static uint8_t g_arm_action_current_step = 0;
static uint8_t g_arm_action_last_step = 0;
static uint8_t g_arm_action_segment_index = 0;
static arm_action_state_t g_arm_action_state = ARM_ACTION_STATE_READY;
static arm_control_source_t g_arm_action_segment_source = ARM_CONTROL_SOURCE_PLAYBACK;
static bool g_arm_action_playing = false;
static bool g_arm_action_home_segment = false;
static bool g_arm_action_custom_segment = false;

/* ---- NG 分拣腕部(J5)转角补偿叠加 ----
 * 标准录制动作不含腕旋转。这里在不改关键帧协议的前提下，于播放状态机里
 * 注入两段“只动 J5”的平滑段：第2步后转到对齐角，第3、4步(抓取+抬起)期间
 * 把 J5 摁在对齐角(覆盖录制里的中位)，第4步后再转回中位。死区/无效则完全
 * 退化成原版播放，零额外停顿。 */
#define ARM_NG_J5_CHANNEL        (4U)        /* J5 = 腕部 Roll */
#define ARM_NG_DEADZONE_X10      (30)        /* 3.0 度：以内完全不动腕、不卡顿 */
#define ARM_NG_J5_MOVE_MS        (1000U)     /* 对齐/复位平滑时长 */
#define ARM_NG_ALIGN_AFTER_IDX   (1U)        /* 第2步后注入对齐(0-based index 1) */
#define ARM_NG_RESET_AFTER_IDX   (3U)        /* 第4步后注入复位(0-based index 3) */
#define ARM_NG_YAW_SIGN          (-1)        /* 现场实测腕转向与物品朝向相反，取 -1 校正 */

typedef enum e_arm_ng_phase
{
    ARM_NG_NONE = 0,
    ARM_NG_PRE,        /* 对齐前的步(1..2) */
    ARM_NG_ALIGNING,   /* 注入段：中位 -> 对齐 */
    ARM_NG_HELD,       /* 第3..4步：J5 强制保持对齐(抓取+抬起) */
    ARM_NG_RESETTING,  /* 注入段：对齐 -> 中位 */
    ARM_NG_POST        /* 第5..8步：正常播放 */
} arm_ng_phase_t;

static bool           g_arm_ng_active = false;
static arm_ng_phase_t g_arm_ng_phase = ARM_NG_NONE;
static uint16_t       g_arm_ng_aligned_us = 0;
static uint16_t       g_arm_ng_neutral_us = 0;
static uint8_t        g_arm_ng_resume_index = 0;

static void arm_ng_reset(void)
{
    g_arm_ng_active = false;
    g_arm_ng_phase = ARM_NG_NONE;
    g_arm_ng_resume_index = 0;
}

static uint16_t arm_action_clamp_duration(uint16_t duration_ms)
{
    if (duration_ms < ARM_ACTION_MIN_DURATION_MS)
    {
        return ARM_ACTION_MIN_DURATION_MS;
    }

    if (duration_ms > ARM_ACTION_MAX_DURATION_MS)
    {
        return ARM_ACTION_MAX_DURATION_MS;
    }

    return duration_ms;
}

static void arm_action_enter_error(void)
{
    arm_ng_reset();
    g_arm_action_playing = false;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = false;
    g_arm_action_state = ARM_ACTION_STATE_ERROR;
}

static fsp_err_t arm_action_apply_targets(const uint16_t *p_targets)
{
    fsp_err_t err;

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        uint16_t value = p_targets[i];

        /* NG: 抓取+抬起期间，强制 J5 保持对齐角，无视录制关键帧里的 J5 列。 */
        if (g_arm_ng_active && (ARM_NG_HELD == g_arm_ng_phase) && (ARM_NG_J5_CHANNEL == i))
        {
            value = g_arm_ng_aligned_us;
        }

        err = arm_control_set_target_us(i, value, g_arm_action_segment_source);
        if (FSP_SUCCESS != err)
        {
            arm_action_enter_error();
            return err;
        }
    }

    return FSP_SUCCESS;
}

/* 三次 smoothstep 插值：各关节平滑起停，不改变关键帧协议。 */
static uint32_t arm_action_smoothstep_q15(uint32_t elapsed_ms, uint32_t duration_ms)
{
    uint32_t x;
    uint32_t x2;
    uint32_t x3;

    if (0U == duration_ms)
    {
        return ARM_ACTION_SMOOTH_Q15_ONE;
    }

    if (elapsed_ms >= duration_ms)
    {
        return ARM_ACTION_SMOOTH_Q15_ONE;
    }

    x = (elapsed_ms * ARM_ACTION_SMOOTH_Q15_ONE) / duration_ms;
    x2 = (x * x) / ARM_ACTION_SMOOTH_Q15_ONE;
    x3 = (x2 * x) / ARM_ACTION_SMOOTH_Q15_ONE;

    return (3UL * x2) - (2UL * x3);
}

static uint16_t arm_action_interpolate_us(uint16_t start_us, uint16_t target_us, uint32_t scale_q15)
{
    int32_t delta = (int32_t)target_us - (int32_t)start_us;
    int32_t value = (int32_t)start_us + ((delta * (int32_t)scale_q15) / (int32_t)ARM_ACTION_SMOOTH_Q15_ONE);

    if (value < 0)
    {
        return 0;
    }

    return (uint16_t)value;
}

static void arm_action_capture_current_targets(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_action_segment_start_us[i] = arm_control_get_target_us(i);
    }
}

static void arm_action_capture_previous_segment_target(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_action_segment_start_us[i] = g_arm_action_segment_target_us[i];
    }
}

static fsp_err_t arm_action_begin_recorded_segment(uint8_t step_index, bool start_from_current)
{
    if (step_index >= g_arm_action_step_count)
    {
        arm_action_enter_error();
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (start_from_current)
    {
        arm_action_capture_current_targets();
    }
    else
    {
        arm_action_capture_previous_segment_target();
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_action_segment_target_us[i] = g_arm_action_steps[step_index].servo_us[i];
    }

    g_arm_action_segment_duration_ms = arm_action_clamp_duration(g_arm_action_steps[step_index].duration_ms);
    g_arm_action_segment_start_tick = g_tick;
    g_arm_action_segment_index = step_index;
    g_arm_action_current_step = (uint8_t)(step_index + 1U);
    g_arm_action_segment_source = ARM_CONTROL_SOURCE_PLAYBACK;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = false;

    return FSP_SUCCESS;
}

static fsp_err_t arm_action_begin_home_segment(uint16_t duration_ms,
                                               bool start_from_current,
                                               arm_control_source_t source)
{
    if (start_from_current)
    {
        arm_action_capture_current_targets();
    }
    else
    {
        arm_action_capture_previous_segment_target();
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_action_segment_target_us[i] = arm_control_get_home_us(i);
    }

    g_arm_action_segment_duration_ms = arm_action_clamp_duration(duration_ms);
    g_arm_action_segment_start_tick = g_tick;
    g_arm_action_done_tick = 0;
    g_arm_action_segment_source = source;
    g_arm_action_home_segment = true;
    g_arm_action_custom_segment = false;

    return FSP_SUCCESS;
}

static fsp_err_t arm_action_begin_custom_segment(const uint16_t target_us[ARM_SERVO_COUNT],
                                                 uint16_t duration_ms,
                                                 arm_control_source_t source)
{
    if (NULL == target_us)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    arm_action_capture_current_targets();

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if ((target_us[i] < arm_control_get_min_us(i)) ||
            (target_us[i] > arm_control_get_max_us(i)))
        {
            return FSP_ERR_INVALID_ARGUMENT;
        }

        g_arm_action_segment_target_us[i] = target_us[i];
    }

    g_arm_action_segment_duration_ms = arm_action_clamp_duration(duration_ms);
    g_arm_action_segment_start_tick = g_tick;
    g_arm_action_done_tick = 0;
    g_arm_action_segment_index = 0;
    g_arm_action_current_step = 0;
    g_arm_action_segment_source = source;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = true;

    return FSP_SUCCESS;
}

/* 把物品朝向(0.1度)换算成 J5 脉宽，沿用运动学的双斜率(中位约1457us, ±90度≈957/958us)，
 * 并 clamp 到 J5 量程。方向用 ARM_NG_YAW_SIGN 控制，现场反了改符号即可。 */
static uint16_t arm_ng_yaw_to_j5_us(int16_t object_yaw_x10)
{
    float roll_deg = ((float)ARM_NG_YAW_SIGN * (float)object_yaw_x10) / 10.0f;
    float neutral = (float)arm_control_get_home_us(ARM_NG_J5_CHANNEL);
    float us;
    uint16_t min_us;
    uint16_t max_us;

    if (roll_deg <= 0.0f)
    {
        us = neutral + ((roll_deg * 957.0f) / 90.0f);
    }
    else
    {
        us = neutral + ((roll_deg * 958.0f) / 90.0f);
    }

    min_us = arm_control_get_min_us(ARM_NG_J5_CHANNEL);
    max_us = arm_control_get_max_us(ARM_NG_J5_CHANNEL);
    if (us < (float)min_us)
    {
        us = (float)min_us;
    }
    if (us > (float)max_us)
    {
        us = (float)max_us;
    }

    return (uint16_t)(us + 0.5f);
}

/* 注入一段“只动 J5”的平滑自定义段：其余关节维持当前目标不动。 */
static fsp_err_t arm_action_begin_ng_j5_segment(uint16_t j5_us)
{
    uint16_t target_us[ARM_SERVO_COUNT];

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        target_us[i] = arm_control_get_target_us(i);
    }
    target_us[ARM_NG_J5_CHANNEL] = j5_us;

    return arm_action_begin_custom_segment(target_us, ARM_NG_J5_MOVE_MS, ARM_CONTROL_SOURCE_PLAYBACK);
}

fsp_err_t arm_action_init(void)
{
    fsp_err_t load_err;

    g_arm_action_step_count = 0;
    g_arm_action_current_step = 0;
    g_arm_action_last_step = 0;
    g_arm_action_segment_index = 0;
    g_arm_action_state = ARM_ACTION_STATE_READY;
    g_arm_action_segment_source = ARM_CONTROL_SOURCE_PLAYBACK;
    g_arm_action_playing = false;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = false;
    arm_ng_reset();
    g_arm_action_segment_duration_ms = ARM_ACTION_HOME_DURATION_MS;
    g_arm_action_segment_start_tick = g_tick;

    memset(g_arm_action_steps, 0, sizeof(g_arm_action_steps));
    memset(g_arm_action_segment_start_us, 0, sizeof(g_arm_action_segment_start_us));
    memset(g_arm_action_segment_target_us, 0, sizeof(g_arm_action_segment_target_us));

    load_err = arm_action_load_from_flash();
    if (FSP_SUCCESS == load_err)
    {
        my_printf(&g_debug_uart,
                  "arm action loaded from flash count=%u\r\n",
                  (unsigned int)g_arm_action_step_count);
    }
    else if (FSP_ERR_NOT_FOUND == load_err)
    {
        my_printf(&g_debug_uart, "arm action use empty slot, no flash record\r\n");
    }
    else
    {
        my_printf(&g_debug_uart, "arm action flash load err=%d\r\n", load_err);
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_action_add_step(uint16_t duration_ms)
{
    fsp_err_t err;
    uint8_t old_step_count;
    uint8_t old_current_step;
    uint8_t old_last_step;
    arm_action_state_t old_state;

    if (!arm_control_is_ready())
    {
        arm_action_enter_error();
        return FSP_ERR_NOT_OPEN;
    }

    if (g_arm_action_playing || (g_arm_action_step_count >= ARM_ACTION_MAX_STEPS))
    {
        arm_action_enter_error();
        return FSP_ERR_INVALID_ARGUMENT;
    }

    old_step_count = g_arm_action_step_count;
    old_current_step = g_arm_action_current_step;
    old_last_step = g_arm_action_last_step;
    old_state = g_arm_action_state;

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_action_steps[g_arm_action_step_count].servo_us[i] = arm_control_get_target_us(i);
    }

    g_arm_action_steps[g_arm_action_step_count].duration_ms = arm_action_clamp_duration(duration_ms);
    g_arm_action_step_count++;
    g_arm_action_last_step = g_arm_action_step_count;
    g_arm_action_current_step = 0;
    g_arm_action_state = ARM_ACTION_STATE_RECORD;

    err = arm_action_save_to_flash();
    if (FSP_SUCCESS != err)
    {
        memset(&g_arm_action_steps[old_step_count], 0, sizeof(g_arm_action_steps[old_step_count]));
        g_arm_action_step_count = old_step_count;
        g_arm_action_current_step = old_current_step;
        g_arm_action_last_step = old_last_step;
        g_arm_action_state = old_state;
        return err;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_action_delete_last_step(void)
{
    fsp_err_t err;
    arm_action_step_t deleted_step;
    uint8_t old_step_count;
    uint8_t old_current_step;
    uint8_t old_last_step;
    arm_action_state_t old_state;
    bool deleted_valid = false;

    if (g_arm_action_playing)
    {
        arm_action_enter_error();
        return FSP_ERR_INVALID_ARGUMENT;
    }

    old_step_count = g_arm_action_step_count;
    old_current_step = g_arm_action_current_step;
    old_last_step = g_arm_action_last_step;
    old_state = g_arm_action_state;
    memset(&deleted_step, 0, sizeof(deleted_step));

    if (g_arm_action_step_count > 0U)
    {
        g_arm_action_step_count--;
        deleted_step = g_arm_action_steps[g_arm_action_step_count];
        deleted_valid = true;
        memset(&g_arm_action_steps[g_arm_action_step_count], 0, sizeof(g_arm_action_steps[g_arm_action_step_count]));
    }

    g_arm_action_last_step = g_arm_action_step_count;
    g_arm_action_current_step = 0;
    g_arm_action_state = ARM_ACTION_STATE_READY;

    err = arm_action_save_to_flash();
    if (FSP_SUCCESS != err)
    {
        if (deleted_valid && (old_step_count > 0U) && (old_step_count <= ARM_ACTION_MAX_STEPS))
        {
            g_arm_action_steps[old_step_count - 1U] = deleted_step;
        }
        g_arm_action_step_count = old_step_count;
        g_arm_action_current_step = old_current_step;
        g_arm_action_last_step = old_last_step;
        g_arm_action_state = old_state;
        return err;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_action_clear(void)
{
    fsp_err_t err;
    arm_action_step_t old_steps[ARM_ACTION_MAX_STEPS];
    uint8_t old_step_count;
    uint8_t old_current_step;
    uint8_t old_last_step;
    arm_action_state_t old_state;

    if (g_arm_action_playing)
    {
        arm_action_enter_error();
        return FSP_ERR_INVALID_ARGUMENT;
    }

    memcpy(old_steps, g_arm_action_steps, sizeof(old_steps));
    old_step_count = g_arm_action_step_count;
    old_current_step = g_arm_action_current_step;
    old_last_step = g_arm_action_last_step;
    old_state = g_arm_action_state;

    memset(g_arm_action_steps, 0, sizeof(g_arm_action_steps));
    g_arm_action_step_count = 0;
    g_arm_action_current_step = 0;
    g_arm_action_last_step = 0;
    g_arm_action_state = ARM_ACTION_STATE_READY;

    err = arm_action_save_to_flash();
    if (FSP_SUCCESS != err)
    {
        memcpy(g_arm_action_steps, old_steps, sizeof(g_arm_action_steps));
        g_arm_action_step_count = old_step_count;
        g_arm_action_current_step = old_current_step;
        g_arm_action_last_step = old_last_step;
        g_arm_action_state = old_state;
        return err;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_action_play(void)
{
    fsp_err_t err;

    if (!arm_control_is_ready())
    {
        arm_action_enter_error();
        return FSP_ERR_NOT_OPEN;
    }

    if (0U == g_arm_action_step_count)
    {
        arm_action_enter_error();
        return FSP_ERR_INVALID_ARGUMENT;
    }

    arm_ng_reset();   /* 普通播放：确保不带 NG 腕补偿 */

    err = arm_action_begin_recorded_segment(0U, true);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_arm_action_playing = true;
    g_arm_action_state = ARM_ACTION_STATE_PLAYING;
    buzzer_beep(BUZZER_DEFAULT_BEEP_MS);

    return FSP_SUCCESS;
}

/* NG 分拣播放：在标准录制动作里叠加 J5 腕部对齐/复位。
 * param_valid=false 或 |yaw|<死区 或 步数不足 -> 退化成普通 arm_action_play()，零额外停顿。 */
fsp_err_t arm_action_play_ng(int16_t object_yaw_x10, bool param_valid)
{
    fsp_err_t err;
    int16_t mag;
    bool do_ng;

    mag = (object_yaw_x10 < 0) ? (int16_t)(-object_yaw_x10) : object_yaw_x10;
    do_ng = param_valid &&
            (mag >= ARM_NG_DEADZONE_X10) &&
            (g_arm_action_step_count >= (uint8_t)(ARM_NG_RESET_AFTER_IDX + 2U));

    err = arm_action_play();   /* 内部已 arm_ng_reset() 并启动第0步 */
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (do_ng)
    {
        g_arm_ng_neutral_us = arm_control_get_home_us(ARM_NG_J5_CHANNEL);
        g_arm_ng_aligned_us = arm_ng_yaw_to_j5_us(object_yaw_x10);
        g_arm_ng_active = true;
        g_arm_ng_phase = ARM_NG_PRE;
        g_arm_ng_resume_index = 0;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_action_stop(void)
{
    arm_ng_reset();
    g_arm_action_playing = false;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = false;
    g_arm_action_current_step = 0;
    g_arm_action_state = ARM_ACTION_STATE_STOPPED;

    return FSP_SUCCESS;
}

fsp_err_t arm_action_home(uint16_t duration_ms, arm_control_source_t source)
{
    fsp_err_t err;

    if (!arm_control_is_ready())
    {
        arm_action_enter_error();
        return FSP_ERR_NOT_OPEN;
    }

    arm_ng_reset();

    err = arm_action_begin_home_segment(duration_ms, true, source);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_arm_action_playing = true;
    g_arm_action_current_step = 0;
    g_arm_action_state = ARM_ACTION_STATE_PLAYING;

    return FSP_SUCCESS;
}

fsp_err_t arm_action_move_to_targets(const uint16_t target_us[ARM_SERVO_COUNT],
                                     uint16_t duration_ms,
                                     arm_control_source_t source)
{
    fsp_err_t err;

    if (!arm_control_is_ready())
    {
        arm_action_enter_error();
        return FSP_ERR_NOT_OPEN;
    }

    if (g_arm_action_playing)
    {
        return FSP_ERR_IN_USE;
    }

    arm_ng_reset();

    err = arm_action_begin_custom_segment(target_us, duration_ms, source);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_arm_action_playing = true;
    g_arm_action_state = ARM_ACTION_STATE_PLAYING;

    return FSP_SUCCESS;
}

fsp_err_t arm_action_load_from_flash(void)
{
    arm_config_action_step_t saved_steps[ARM_CONFIG_ACTION_MAX_STEPS];
    uint8_t step_count = 0;
    fsp_err_t err;

    if (ARM_CONFIG_ACTION_SERVO_COUNT != ARM_SERVO_COUNT)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (ARM_CONFIG_ACTION_MAX_STEPS != ARM_ACTION_MAX_STEPS)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (g_arm_action_playing)
    {
        return FSP_ERR_IN_USE;
    }

    err = arm_config_load_action(saved_steps, &step_count);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    memset(g_arm_action_steps, 0, sizeof(g_arm_action_steps));
    for (uint8_t step = 0; step < step_count; step++)
    {
        for (uint8_t ch = 0; ch < ARM_SERVO_COUNT; ch++)
        {
            g_arm_action_steps[step].servo_us[ch] = saved_steps[step].servo_us[ch];
        }
        g_arm_action_steps[step].duration_ms = arm_action_clamp_duration(saved_steps[step].duration_ms);
    }

    g_arm_action_step_count = step_count;
    g_arm_action_current_step = 0;
    g_arm_action_last_step = step_count;
    g_arm_action_state = ARM_ACTION_STATE_READY;
    g_arm_action_playing = false;
    g_arm_action_home_segment = false;
    g_arm_action_custom_segment = false;

    return FSP_SUCCESS;
}

fsp_err_t arm_action_save_to_flash(void)
{
    arm_config_action_step_t save_steps[ARM_CONFIG_ACTION_MAX_STEPS];

    if (ARM_CONFIG_ACTION_SERVO_COUNT != ARM_SERVO_COUNT)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (ARM_CONFIG_ACTION_MAX_STEPS != ARM_ACTION_MAX_STEPS)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (g_arm_action_playing)
    {
        return FSP_ERR_IN_USE;
    }

    memset(save_steps, 0, sizeof(save_steps));
    for (uint8_t step = 0; step < g_arm_action_step_count; step++)
    {
        for (uint8_t ch = 0; ch < ARM_SERVO_COUNT; ch++)
        {
            save_steps[step].servo_us[ch] = g_arm_action_steps[step].servo_us[ch];
        }
        save_steps[step].duration_ms = arm_action_clamp_duration(g_arm_action_steps[step].duration_ms);
    }

    return arm_config_save_action(save_steps, g_arm_action_step_count);
}

bool arm_action_has_steps(void)
{
    return (g_arm_action_step_count > 0U);
}

void arm_action_task(void)
{
    uint32_t elapsed_ms;
    uint32_t scale_q15;
    uint16_t next_targets[ARM_SERVO_COUNT];
    fsp_err_t err;

    if (!g_arm_action_playing)
    {
        if ((ARM_ACTION_STATE_DONE == g_arm_action_state) &&
            ((uint32_t)(g_tick - g_arm_action_done_tick) >= ARM_ACTION_DONE_HOLD_MS))
        {
            g_arm_action_current_step = 0;
            g_arm_action_state = ARM_ACTION_STATE_READY;
        }
        return;
    }

    elapsed_ms = (uint32_t)(g_tick - g_arm_action_segment_start_tick);

    if (elapsed_ms >= g_arm_action_segment_duration_ms)
    {
        err = arm_action_apply_targets(g_arm_action_segment_target_us);
        if (FSP_SUCCESS != err)
        {
            return;
        }

        if (g_arm_action_home_segment)
        {
            arm_ng_reset();
            g_arm_action_playing = false;
            g_arm_action_home_segment = false;
            g_arm_action_state = ARM_ACTION_STATE_DONE;
            g_arm_action_done_tick = g_tick;
            return;
        }

        if (g_arm_action_custom_segment)
        {
            /* NG: 注入的 J5 段完成后，恢复录制播放(而非 DONE)。 */
            if (g_arm_ng_active && (ARM_NG_ALIGNING == g_arm_ng_phase))
            {
                g_arm_ng_phase = ARM_NG_HELD;
                (void)arm_action_begin_recorded_segment(g_arm_ng_resume_index, false);
                return;
            }
            if (g_arm_ng_active && (ARM_NG_RESETTING == g_arm_ng_phase))
            {
                g_arm_ng_phase = ARM_NG_POST;
                (void)arm_action_begin_recorded_segment(g_arm_ng_resume_index, false);
                return;
            }

            g_arm_action_playing = false;
            g_arm_action_custom_segment = false;
            g_arm_action_state = ARM_ACTION_STATE_DONE;
            g_arm_action_done_tick = g_tick;
            return;
        }

        /* NG: 某录制步刚完成 -> 在推进前判断是否注入 J5 段。 */
        if (g_arm_ng_active && (ARM_NG_PRE == g_arm_ng_phase) &&
            (ARM_NG_ALIGN_AFTER_IDX == g_arm_action_segment_index))
        {
            if (FSP_SUCCESS == arm_action_begin_ng_j5_segment(g_arm_ng_aligned_us))
            {
                g_arm_ng_resume_index = (uint8_t)(g_arm_action_segment_index + 1U);
                g_arm_ng_phase = ARM_NG_ALIGNING;
                return;
            }
            arm_ng_reset();   /* 注入失败：放弃 NG，按原版推进 */
        }
        else if (g_arm_ng_active && (ARM_NG_HELD == g_arm_ng_phase) &&
                 (ARM_NG_RESET_AFTER_IDX == g_arm_action_segment_index))
        {
            uint8_t next_index = (uint8_t)(g_arm_action_segment_index + 1U);
            /* 复位到“下一录制步(第5步)的 J5”，让 POST 段衔接零跳变；越界则退回中位。 */
            uint16_t reset_j5_us = (next_index < g_arm_action_step_count)
                                   ? g_arm_action_steps[next_index].servo_us[ARM_NG_J5_CHANNEL]
                                   : g_arm_ng_neutral_us;

            if (FSP_SUCCESS == arm_action_begin_ng_j5_segment(reset_j5_us))
            {
                g_arm_ng_resume_index = next_index;
                g_arm_ng_phase = ARM_NG_RESETTING;
                return;
            }
            arm_ng_reset();
        }

        if ((uint8_t)(g_arm_action_segment_index + 1U) < g_arm_action_step_count)
        {
            (void)arm_action_begin_recorded_segment((uint8_t)(g_arm_action_segment_index + 1U), false);
        }
        else
        {
            arm_ng_reset();
            (void)arm_action_begin_home_segment(ARM_ACTION_HOME_DURATION_MS,
                                                false,
                                                ARM_CONTROL_SOURCE_PLAYBACK);
        }

        return;
    }

    scale_q15 = arm_action_smoothstep_q15(elapsed_ms, g_arm_action_segment_duration_ms);

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        next_targets[i] = arm_action_interpolate_us(g_arm_action_segment_start_us[i],
                                                    g_arm_action_segment_target_us[i],
                                                    scale_q15);
    }

    (void)arm_action_apply_targets(next_targets);
}

void arm_action_get_info(arm_action_info_t *p_info)
{
    uint32_t elapsed_ms;

    if (NULL == p_info)
    {
        return;
    }

    p_info->slot_index = ARM_ACTION_SLOT_INDEX;
    p_info->step_count = g_arm_action_step_count;
    p_info->current_step = g_arm_action_current_step;
    p_info->last_step = g_arm_action_last_step;
    p_info->state = g_arm_action_state;
    if (g_arm_action_playing)
    {
        elapsed_ms = (uint32_t)(g_tick - g_arm_action_segment_start_tick);
        if (elapsed_ms >= g_arm_action_segment_duration_ms)
        {
            p_info->playback_progress = 1000U;
        }
        else
        {
            p_info->playback_progress =
                    (uint16_t)((elapsed_ms * 1000UL) / g_arm_action_segment_duration_ms);
        }
    }
    else if (ARM_ACTION_STATE_DONE == g_arm_action_state)
    {
        p_info->playback_progress = 1000U;
    }
    else
    {
        p_info->playback_progress = 0U;
    }
    p_info->playing = g_arm_action_playing;
}

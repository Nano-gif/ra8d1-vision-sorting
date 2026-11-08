#include "arm_control_app.h"
#include "mydefine.h"

#if ARM_CONFIG_HOME_SERVO_COUNT != ARM_SERVO_COUNT
#error "ARM_CONFIG_HOME_SERVO_COUNT must match ARM_SERVO_COUNT"
#endif

typedef struct st_arm_servo_config
{
    uint16_t min_us;
    uint16_t max_us;
    uint16_t home_us;
} arm_servo_config_t;

static const arm_servo_config_t g_arm_servo_config[ARM_SERVO_COUNT] =
{
    {500U, 2500U, 1500U}, /* J1 Base */
    {500U, 2327U, 500U},  /* J2 Shldr */
    {500U, 2356U, 500U},  /* J3 Elbow */
    {1500U, 2443U, 2443U},/* J4 Pitch */
    {500U, 2415U, 1457U}, /* J5 Roll */
    {500U, 1500U, 500U},  /* J6 Grip */
};

static uint16_t g_arm_target_us[ARM_SERVO_COUNT];
static uint16_t g_arm_current_us[ARM_SERVO_COUNT];
static uint16_t g_arm_min_us[ARM_SERVO_COUNT];
static uint16_t g_arm_max_us[ARM_SERVO_COUNT];
static uint16_t g_arm_home_us[ARM_SERVO_COUNT];
static arm_control_source_t g_arm_last_source[ARM_SERVO_COUNT];
static bool g_arm_output_dirty[ARM_SERVO_COUNT];
static bool g_arm_ready = false;

static bool arm_control_channel_is_valid(uint8_t channel)
{
    return (channel < ARM_SERVO_COUNT);
}

static uint16_t arm_control_clamp_us(uint8_t channel, uint16_t pulse_us)
{
    if (pulse_us < g_arm_min_us[channel])
    {
        return g_arm_min_us[channel];
    }

    if (pulse_us > g_arm_max_us[channel])
    {
        return g_arm_max_us[channel];
    }

    return pulse_us;
}

static void arm_control_apply_home_us_all(const uint16_t home_us[ARM_SERVO_COUNT])
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_home_us[i] = arm_control_clamp_us(i, home_us[i]);
    }
}

fsp_err_t arm_control_init(void)
{
    fsp_err_t err;
    fsp_err_t load_err;
    uint16_t saved_home_us[ARM_SERVO_COUNT];

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_min_us[i] = g_arm_servo_config[i].min_us;
        g_arm_max_us[i] = g_arm_servo_config[i].max_us;
        g_arm_home_us[i] = arm_control_clamp_us(i, g_arm_servo_config[i].home_us);
        g_arm_target_us[i] = g_arm_home_us[i];
        g_arm_current_us[i] = g_arm_home_us[i];
        g_arm_last_source[i] = ARM_CONTROL_SOURCE_NONE;
        g_arm_output_dirty[i] = false;
    }

    load_err = arm_config_load_home(saved_home_us);
    if (FSP_SUCCESS == load_err)
    {
        arm_control_apply_home_us_all(saved_home_us);
        my_printf(&g_debug_uart, "arm home loaded from flash\r\n");
    }
    else if (FSP_ERR_NOT_FOUND == load_err)
    {
        my_printf(&g_debug_uart, "arm home use default, no flash record\r\n");
    }
    else
    {
        my_printf(&g_debug_uart, "arm home use default, load err=%d\r\n", load_err);
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_target_us[i] = g_arm_home_us[i];
        g_arm_current_us[i] = g_arm_home_us[i];
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        err = pca9685_set_servo_us(i, g_arm_target_us[i]);
        if (FSP_SUCCESS != err)
        {
            g_arm_ready = false;
            return err;
        }
    }

    g_arm_ready = true;
    return FSP_SUCCESS;
}

fsp_err_t arm_control_set_target_us(uint8_t channel, uint16_t pulse_us, arm_control_source_t source)
{
    uint16_t clamped_us;

    if (!arm_control_channel_is_valid(channel))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!g_arm_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    clamped_us = arm_control_clamp_us(channel, pulse_us);
    g_arm_last_source[channel] = source;

    if (g_arm_target_us[channel] != clamped_us)
    {
        g_arm_target_us[channel] = clamped_us;
        g_arm_output_dirty[channel] = true;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_control_home(arm_control_source_t source)
{
    if (!g_arm_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_target_us[i] = g_arm_home_us[i];
        g_arm_last_source[i] = source;
        g_arm_output_dirty[i] = true;
    }

    return FSP_SUCCESS;
}

fsp_err_t arm_control_save_home_from_current_targets(void)
{
    uint16_t home_us[ARM_SERVO_COUNT];
    fsp_err_t err;

    if (!g_arm_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        home_us[i] = arm_control_clamp_us(i, g_arm_target_us[i]);
    }

    err = arm_config_save_home(home_us);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    arm_control_apply_home_us_all(home_us);

    return FSP_SUCCESS;
}

fsp_err_t arm_control_set_home_us_all(const uint16_t home_us[ARM_SERVO_COUNT])
{
    if (NULL == home_us)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!g_arm_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    arm_control_apply_home_us_all(home_us);

    return FSP_SUCCESS;
}

void arm_control_get_home_us_all(uint16_t home_us[ARM_SERVO_COUNT])
{
    if (NULL == home_us)
    {
        return;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        home_us[i] = g_arm_home_us[i];
    }
}

uint16_t arm_control_get_target_us(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return 0;
    }

    return g_arm_target_us[channel];
}

uint16_t arm_control_get_current_us(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return 0;
    }

    return g_arm_current_us[channel];
}

uint16_t arm_control_get_min_us(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return 0;
    }

    return g_arm_min_us[channel];
}

uint16_t arm_control_get_max_us(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return 0;
    }

    return g_arm_max_us[channel];
}

uint16_t arm_control_get_home_us(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return 0;
    }

    return g_arm_home_us[channel];
}

arm_control_source_t arm_control_get_last_source(uint8_t channel)
{
    if (!arm_control_channel_is_valid(channel))
    {
        return ARM_CONTROL_SOURCE_NONE;
    }

    return g_arm_last_source[channel];
}

bool arm_control_is_ready(void)
{
    return g_arm_ready;
}

void arm_control_task(void)
{
    fsp_err_t err;
    bool has_dirty = false;

    if (!g_arm_ready)
    {
        return;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (g_arm_output_dirty[i])
        {
            has_dirty = true;
            break;
        }
    }

    if (!has_dirty)
    {
        return;
    }

    err = pca9685_set_servo_us_range(0U, g_arm_target_us, ARM_SERVO_COUNT);
    if (FSP_SUCCESS != err)
    {
        return;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        g_arm_current_us[i] = g_arm_target_us[i];
        g_arm_output_dirty[i] = false;
    }
}

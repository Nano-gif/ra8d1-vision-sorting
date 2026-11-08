#include "light_pwm_app.h"

#include "bsp_pin_cfg.h"
#include "hal_data.h"

#include <rtdevice.h>
#include <rthw.h>
#include <stdlib.h>

#ifndef LIGHT_EN
#define LIGHT_EN  BSP_IO_PORT_05_PIN_09
#endif

#ifndef LIGHT_PWM
#define LIGHT_PWM BSP_IO_PORT_05_PIN_08
#endif

#define LIGHT_PWM_APP_DEFAULT_FREQ_HZ       10000U
#define LIGHT_PWM_APP_MIN_FREQ_HZ           10U
#define LIGHT_PWM_APP_MAX_FREQ_HZ           10000U
#define LIGHT_PWM_APP_AUTOSTART_LEVEL       5U
#define LIGHT_PWM_APP_EN_MASK               (1U << 9)
#define LIGHT_PWM_APP_PWM_MASK              (1U << 8)
#define LIGHT_PWM_APP_TIMER_PERIOD_US       10U
#define LIGHT_PWM_APP_TIMER_HZ              (1000000U / LIGHT_PWM_APP_TIMER_PERIOD_US)
#define LIGHT_PWM_APP_MIN_PHASE_STEPS       10U
#define LIGHT_PWM_APP_MAX_PHASE_STEPS       10000U

static rt_bool_t s_light_opened = RT_FALSE;
static volatile rt_bool_t s_light_enabled = RT_FALSE;
static rt_bool_t s_light_timer_opened = RT_FALSE;
static volatile rt_bool_t s_light_timer_running = RT_FALSE;
static struct rt_mutex s_light_lock;
static rt_bool_t s_light_lock_ready = RT_FALSE;
static uint8_t s_light_level = 0U;
static volatile uint16_t s_light_duty_permille = 0U;
static uint32_t s_light_frequency_hz = LIGHT_PWM_APP_DEFAULT_FREQ_HZ;
static volatile uint32_t s_light_phase_steps = LIGHT_PWM_APP_TIMER_HZ / LIGHT_PWM_APP_DEFAULT_FREQ_HZ;
static volatile uint32_t s_light_pwm_cycles = 0U;
static volatile uint32_t s_light_irq_ticks = 0U;
static volatile uint32_t s_light_phase = 0U;
static volatile uint32_t s_light_on_steps = 0U;

static uint32_t light_pwm_app_parse_u32(const char *text, uint32_t default_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return default_value;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return default_value;
    }

    return (uint32_t)value;
}

static void light_pwm_app_lock(void)
{
    if (s_light_lock_ready == RT_TRUE)
    {
        (void)rt_mutex_take(&s_light_lock, RT_WAITING_FOREVER);
    }
}

static void light_pwm_app_unlock(void)
{
    if (s_light_lock_ready == RT_TRUE)
    {
        (void)rt_mutex_release(&s_light_lock);
    }
}

static void light_pwm_app_en_release(void)
{
    R_PORT5->PCNTR3 = LIGHT_PWM_APP_EN_MASK;
}

static void light_pwm_app_en_assert(void)
{
    R_PORT5->PCNTR3 = (LIGHT_PWM_APP_EN_MASK << 16);
}

static void light_pwm_app_pwm_set(rt_uint8_t level)
{
    R_PORT5->PCNTR3 = (level != PIN_LOW) ? LIGHT_PWM_APP_PWM_MASK : (LIGHT_PWM_APP_PWM_MASK << 16);
}

static void light_pwm_app_update_pwm_params_locked(void)
{
    uint32_t phase_steps;
    uint32_t on_steps;
    uint32_t freq = s_light_frequency_hz;

    if (freq < LIGHT_PWM_APP_MIN_FREQ_HZ)
    {
        freq = LIGHT_PWM_APP_MIN_FREQ_HZ;
    }
    if (freq > LIGHT_PWM_APP_MAX_FREQ_HZ)
    {
        freq = LIGHT_PWM_APP_MAX_FREQ_HZ;
    }

    phase_steps = LIGHT_PWM_APP_TIMER_HZ / freq;
    if (phase_steps < LIGHT_PWM_APP_MIN_PHASE_STEPS)
    {
        phase_steps = LIGHT_PWM_APP_MIN_PHASE_STEPS;
    }
    if (phase_steps > LIGHT_PWM_APP_MAX_PHASE_STEPS)
    {
        phase_steps = LIGHT_PWM_APP_MAX_PHASE_STEPS;
    }

    on_steps = ((uint32_t)s_light_duty_permille * phase_steps + (LIGHT_PWM_APP_DUTY_MAX / 2U)) /
               LIGHT_PWM_APP_DUTY_MAX;
    if ((s_light_duty_permille > 0U) && (on_steps == 0U))
    {
        on_steps = 1U;
    }
    if ((s_light_duty_permille < LIGHT_PWM_APP_DUTY_MAX) && (on_steps >= phase_steps))
    {
        on_steps = phase_steps - 1U;
    }

    s_light_phase_steps = phase_steps;
    s_light_on_steps = on_steps;
    if (s_light_phase >= phase_steps)
    {
        s_light_phase = 0U;
    }
}

static void light_pwm_app_gpio_init(void)
{
    /*
     * LIGHT_EN 在 RASC 中配置为开漏输出：写高电平即释放引脚，
     * 由光源板上的 R41 上拉，保证上电时光源处于安全的关闭状态。
     */
    R_BSP_PinAccessEnable();
    R_BSP_PinCfg(LIGHT_EN,
                 IOPORT_CFG_NMOS_ENABLE |
                 IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                 IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_BSP_PinCfg(LIGHT_PWM,
                 IOPORT_CFG_DRIVE_HIGH |
                 IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                 IOPORT_CFG_PORT_OUTPUT_LOW);
    light_pwm_app_pwm_set(PIN_LOW);
    light_pwm_app_en_release();
    R_BSP_PinAccessDisable();
}

static void light_pwm_app_apply_enable(rt_bool_t enable)
{
    if ((enable == RT_TRUE) && (s_light_duty_permille > 0U))
    {
        light_pwm_app_update_pwm_params_locked();
        if (s_light_duty_permille >= LIGHT_PWM_APP_DUTY_MAX)
        {
            light_pwm_app_pwm_set(PIN_HIGH);
        }
        light_pwm_app_en_assert();
        s_light_enabled = RT_TRUE;
    }
    else
    {
        light_pwm_app_pwm_set(PIN_LOW);
        light_pwm_app_en_release();
        s_light_enabled = RT_FALSE;
    }
}

static int light_pwm_app_timer_open(void)
{
    fsp_err_t err;

    if (s_light_timer_opened == RT_TRUE)
    {
        return RT_EOK;
    }

    err = R_GPT_Open(&g_light_pwm_timer_ctrl, &g_light_pwm_timer_cfg);
    if ((err != FSP_SUCCESS) && (err != FSP_ERR_ALREADY_OPEN))
    {
        return -RT_ERROR;
    }

    (void)R_GPT_Stop(&g_light_pwm_timer_ctrl);
    (void)R_GPT_Reset(&g_light_pwm_timer_ctrl);
    s_light_timer_opened = RT_TRUE;
    return RT_EOK;
}

static int light_pwm_app_timer_start(void)
{
    if (light_pwm_app_timer_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (s_light_timer_running == RT_TRUE)
    {
        return RT_EOK;
    }

    if (R_GPT_Start(&g_light_pwm_timer_ctrl) != FSP_SUCCESS)
    {
        return -RT_ERROR;
    }

    s_light_timer_running = RT_TRUE;
    return RT_EOK;
}

static void light_pwm_app_timer_stop(void)
{
    if ((s_light_timer_opened == RT_TRUE) && (s_light_timer_running == RT_TRUE))
    {
        (void)R_GPT_Stop(&g_light_pwm_timer_ctrl);
        (void)R_GPT_Reset(&g_light_pwm_timer_ctrl);
        s_light_timer_running = RT_FALSE;
    }
}

static void light_pwm_app_make_ready(void)
{
    if (s_light_lock_ready != RT_TRUE)
    {
        if (rt_mutex_init(&s_light_lock, "lightpwm", RT_IPC_FLAG_PRIO) == RT_EOK)
        {
            s_light_lock_ready = RT_TRUE;
        }
    }

    if (s_light_opened != RT_TRUE)
    {
        light_pwm_app_gpio_init();
        s_light_opened = RT_TRUE;
    }
}

int light_pwm_app_open(void)
{
    light_pwm_app_make_ready();
    return light_pwm_app_timer_open();
}

int light_pwm_app_set_enable(rt_bool_t enable)
{
    if (light_pwm_app_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    light_pwm_app_lock();
    if ((enable == RT_TRUE) &&
        (s_light_duty_permille > 0U) &&
        (s_light_duty_permille < LIGHT_PWM_APP_DUTY_MAX))
    {
        if (light_pwm_app_timer_start() != RT_EOK)
        {
            light_pwm_app_unlock();
            return -RT_ERROR;
        }
    }
    light_pwm_app_apply_enable(enable);
    if ((s_light_enabled != RT_TRUE) || (s_light_duty_permille >= LIGHT_PWM_APP_DUTY_MAX))
    {
        light_pwm_app_timer_stop();
    }
    light_pwm_app_unlock();
    return RT_EOK;
}

int light_pwm_app_set_duty_permille(uint16_t duty_permille)
{
    if (duty_permille > LIGHT_PWM_APP_DUTY_MAX)
    {
        duty_permille = LIGHT_PWM_APP_DUTY_MAX;
    }

    if (light_pwm_app_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    light_pwm_app_lock();
    s_light_duty_permille = duty_permille;
    s_light_level = (uint8_t)((duty_permille + 50U) / 100U);
    light_pwm_app_update_pwm_params_locked();
    if ((duty_permille > 0U) && (duty_permille < LIGHT_PWM_APP_DUTY_MAX))
    {
        if (light_pwm_app_timer_start() != RT_EOK)
        {
            light_pwm_app_unlock();
            return -RT_ERROR;
        }
    }
    light_pwm_app_apply_enable((duty_permille > 0U) ? RT_TRUE : RT_FALSE);
    if ((s_light_enabled != RT_TRUE) || (s_light_duty_permille >= LIGHT_PWM_APP_DUTY_MAX))
    {
        light_pwm_app_timer_stop();
    }
    light_pwm_app_unlock();
    return RT_EOK;
}

int light_pwm_app_set_level(uint8_t level)
{
    if (level > LIGHT_PWM_APP_LEVEL_MAX)
    {
        level = LIGHT_PWM_APP_LEVEL_MAX;
    }

    return light_pwm_app_set_duty_permille((uint16_t)level * 100U);
}

int light_pwm_app_set_percent(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    return light_pwm_app_set_duty_permille((uint16_t)percent * 10U);
}

int light_pwm_app_set_frequency(uint32_t frequency_hz)
{
    if (frequency_hz < LIGHT_PWM_APP_MIN_FREQ_HZ)
    {
        frequency_hz = LIGHT_PWM_APP_MIN_FREQ_HZ;
    }
    if (frequency_hz > LIGHT_PWM_APP_MAX_FREQ_HZ)
    {
        frequency_hz = LIGHT_PWM_APP_MAX_FREQ_HZ;
    }

    if (light_pwm_app_open() != RT_EOK)
    {
        return -RT_ERROR;
    }

    light_pwm_app_lock();
    s_light_frequency_hz = frequency_hz;
    light_pwm_app_update_pwm_params_locked();
    light_pwm_app_unlock();
    return RT_EOK;
}

void light_pwm_app_timer_cb(timer_callback_args_t *p_args)
{
    uint32_t phase;
    uint32_t phase_steps;
    uint32_t on_steps;

    if ((p_args == RT_NULL) || (p_args->event != TIMER_EVENT_CYCLE_END))
    {
        return;
    }

    s_light_irq_ticks++;

    if ((s_light_enabled != RT_TRUE) || (s_light_duty_permille == 0U))
    {
        light_pwm_app_pwm_set(PIN_LOW);
        s_light_phase = 0U;
        return;
    }

    if (s_light_duty_permille >= LIGHT_PWM_APP_DUTY_MAX)
    {
        light_pwm_app_pwm_set(PIN_HIGH);
        s_light_phase = 0U;
        return;
    }

    phase = s_light_phase;
    phase_steps = s_light_phase_steps;
    on_steps = s_light_on_steps;

    if (phase_steps == 0U)
    {
        phase_steps = LIGHT_PWM_APP_MIN_PHASE_STEPS;
    }

    light_pwm_app_pwm_set((phase < on_steps) ? PIN_HIGH : PIN_LOW);

    phase++;
    if (phase >= phase_steps)
    {
        phase = 0U;
        s_light_pwm_cycles++;
    }

    s_light_phase = phase;
}

void light_pwm_app_get_state(light_pwm_app_state_t *state)
{
    if (state == RT_NULL)
    {
        return;
    }

    light_pwm_app_lock();
    state->opened = s_light_opened;
    state->enabled = s_light_enabled;
    state->level = s_light_level;
    state->duty_permille = s_light_duty_permille;
    state->frequency_hz = s_light_frequency_hz;
    state->pwm_cycles = s_light_pwm_cycles;
    state->irq_ticks = s_light_irq_ticks;
    state->phase_steps = s_light_phase_steps;
    state->timer_running = s_light_timer_running;
    light_pwm_app_unlock();

    state->en_level = rt_pin_read(LIGHT_EN);
    state->pwm_level = rt_pin_read(LIGHT_PWM);
}

static void light_pwm_app_print_state(void)
{
    light_pwm_app_state_t state;

    light_pwm_app_get_state(&state);
    rt_kprintf("[light] opened=%s timer=%s enabled=%s level=%u/%u duty=%u.%u%% freq=%u Hz steps=%u cycles=%u irq=%u\n",
               (state.opened == RT_TRUE) ? "yes" : "no",
               (state.timer_running == RT_TRUE) ? "on" : "off",
               (state.enabled == RT_TRUE) ? "yes" : "no",
               (unsigned int)state.level,
               (unsigned int)LIGHT_PWM_APP_LEVEL_MAX,
               (unsigned int)(state.duty_permille / 10U),
               (unsigned int)(state.duty_permille % 10U),
               (unsigned int)state.frequency_hz,
               (unsigned int)state.phase_steps,
               (unsigned int)state.pwm_cycles,
               (unsigned int)state.irq_ticks);
    rt_kprintf("[light] pins EN=P509(open-drain low=on high=off) level=%d PWM=P508 level=%d\n",
               state.en_level,
               state.pwm_level);
}

static int light_open_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (light_pwm_app_open() != RT_EOK)
    {
        rt_kprintf("[light] open failed\n");
        return -1;
    }

    rt_kprintf("[light] opened GPT timer software PWM, EN=P509 low=on high/release=off, PWM=P508\n");
    light_pwm_app_print_state();
    return 0;
}

static int light_on_cmd(int argc, char **argv)
{
    uint32_t level = LIGHT_PWM_APP_AUTOSTART_LEVEL;

    if (argc >= 2)
    {
        level = light_pwm_app_parse_u32(argv[1], LIGHT_PWM_APP_AUTOSTART_LEVEL);
    }

    if (level > LIGHT_PWM_APP_LEVEL_MAX)
    {
        level = LIGHT_PWM_APP_LEVEL_MAX;
    }

    if (light_pwm_app_set_level((uint8_t)level) != RT_EOK)
    {
        rt_kprintf("[light] on failed\n");
        return -1;
    }

    rt_kprintf("[light] on level=%u/%u\n",
               (unsigned int)level,
               (unsigned int)LIGHT_PWM_APP_LEVEL_MAX);
    return 0;
}

static int light_off_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (light_pwm_app_set_enable(RT_FALSE) != RT_EOK)
    {
        rt_kprintf("[light] off failed\n");
        return -1;
    }

    rt_kprintf("[light] off, EN released high and PWM low\n");
    return 0;
}

static int light_set_cmd(int argc, char **argv)
{
    uint32_t level;

    if (argc < 2)
    {
        rt_kprintf("[light] usage: light_set <0-10>\n");
        return -1;
    }

    level = light_pwm_app_parse_u32(argv[1], 0U);
    if (level > LIGHT_PWM_APP_LEVEL_MAX)
    {
        level = LIGHT_PWM_APP_LEVEL_MAX;
    }

    if (light_pwm_app_set_level((uint8_t)level) != RT_EOK)
    {
        rt_kprintf("[light] set failed\n");
        return -1;
    }

    rt_kprintf("[light] set level=%u/%u\n",
               (unsigned int)level,
               (unsigned int)LIGHT_PWM_APP_LEVEL_MAX);
    return 0;
}

static int light_percent_cmd(int argc, char **argv)
{
    uint32_t percent;

    if (argc < 2)
    {
        rt_kprintf("[light] usage: light_percent <0-100>\n");
        return -1;
    }

    percent = light_pwm_app_parse_u32(argv[1], 0U);
    if (percent > 100U)
    {
        percent = 100U;
    }

    if (light_pwm_app_set_percent((uint8_t)percent) != RT_EOK)
    {
        rt_kprintf("[light] percent failed\n");
        return -1;
    }

    rt_kprintf("[light] set percent=%u%%\n", (unsigned int)percent);
    return 0;
}

static int light_freq_cmd(int argc, char **argv)
{
    uint32_t freq;

    if (argc < 2)
    {
        rt_kprintf("[light] usage: light_freq <10-10000 Hz>\n");
        return -1;
    }

    freq = light_pwm_app_parse_u32(argv[1], LIGHT_PWM_APP_DEFAULT_FREQ_HZ);
    if (light_pwm_app_set_frequency(freq) != RT_EOK)
    {
        rt_kprintf("[light] freq failed\n");
        return -1;
    }

    rt_kprintf("[light] set freq=%u Hz\n", (unsigned int)freq);
    return 0;
}

static int light_stat_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    light_pwm_app_print_state();
    return 0;
}

MSH_CMD_EXPORT_ALIAS(light_open_cmd, light_open, open light software PWM controller);
MSH_CMD_EXPORT_ALIAS(light_on_cmd, light_on, turn light on with optional level 0-10);
MSH_CMD_EXPORT_ALIAS(light_off_cmd, light_off, turn light off);
MSH_CMD_EXPORT_ALIAS(light_set_cmd, light_set, set light level 0-10);
MSH_CMD_EXPORT_ALIAS(light_percent_cmd, light_percent, set light percent 0-100);
MSH_CMD_EXPORT_ALIAS(light_freq_cmd, light_freq, set light software PWM frequency);
MSH_CMD_EXPORT_ALIAS(light_stat_cmd, light_stat, show light PWM state);

#ifndef LIGHT_PWM_APP_H
#define LIGHT_PWM_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_PWM_APP_LEVEL_MAX 10U
#define LIGHT_PWM_APP_DUTY_MAX  1000U

typedef struct
{
    rt_bool_t opened;
    rt_bool_t enabled;
    uint8_t level;
    uint16_t duty_permille;
    uint32_t frequency_hz;
    uint32_t pwm_cycles;
    uint32_t irq_ticks;
    uint32_t phase_steps;
    rt_bool_t timer_running;
    int en_level;
    int pwm_level;
} light_pwm_app_state_t;

int light_pwm_app_open(void);
int light_pwm_app_set_enable(rt_bool_t enable);
int light_pwm_app_set_level(uint8_t level);
int light_pwm_app_set_percent(uint8_t percent);
int light_pwm_app_set_duty_permille(uint16_t duty_permille);
int light_pwm_app_set_frequency(uint32_t frequency_hz);
void light_pwm_app_get_state(light_pwm_app_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_PWM_APP_H */

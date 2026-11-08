#ifndef PCA9685_APP_H_
#define PCA9685_APP_H_

#include "mydefine.h"

#define PCA9685_REG_MODE1      0x00
#define PCA9685_REG_MODE2      0x01
#define PCA9685_REG_LED0_ON_L  0x06
#define PCA9685_REG_LED0_ON_H  0x07
#define PCA9685_REG_LED0_OFF_L 0x08
#define PCA9685_REG_LED0_OFF_H 0x09
#define PCA9685_REG_PRE_SCALE  0xFE

#define PCA9685_SERVO_50HZ_PRESCALE  0x79U
#define PCA9685_SERVO_PERIOD_US      20000U
#define PCA9685_SERVO_MIN_US         500U
#define PCA9685_SERVO_MID_US         1500U
#define PCA9685_SERVO_MAX_US         2500U
#define PCA9685_PWM_STEPS            4096U

fsp_err_t pca9685_init(void);
fsp_err_t pca9685_write_reg(uint8_t reg, uint8_t data);
fsp_err_t pca9685_read_reg(uint8_t reg, uint8_t *p_data);
fsp_err_t pca9685_set_pwm_ticks(uint8_t channel, uint16_t on_tick, uint16_t off_tick);
uint16_t pca9685_servo_us_to_tick(uint16_t pulse_us);
fsp_err_t pca9685_set_servo_us(uint8_t channel, uint16_t pulse_us);
fsp_err_t pca9685_set_servo_us_range(uint8_t first_channel, const uint16_t *p_pulse_us, uint8_t count);
fsp_err_t pca9685_set_50hz(void);
fsp_err_t pca9685_quick_test(void);
void pca_i2c_callback(i2c_master_callback_args_t *p_args);

#endif

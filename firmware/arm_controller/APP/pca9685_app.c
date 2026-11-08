#include "pca9685_app.h"

#define PCA9685_I2C_TIMEOUT_MS  100U

static volatile bool g_pca_i2c_done = false;
static volatile i2c_master_event_t g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

void pca_i2c_callback(i2c_master_callback_args_t *p_args)
{
		g_pca_i2c_event = p_args->event;

		if ((I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) ||
				(I2C_MASTER_EVENT_RX_COMPLETE == p_args->event) ||
				(I2C_MASTER_EVENT_ABORTED == p_args->event))
		{
				g_pca_i2c_done = true;
		}
}

static fsp_err_t pca_i2c_wait_done(i2c_master_event_t expected_event)
{
		uint32_t start_tick = g_tick;

		while (!g_pca_i2c_done)
		{
				if ((uint32_t)(g_tick - start_tick) > PCA9685_I2C_TIMEOUT_MS)
				{
						g_pca_i2c.p_api->abort(g_pca_i2c.p_ctrl);
						return FSP_ERR_TIMEOUT;
				}
		}

		if (expected_event != g_pca_i2c_event)
		{
				return FSP_ERR_ABORTED;
		}

		return FSP_SUCCESS;
}

fsp_err_t pca9685_init(void)
{
		return g_pca_i2c.p_api->open(g_pca_i2c.p_ctrl, g_pca_i2c.p_cfg);
}

fsp_err_t pca9685_write_reg(uint8_t reg, uint8_t data)
{
		fsp_err_t err;
		uint8_t tx_buf[2];

		tx_buf[0] = reg;
		tx_buf[1] = data;

		g_pca_i2c_done = false;
		g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

		err = g_pca_i2c.p_api->write(g_pca_i2c.p_ctrl, tx_buf, 2, false);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return pca_i2c_wait_done(I2C_MASTER_EVENT_TX_COMPLETE);
}

fsp_err_t pca9685_read_reg(uint8_t reg, uint8_t *p_data)
{
		fsp_err_t err;

		if (NULL == p_data)
		{
				return FSP_ERR_ASSERTION;
		}

		g_pca_i2c_done = false;
		g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

		err = g_pca_i2c.p_api->write(g_pca_i2c.p_ctrl, &reg, 1, true);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		err = pca_i2c_wait_done(I2C_MASTER_EVENT_TX_COMPLETE);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		g_pca_i2c_done = false;
		g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

		err = g_pca_i2c.p_api->read(g_pca_i2c.p_ctrl, p_data, 1, false);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return pca_i2c_wait_done(I2C_MASTER_EVENT_RX_COMPLETE);
}

static fsp_err_t pca9685_write4(uint8_t start_reg,
																uint8_t data0,
																uint8_t data1,
																uint8_t data2,
																uint8_t data3)
{
		fsp_err_t err;
		uint8_t tx_buf[5];

		tx_buf[0] = start_reg;
		tx_buf[1] = data0;
		tx_buf[2] = data1;
		tx_buf[3] = data2;
		tx_buf[4] = data3;

		g_pca_i2c_done = false;
		g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

		err = g_pca_i2c.p_api->write(g_pca_i2c.p_ctrl, tx_buf, 5, false);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return pca_i2c_wait_done(I2C_MASTER_EVENT_TX_COMPLETE);
}

static fsp_err_t pca9685_print_led0_regs(const char *p_tag)
{
		fsp_err_t err;
		uint8_t on_l = 0;
		uint8_t on_h = 0;
		uint8_t off_l = 0;
		uint8_t off_h = 0;

		err = pca9685_read_reg(PCA9685_REG_LED0_ON_L, &on_l);
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 read LED0_ON_L failed: %d\r\n", err);
				return err;
		}

		err = pca9685_read_reg(PCA9685_REG_LED0_ON_H, &on_h);
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 read LED0_ON_H failed: %d\r\n", err);
				return err;
		}

		err = pca9685_read_reg(PCA9685_REG_LED0_OFF_L, &off_l);
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 read LED0_OFF_L failed: %d\r\n", err);
				return err;
		}

		err = pca9685_read_reg(PCA9685_REG_LED0_OFF_H, &off_h);
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 read LED0_OFF_H failed: %d\r\n", err);
				return err;
		}

		my_printf(&g_debug_uart,
							"LED0 %s: ON_L=0x%02X ON_H=0x%02X OFF_L=0x%02X OFF_H=0x%02X\r\n",
							p_tag,
							on_l,
							on_h,
							off_l,
							off_h);

		return FSP_SUCCESS;
}

uint16_t pca9685_servo_us_to_tick(uint16_t pulse_us)
{
		uint32_t tick;

		if (pulse_us < PCA9685_SERVO_MIN_US)
		{
				pulse_us = PCA9685_SERVO_MIN_US;
		}
		else if (pulse_us > PCA9685_SERVO_MAX_US)
		{
				pulse_us = PCA9685_SERVO_MAX_US;
		}
		else
		{
				/* 范围内，保持原值。 */
		}

		tick = ((uint32_t)pulse_us * PCA9685_PWM_STEPS + (PCA9685_SERVO_PERIOD_US / 2U)) /
					 PCA9685_SERVO_PERIOD_US;

		if (tick > (PCA9685_PWM_STEPS - 1U))
		{
				tick = PCA9685_PWM_STEPS - 1U;
		}

		return (uint16_t)tick;
}

fsp_err_t pca9685_set_pwm_ticks(uint8_t channel, uint16_t on_tick, uint16_t off_tick)
{
		uint8_t start_reg;

		if (channel > 15U)
		{
				return FSP_ERR_INVALID_ARGUMENT;
		}

		on_tick &= 0x0FFFU;
		off_tick &= 0x0FFFU;
		start_reg = (uint8_t)(PCA9685_REG_LED0_ON_L + (uint8_t)(4U * channel));

		return pca9685_write4(start_reg,
													(uint8_t)(on_tick & 0xFFU),
													(uint8_t)((on_tick >> 8) & 0x0FU),
													(uint8_t)(off_tick & 0xFFU),
													(uint8_t)((off_tick >> 8) & 0x0FU));
}

fsp_err_t pca9685_set_servo_us(uint8_t channel, uint16_t pulse_us)
{
		return pca9685_set_pwm_ticks(channel, 0, pca9685_servo_us_to_tick(pulse_us));
}

fsp_err_t pca9685_set_servo_us_range(uint8_t first_channel, const uint16_t *p_pulse_us, uint8_t count)
{
		fsp_err_t err;
		uint8_t tx_buf[1U + (4U * 16U)];
		uint8_t tx_len;

		if (NULL == p_pulse_us)
		{
				return FSP_ERR_ASSERTION;
		}

		if ((0U == count) || (first_channel > 15U) || (count > (uint8_t)(16U - first_channel)))
		{
				return FSP_ERR_INVALID_ARGUMENT;
		}

		tx_buf[0] = (uint8_t)(PCA9685_REG_LED0_ON_L + (uint8_t)(4U * first_channel));
		tx_len = (uint8_t)(1U + (4U * count));

		for (uint8_t i = 0; i < count; i++)
		{
				uint16_t off_tick = pca9685_servo_us_to_tick(p_pulse_us[i]);
				uint8_t offset = (uint8_t)(1U + (4U * i));

				tx_buf[offset] = 0U;
				tx_buf[offset + 1U] = 0U;
				tx_buf[offset + 2U] = (uint8_t)(off_tick & 0xFFU);
				tx_buf[offset + 3U] = (uint8_t)((off_tick >> 8) & 0x0FU);
		}

		g_pca_i2c_done = false;
		g_pca_i2c_event = I2C_MASTER_EVENT_ABORTED;

		err = g_pca_i2c.p_api->write(g_pca_i2c.p_ctrl, tx_buf, tx_len, false);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return pca_i2c_wait_done(I2C_MASTER_EVENT_TX_COMPLETE);
}

fsp_err_t pca9685_set_50hz(void)
{
		fsp_err_t err;

		err = pca9685_write_reg(PCA9685_REG_MODE1, 0x11U);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

		err = pca9685_write_reg(PCA9685_REG_PRE_SCALE, PCA9685_SERVO_50HZ_PRESCALE);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		err = pca9685_write_reg(PCA9685_REG_MODE1, 0x21U);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

		err = pca9685_write_reg(PCA9685_REG_MODE2, 0x04U);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return FSP_SUCCESS;
}

fsp_err_t pca9685_quick_test(void)
{
		fsp_err_t err;
		uint8_t mode1 = 0;
		uint8_t prescale = 0;

		err = pca9685_read_reg(PCA9685_REG_MODE1, &mode1);
		my_printf(&g_debug_uart, "PCA9685 MODE1 before = 0x%02X err=%d\r\n", mode1, err);
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		err = pca9685_set_50hz();
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 set 50Hz failed: %d\r\n", err);
				return err;
		}

		err = pca9685_read_reg(PCA9685_REG_MODE1, &mode1);
		if (FSP_SUCCESS == err)
		{
				my_printf(&g_debug_uart, "PCA9685 MODE1 after = 0x%02X\r\n", mode1);
		}
		else
		{
				my_printf(&g_debug_uart, "PCA9685 read MODE1 after failed: %d\r\n", err);
				return err;
		}

		err = pca9685_read_reg(PCA9685_REG_PRE_SCALE, &prescale);
		if (FSP_SUCCESS == err)
		{
				my_printf(&g_debug_uart, "PCA9685 PRE_SCALE = 0x%02X\r\n", prescale);
		}
		else
		{
				my_printf(&g_debug_uart, "PCA9685 read PRE_SCALE failed: %d\r\n", err);
				return err;
		}

		err = pca9685_set_servo_us(0, PCA9685_SERVO_MID_US);
		if (FSP_SUCCESS != err)
		{
				my_printf(&g_debug_uart, "PCA9685 CH0 pwm failed: %d\r\n", err);
				return err;
		}

		my_printf(&g_debug_uart, "PCA9685 CH0 50Hz 1500us output set\r\n");
		err = pca9685_print_led0_regs("pwm");
		if (FSP_SUCCESS != err)
		{
				return err;
		}

		return FSP_SUCCESS;
}

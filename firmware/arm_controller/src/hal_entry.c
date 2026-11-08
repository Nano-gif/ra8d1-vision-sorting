#include "hal_data.h"
#include "mydefine.h"
#include "debug_uart_app.h"

/* 上电初始化各外设与应用模块，随后进入协作式调度主循环 */
void hal_entry(void)
{
    systick_init_us(1000);
    debug_uart_init();
    my_printf(&g_debug_uart, "\r\nRA6M5 robotic arm controller\r\n");
    my_printf(&g_debug_uart, "Please input: channel pulse_us\r\n");

    fsp_err_t err;

    err = buzzer_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "buzzer init failed: %d\r\n", err);
    }

    err = hmi_uart_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "hmi uart open failed: %d\r\n", err);
    }
    else
    {
        my_printf(&g_debug_uart, "hmi uart open ok\r\n");
    }

    err = rs485_uart_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "rs485 uart open failed: %d\r\n", err);
    }
    else
    {
        my_printf(&g_debug_uart, "rs485 uart open ok\r\n");
    }

    err = arm_manual_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "arm manual init failed: %d\r\n", err);
    }

    err = arm_config_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "arm config flash open failed: %d\r\n", err);
    }
    else
    {
        my_printf(&g_debug_uart, "arm config flash open ok\r\n");
    }

    err = pca9685_init();
    if (FSP_SUCCESS != err)
    {
        my_printf(&g_debug_uart, "PCA9685 i2c open failed: %d\r\n", err);
    }
    else
    {
        err = pca9685_quick_test();
        if (FSP_SUCCESS != err)
        {
            my_printf(&g_debug_uart, "PCA9685 quick test failed: %d\r\n", err);
        }
        else
        {
            err = arm_control_init();
            if (FSP_SUCCESS != err)
            {
                my_printf(&g_debug_uart, "arm control init failed: %d\r\n", err);
            }
            else
            {
                err = arm_action_init();
                if (FSP_SUCCESS != err)
                {
                    my_printf(&g_debug_uart, "arm action init failed: %d\r\n", err);
                }
                err = modbus_slave_init();
                if (FSP_SUCCESS != err)
                {
                    my_printf(&g_debug_uart, "modbus slave init failed: %d\r\n", err);
                }
                else
                {
                    my_printf(&g_debug_uart, "modbus slave ready: addr=1 baud=57600\r\n");
                }
                my_printf(&g_debug_uart, "arm control ready: %u servos\r\n", (unsigned int)ARM_SERVO_COUNT);
            }
        }
    }
    scheduler_init();
    while (1)
    {
        scheduler_run();
    }
}

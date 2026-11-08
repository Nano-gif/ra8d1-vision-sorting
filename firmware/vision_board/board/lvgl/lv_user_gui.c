/*
 * LVGL user GUI entry for the RT-Thread Vision Board project.
 *
 * Call chain:
 * lvgl_thread_entry()
 *   -> lv_port_disp_init()   display driver
 *   -> lv_port_indev_init()  GT1151 touch input
 *   -> lv_user_gui_init()    application UI starts here
 */

#include <rtdevice.h>
#include <rtthread.h>

#include "ui/ui.h"
#include "ui/ui_settings_data.h"
#include "model_app.h"

#define LCD_BACKLIGHT_PWM_DEV_NAME      "pwm6"
#define LCD_BACKLIGHT_PWM_CHANNEL       0
#define LCD_BACKLIGHT_PWM_PERIOD_NS     10000U
#define LCD_BACKLIGHT_MIN_PERCENT       5U

static void board_set_lcd_brightness(uint16_t percent)
{
    static struct rt_device_pwm *pwm_dev = RT_NULL;

    if (percent > 100U) {
        percent = 100U;
    }

    /*
     * Keep a small minimum value so sliding to 0 does not look like the
     * display has died. Change this to 0U if you want true backlight off.
     */
    if (percent < LCD_BACKLIGHT_MIN_PERCENT) {
        percent = LCD_BACKLIGHT_MIN_PERCENT;
    }

    if (pwm_dev == RT_NULL) {
        pwm_dev = (struct rt_device_pwm *)rt_device_find(LCD_BACKLIGHT_PWM_DEV_NAME);
        if (pwm_dev == RT_NULL) {
            rt_kprintf("LCD backlight PWM device %s not found\r\n", LCD_BACKLIGHT_PWM_DEV_NAME);
            return;
        }

        rt_pwm_enable(pwm_dev, LCD_BACKLIGHT_PWM_CHANNEL);
    }

    rt_pwm_set(pwm_dev,
               LCD_BACKLIGHT_PWM_CHANNEL,
               LCD_BACKLIGHT_PWM_PERIOD_NS,
               (LCD_BACKLIGHT_PWM_PERIOD_NS * percent) / 100U);
}

static void board_ui_settings_command_handler(ui_settings_command_t cmd,
                                              uint16_t value,
                                              void *user_data)
{
    (void)user_data;

    if (cmd == UI_SETTINGS_CMD_SET_BRIGHTNESS) {
        board_set_lcd_brightness(value);
    } else if (cmd == UI_SETTINGS_CMD_LOAD_MODEL) {
        const ui_settings_model_file_t *model = ui_settings_data_get_selected_model();

        (void)value;
        if (model == RT_NULL) {
            rt_kprintf("[settings] no selected model\r\n");
            return;
        }

        if (model_app_load_package(model->path) == RT_EOK) {
            rt_kprintf("[settings] model loaded: %s\r\n", model->name);
        } else {
            rt_kprintf("[settings] model load failed: %s\r\n", model_app_last_error());
        }
    }
}

void lv_user_gui_init(void)
{
    const ui_settings_snapshot_t *settings;

    (void)ui_settings_data_load_config();
    ui_settings_data_set_command_handler(board_ui_settings_command_handler, RT_NULL);
    settings = ui_settings_data_get_snapshot();
    board_set_lcd_brightness(settings->display.brightness_percent);

    /* ui_init() is the same UI entry used by the PC simulator project. */
    ui_init();
}

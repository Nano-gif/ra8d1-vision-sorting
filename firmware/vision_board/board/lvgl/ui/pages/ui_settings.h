#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "lvgl/lvgl.h"

/*
 * Create the Settings page.
 *
 * First version is a static product-style layout with fake data. Real SD scan,
 * PWM brightness and model loading will be connected through ui_settings_data.
 */
lv_obj_t *ui_settings_create(lv_obj_t *parent);

/*
 * Called when the Settings page becomes visible.
 *
 * It retries loading /vision/config.ini because TF-card mounting can finish
 * after the LVGL page was created.
 */
void ui_settings_on_show(void);

#endif /* UI_SETTINGS_H */

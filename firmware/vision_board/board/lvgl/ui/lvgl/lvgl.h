/*
 * Compatibility header for UI code copied from the PC LVGL simulator.
 *
 * PC project include style:
 *     #include "lvgl/lvgl.h"
 *
 * RT-Thread LVGL package include style:
 *     #include <lvgl.h>
 *
 * Keeping this shim means the UI folder can be copied from the PC project
 * without editing every UI source file each time.
 */

#ifndef UI_LVGL_COMPAT_H
#define UI_LVGL_COMPAT_H

#include <lvgl.h>

/* LVGL v8-style names used by the PC UI, mapped to LVGL v9 names. */
#ifndef LV_LABEL_LONG_MODE_CLIP
#define LV_LABEL_LONG_MODE_CLIP LV_LABEL_LONG_CLIP
#endif

#ifndef LV_LABEL_LONG_MODE_WRAP
#define LV_LABEL_LONG_MODE_WRAP LV_LABEL_LONG_WRAP
#endif

#endif /* UI_LVGL_COMPAT_H */

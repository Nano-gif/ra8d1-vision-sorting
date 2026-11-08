#ifndef UI_H
#define UI_H

#include "lvgl/lvgl.h"

/*
 * UI 总入口。
 *
 * main.c 只需要调用 ui_init()。
 * 具体页面内容放在 src/ui/pages/，公共配置放在 ui_config.h。
 */
void ui_init(void);

/* Notify the UI that TF-card/storage state changed. */
void ui_notify_storage_changed(void);

/*
 * Process deferred UI events from non-LVGL threads.
 *
 * Call this from the LVGL thread before lv_task_handler(). The function only
 * checks RAM flags; it does not poll the TF card or scan the file system.
 */
void ui_process_pending_events(void);

#endif /* UI_H */

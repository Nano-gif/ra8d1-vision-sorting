#ifndef UI_LANGUAGE_H
#define UI_LANGUAGE_H

#include "lvgl/lvgl.h"
#include <stdbool.h>

/*
 * UI 全局语言模块。
 *
 * 作用：
 * 1. 统一保存当前语言，避免每个页面各存一份语言状态。
 * 2. 统一提供中英文文本选择函数，后续页面中文化时直接调用 ui_text()。
 * 3. 统一提供中文/英文字体，后续移植到 RT-Thread 时只需要保证字体文件一起编译。
 */

typedef enum {
    UI_LANGUAGE_ZH_CN = 0,     /* 中文界面，作为当前默认语言。 */
    UI_LANGUAGE_EN_US,         /* 英文界面，方便调试或展示。 */
} ui_language_t;

typedef void (*ui_language_changed_cb_t)(ui_language_t language, void *user_data);

/* 返回当前全局语言。 */
ui_language_t ui_language_get(void);

/* 设置当前全局语言；语言变化后会通知已注册的回调。 */
void ui_language_set(ui_language_t language);

/* 判断当前是否为中文界面。 */
bool ui_language_is_chinese(void);

/* 根据当前语言返回中文或英文文案，页面代码里尽量用这个函数减少 if 判断。 */
const char *ui_text(const char *zh_cn, const char *en_us);

/* 返回普通文本字体；中文用项目专用中文字库，英文用 LVGL 内置 Montserrat。 */
const lv_font_t *ui_font_body(void);

/* 返回标题文本字体；中文标题暂用 16px 字库，英文标题继续用较大的 Montserrat。 */
const lv_font_t *ui_font_title(void);

/* 注册语言变化回调；当前只保留一个全局回调，足够让 ui.c 刷新外壳导航。 */
void ui_language_set_changed_callback(ui_language_changed_cb_t cb, void *user_data);

#endif /* UI_LANGUAGE_H */

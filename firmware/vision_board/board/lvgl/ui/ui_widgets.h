#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "lvgl/lvgl.h"
#include <stdint.h>

/*
 * UI 通用控件。
 *
 * 这里放多个页面都会用到的“小零件”：面板、状态胶囊、指标卡、操作按钮。
 * 页面文件只负责摆放和业务含义，不重复写基础样式。
 */

lv_obj_t *ui_widget_create_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height,
                                 const char *title);

lv_obj_t *ui_widget_create_status_pill(lv_obj_t *parent,
                                       const char *text,
                                       int32_t x,
                                       int32_t width,
                                       uint32_t bg_color,
                                       uint32_t border_color);

lv_obj_t *ui_widget_create_metric_card(lv_obj_t *parent,
                                       int32_t x,
                                       int32_t y,
                                       const char *value,
                                       const char *name);

lv_obj_t *ui_widget_create_action_button(lv_obj_t *parent,
                                         int32_t x,
                                         int32_t y,
                                         int32_t width,
                                         const char *text,
                                         uint32_t bg_color,
                                         uint32_t border_color);

#endif /* UI_WIDGETS_H */

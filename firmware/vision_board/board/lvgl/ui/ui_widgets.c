#include "ui_widgets.h"
#include "ui_config.h"

/* 创建通用面板：带深色背景、细边框和左上标题。 */
lv_obj_t *ui_widget_create_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height,
                                 const char *title)
{
    /* Detect/Collect/Data/Settings 后续都可以复用这个样式。 */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, 14, 12);

    return panel;
}

/* 创建顶栏状态胶囊，例如 RUN/STOP、相机状态、模型名和时间。 */
lv_obj_t *ui_widget_create_status_pill(lv_obj_t *parent,
                                       const char *text,
                                       int32_t x,
                                       int32_t width,
                                       uint32_t bg_color,
                                       uint32_t border_color)
{
    /* 顶栏里的状态胶囊，比如 RUN、相机、模型、时间。 */
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, width, 24);
    lv_obj_set_pos(pill, x, 10);
    lv_obj_set_style_bg_color(pill, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(pill);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);

    return pill;
}

/* 创建推理指标卡片，上方大数字，下方指标名称。 */
lv_obj_t *ui_widget_create_metric_card(lv_obj_t *parent,
                                       int32_t x,
                                       int32_t y,
                                       const char *value,
                                       const char *name)
{
    /* 指标卡用于显示 max score、AI time、FPS 等数据。 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 126, 42);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(value_label, 10, 4);

    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(name_label, 10, 24);

    return card;
}

/* 创建检测页操作按钮，后续可绑定 Start/Stop/Refresh 命令事件。 */
lv_obj_t *ui_widget_create_action_button(lv_obj_t *parent,
                                         int32_t x,
                                         int32_t y,
                                         int32_t width,
                                         const char *text,
                                         uint32_t bg_color,
                                         uint32_t border_color)
{
    /* 这里只负责按钮样式，具体点击事件由页面文件绑定。 */
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, 30);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);

    return btn;
}

#include "ui_collect.h"
#include "../ui_config.h"
#include "../ui_language.h"
#include "../ui_widgets.h"
#include "camera_app.h"
#include "storage_app.h"
#include <stdio.h>

#define UI_COLLECT_AUTO_CAPTURE_PERIOD_MS 1200U
#define UI_COLLECT_CAPTURE_TIMEOUT_MS 3000U

typedef struct {
    uint32_t bg_color;
    uint32_t grad_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t strip_color;
    lv_opa_t border_opa;
    lv_opa_t strip_opa;
    int32_t shadow_width;
    lv_opa_t shadow_opa;
} ui_collect_step_style_t;

typedef struct {
    lv_obj_t *frame;
    lv_obj_t *angle_label;
    lv_obj_t *angle_icon;
    lv_obj_t *angle_value_label;
} ui_collect_placement_guide_t;

static ui_collect_angle_step_t s_angle_steps[] = {
    {"中心", "Center", "物体在ROI中心，姿态接近正式检测。", "Object centered in ROI, close to runtime pose.", 0, 0, 150, UI_COLLECT_STEP_CURRENT},
    {"左偏", "Left", "物体中心略向左，约5到10像素。", "Object slightly left, about 5 to 10 px.", 0, 0, 70, UI_COLLECT_STEP_PENDING},
    {"右偏", "Right", "物体中心略向右，约5到10像素。", "Object slightly right, about 5 to 10 px.", 0, 0, 70, UI_COLLECT_STEP_PENDING},
    {"上偏", "Upper", "物体中心略向上，约5到10像素。", "Object slightly upper, about 5 to 10 px.", 0, 0, 70, UI_COLLECT_STEP_PENDING},
    {"下偏", "Lower", "物体中心略向下，约5到10像素。", "Object slightly lower, about 5 to 10 px.", 0, 0, 70, UI_COLLECT_STEP_PENDING},
    {"轻微旋转", "Rotate", "物体轻微旋转，范围约正负5度。", "Object slightly rotated, about +/-5 deg.", 5, 0, 70, UI_COLLECT_STEP_PENDING},
};

#define UI_COLLECT_ANGLE_COUNT (sizeof(s_angle_steps) / sizeof(s_angle_steps[0]))

static lv_timer_t *s_auto_capture_timer = NULL;
static uint8_t s_collect_tip_shown = 0;
static lv_obj_t *s_collect_tip_overlay = NULL;
static lv_obj_t *s_preview_title_label = NULL;
static lv_obj_t *s_guide_title_label = NULL;
static lv_obj_t *s_progress_title_label = NULL;
static lv_obj_t *s_angle_list_title_label = NULL;
static lv_obj_t *s_capture_button_label = NULL;
static lv_obj_t *s_auto_button = NULL;
static lv_obj_t *s_reset_button = NULL;
static lv_obj_t *s_progress_value_label = NULL;
static lv_obj_t *s_progress_current_label = NULL;
static lv_obj_t *s_progress_bar = NULL;
static ui_collect_placement_guide_t s_placement_guide = {0};
static lv_obj_t *s_angle_rows[UI_COLLECT_ANGLE_COUNT];
static lv_obj_t *s_angle_strips[UI_COLLECT_ANGLE_COUNT];
static lv_obj_t *s_angle_labels[UI_COLLECT_ANGLE_COUNT];
static lv_obj_t *s_angle_count_labels[UI_COLLECT_ANGLE_COUNT];
static uint8_t s_collect_capture_busy = 0U;
static int32_t s_auto_capture_step_index = -1;

static void ui_collect_reset_widget_refs(void);
static void ui_collect_create_preview_panel(lv_obj_t *parent);
static void ui_collect_create_camera_preview(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_collect_create_capture_hint_strip(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_collect_create_angle_indicator(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_collect_create_placement_guide(lv_obj_t *preview);
static void ui_collect_create_guide_corner(lv_obj_t *parent,
                                           int32_t x,
                                           int32_t y,
                                           int32_t width,
                                           int32_t height);
static void ui_collect_create_camera_housing(lv_obj_t *parent, int32_t view_x, int32_t view_y);
static void ui_collect_create_housing_screw(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_collect_create_housing_accent(lv_obj_t *parent,
                                             int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             int32_t height,
                                             uint32_t color,
                                             lv_opa_t opa);
static void ui_collect_create_guide_panel(lv_obj_t *parent);
static void ui_collect_create_progress_card(lv_obj_t *parent);
static void ui_collect_create_progress_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t percent);
static void ui_collect_create_hint_button(lv_obj_t *parent);
static void ui_collect_create_angle_list(lv_obj_t *parent);
static void ui_collect_create_angle_row(lv_obj_t *parent, int32_t y, uint32_t index);
static void ui_collect_style_button(lv_obj_t *btn,
                                    uint32_t bg_color,
                                    uint32_t grad_color,
                                    uint32_t border_color,
                                    uint32_t pressed_color);
static void ui_collect_set_auto_button_running(uint8_t running);
static void ui_collect_apply_label_text(lv_obj_t *label, const char *text);
static void ui_collect_apply_panel_title(lv_obj_t *label, const char *text);
static void ui_collect_format_group_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *step);
static void ui_collect_format_guide_group_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *step);
static void ui_collect_format_current_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *current);
static void ui_collect_hint_button_event_cb(lv_event_t *e);
static void ui_collect_reset_button_event_cb(lv_event_t *e);
static uint16_t ui_collect_get_total_captured(void);
static uint16_t ui_collect_get_total_target(void);
static uint8_t ui_collect_is_all_completed(void);
static int32_t ui_collect_get_current_step_index(void);
static ui_collect_angle_step_t *ui_collect_get_current_step(void);
static void ui_collect_select_angle(uint32_t index);
static int ui_collect_save_current_sample(void);
static void ui_collect_angle_row_event_cb(lv_event_t *e);
static void ui_collect_capture_event_cb(lv_event_t *e);
static void ui_collect_auto_capture_event_cb(lv_event_t *e);
static void ui_collect_auto_timer_cb(lv_timer_t *timer);
static void ui_collect_tip_close_event_cb(lv_event_t *e);
static void ui_collect_show_entry_tip(uint8_t force);
static ui_collect_step_style_t ui_collect_get_step_style(ui_collect_step_state_t state);
static void ui_collect_apply_angle_row_style(uint32_t index);
static void ui_collect_refresh_placement_guide(void);
static void ui_collect_refresh_progress(void);
static void ui_collect_refresh_angle_rows(void);
static void ui_collect_refresh_view(void);

/* 创建采集页面；当前只保留可验证的预览区、采集进度和样本分组向导。 */
lv_obj_t *ui_collect_create(lv_obj_t *parent)
{
    lv_obj_t *page;

    ui_collect_stop_auto_capture();
    ui_collect_reset_widget_refs();

    page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    ui_collect_create_preview_panel(page);
    ui_collect_create_guide_panel(page);
    ui_collect_refresh_view();

    return page;
}

/* 采集页面被切换到前台时调用；只在第一次进入时弹出采集提示。 */
void ui_collect_on_show(void)
{
    ui_collect_show_entry_tip(0);
}

/* 手动记录一张样本；保存当前 RGB565 预览帧成功后再更新分组计数。 */
void ui_collect_capture_one_sample(void)
{
    int32_t index = ui_collect_get_current_step_index();

    if (index < 0) {
        ui_collect_refresh_view();
        return;
    }

    ui_collect_angle_step_t *step = &s_angle_steps[index];

    if (step->captured < step->target) {
        step->captured++;
    }

    if (step->captured >= step->target) {
        step->state = UI_COLLECT_STEP_DONE;

        for (int32_t i = index + 1; i < (int32_t)UI_COLLECT_ANGLE_COUNT; i++) {
            if (s_angle_steps[i].state == UI_COLLECT_STEP_PENDING) {
                s_angle_steps[i].state = UI_COLLECT_STEP_CURRENT;
                break;
            }
        }
    }

    ui_collect_refresh_view();
}

/* 清空采集进度；用于一轮样本采满后重新开始采集。 */
void ui_collect_reset_samples(void)
{
    uint8_t was_completed = ui_collect_is_all_completed();

    ui_collect_stop_auto_capture();

    if (storage_app_collect_is_active())
    {
        int ret;

        if (was_completed)
        {
            storage_app_collect_end_session();
            printf("Collect session completed\n");
        }
        else
        {
            ret = storage_app_collect_clear_current_session();
            if (ret != RT_EOK)
            {
                printf("Collect session clear failed: %s\n", storage_app_last_error());
            }
            else
            {
                printf("Collect session cleared\n");
            }
        }
    }

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        s_angle_steps[i].captured = 0;
        s_angle_steps[i].state = (i == 0) ? UI_COLLECT_STEP_CURRENT : UI_COLLECT_STEP_PENDING;
    }

    ui_collect_refresh_view();
    printf("Collect samples reset\n");
}

/* 启动或停止自动采集；使用 LVGL timer 每 1.2 秒保存一张当前预览帧。 */
void ui_collect_toggle_auto_capture(void)
{
    if (s_auto_capture_timer != NULL) {
        ui_collect_stop_auto_capture();
        printf("Auto capture stop\n");
        return;
    }

    s_auto_capture_step_index = ui_collect_get_current_step_index();
    if (s_auto_capture_step_index < 0) {
        printf("Auto capture ignored: all steps completed\n");
        ui_collect_refresh_view();
        return;
    }

    s_auto_capture_timer = lv_timer_create(ui_collect_auto_timer_cb,
                                           UI_COLLECT_AUTO_CAPTURE_PERIOD_MS,
                                           NULL);

    if (s_auto_capture_timer != NULL) {
        ui_collect_set_auto_button_running(1);
        printf("Auto capture start: %s\n",
               s_angle_steps[s_auto_capture_step_index].en_name);
    }
}

/* 停止自动采集 timer；全部采集完成或再次按下 Auto 时调用。 */
void ui_collect_stop_auto_capture(void)
{
    if (s_auto_capture_timer != NULL) {
        lv_timer_del(s_auto_capture_timer);
        s_auto_capture_timer = NULL;
    }

    s_auto_capture_step_index = -1;
    ui_collect_set_auto_button_running(0);
}

/* 全局语言切换后刷新采集页文字；数据数字不变，只重新生成当前语言下的文案。 */
void ui_collect_refresh_language(void)
{
    ui_collect_apply_panel_title(s_preview_title_label, ui_text("采集预览", "Capture Preview"));
    ui_collect_apply_panel_title(s_guide_title_label, ui_text("样本采集", "Sample Capture"));
    ui_collect_apply_label_text(s_progress_title_label, ui_text("样本进度", "Sample Progress"));
    ui_collect_apply_label_text(s_angle_list_title_label, ui_text("样本分组", "Sample Groups"));
    if (s_angle_list_title_label != NULL) {
        lv_obj_set_style_text_font(s_angle_list_title_label,
                                   ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14,
                                   0);
    }
    ui_collect_apply_label_text(s_capture_button_label, ui_text(LV_SYMBOL_IMAGE " 拍照", LV_SYMBOL_IMAGE " Capture"));
    if (s_capture_button_label != NULL) {
        lv_obj_center(s_capture_button_label);
    }

    ui_collect_set_auto_button_running(s_auto_capture_timer != NULL);

    if (s_reset_button != NULL) {
        lv_obj_t *label = lv_obj_get_child(s_reset_button, 0);
        ui_collect_apply_label_text(label, ui_text(LV_SYMBOL_REFRESH " 清空", LV_SYMBOL_REFRESH " Reset"));
        if (label != NULL) {
            lv_obj_center(label);
        }
    }

    ui_collect_refresh_view();
}

/* 清空本页面保存的 LVGL 对象指针；页面重新创建时避免引用旧对象。 */
static void ui_collect_reset_widget_refs(void)
{
    s_preview_title_label = NULL;
    s_guide_title_label = NULL;
    s_progress_title_label = NULL;
    s_angle_list_title_label = NULL;
    s_capture_button_label = NULL;
    s_auto_button = NULL;
    s_reset_button = NULL;
    s_progress_value_label = NULL;
    s_progress_current_label = NULL;
    s_progress_bar = NULL;
    s_placement_guide.frame = NULL;
    s_placement_guide.angle_label = NULL;
    s_placement_guide.angle_icon = NULL;
    s_placement_guide.angle_value_label = NULL;

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        s_angle_rows[i] = NULL;
        s_angle_strips[i] = NULL;
        s_angle_labels[i] = NULL;
        s_angle_count_labels[i] = NULL;
    }
}

/* 创建左侧预览面板；这里只显示 320x240 预览占位。 */
static void ui_collect_create_preview_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = ui_widget_create_panel(parent, 10, 10, 465, 330, ui_text("采集预览", "Capture Preview"));

    s_preview_title_label = lv_obj_get_child(panel, 0);
    ui_collect_apply_panel_title(s_preview_title_label, ui_text("采集预览", "Capture Preview"));

    ui_collect_create_angle_indicator(panel, 330, 10);
    ui_collect_create_camera_housing(panel, 72, 52);
    ui_collect_create_camera_preview(panel, 72, 52);
    ui_collect_create_capture_hint_strip(panel, 72, 304);
}

/* 创建 320x240 摄像头预览占位框；后续再接入真实 RGB565 预览帧。 */
static void ui_collect_create_camera_preview(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *preview = lv_obj_create(parent);
    lv_obj_set_size(preview, UI_CAMERA_VIEW_W, UI_CAMERA_VIEW_H);
    lv_obj_set_pos(preview, x, y);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x10161A), 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(preview, lv_color_hex(0x2C3B45), 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_radius(preview, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

    ui_collect_create_placement_guide(preview);
}

/* 创建相机画面下方的小提示条；分组提示放在画面外，避免遮挡真实预览。 */
static void ui_collect_create_capture_hint_strip(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *strip = lv_obj_create(parent);
    lv_obj_set_size(strip, UI_CAMERA_VIEW_W, 22);
    lv_obj_set_pos(strip, x, y);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x10181D), 0);
    lv_obj_set_style_bg_grad_color(strip, lv_color_hex(0x17242B), 0);
    lv_obj_set_style_bg_grad_dir(strip, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(strip, lv_color_hex(0x2D424B), 0);
    lv_obj_set_style_border_opa(strip, LV_OPA_70, 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_radius(strip, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(strip);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, 10, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 3, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    s_placement_guide.angle_label = lv_label_create(strip);
    lv_label_set_text(s_placement_guide.angle_label, ui_text("当前分组: --", "Current group: --"));
    lv_obj_set_width(s_placement_guide.angle_label, 286);
    lv_label_set_long_mode(s_placement_guide.angle_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s_placement_guide.angle_label, lv_color_hex(0xC9DCE8), 0);
    lv_obj_set_style_text_font(s_placement_guide.angle_label, ui_font_body(), 0);
    lv_obj_set_pos(s_placement_guide.angle_label, 28, 3);
}

/* 创建面板右上角的小分组指示器；旋转分组会轻微旋转方框，但不会遮挡摄像头画面。 */
static void ui_collect_create_angle_indicator(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *indicator = lv_obj_create(parent);
    lv_obj_set_size(indicator, 116, 26);
    lv_obj_set_pos(indicator, x, y);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_bg_grad_color(indicator, lv_color_hex(0x1B2A31), 0);
    lv_obj_set_style_bg_grad_dir(indicator, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(indicator, lv_color_hex(0x314750), 0);
    lv_obj_set_style_border_opa(indicator, LV_OPA_70, 0);
    lv_obj_set_style_border_width(indicator, 1, 0);
    lv_obj_set_style_radius(indicator, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

    s_placement_guide.angle_icon = lv_obj_create(indicator);
    lv_obj_set_size(s_placement_guide.angle_icon, 14, 14);
    lv_obj_set_pos(s_placement_guide.angle_icon, 11, 6);
    lv_obj_set_style_bg_opa(s_placement_guide.angle_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_placement_guide.angle_icon, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(s_placement_guide.angle_icon, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_placement_guide.angle_icon, 2, 0);
    lv_obj_set_style_radius(s_placement_guide.angle_icon, 2, 0);
    lv_obj_set_style_pad_all(s_placement_guide.angle_icon, 0, 0);
    lv_obj_set_style_transform_pivot_x(s_placement_guide.angle_icon, 7, 0);
    lv_obj_set_style_transform_pivot_y(s_placement_guide.angle_icon, 7, 0);
    lv_obj_clear_flag(s_placement_guide.angle_icon, LV_OBJ_FLAG_SCROLLABLE);

    s_placement_guide.angle_value_label = lv_label_create(indicator);
    lv_label_set_text(s_placement_guide.angle_value_label, ui_text("样本", "Sample"));
    lv_obj_set_style_text_color(s_placement_guide.angle_value_label, lv_color_hex(0xC9DCE8), 0);
    lv_obj_set_style_text_font(s_placement_guide.angle_value_label, ui_font_body(), 0);
    lv_obj_set_pos(s_placement_guide.angle_value_label, 36, 6);
}

/* 创建预览区内的浅色摆放引导层；只保留 240x240 外框，不放文字和旋转框。 */
static void ui_collect_create_placement_guide(lv_obj_t *preview)
{
    s_placement_guide.frame = lv_obj_create(preview);
    lv_obj_set_size(s_placement_guide.frame, 240, 240);
    lv_obj_align(s_placement_guide.frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_placement_guide.frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_placement_guide.frame, lv_color_hex(0xBFD7E8), 0);
    lv_obj_set_style_border_opa(s_placement_guide.frame, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_placement_guide.frame, 1, 0);
    lv_obj_set_style_radius(s_placement_guide.frame, 3, 0);
    lv_obj_set_style_pad_all(s_placement_guide.frame, 0, 0);
    lv_obj_clear_flag(s_placement_guide.frame, LV_OBJ_FLAG_SCROLLABLE);

    ui_collect_create_guide_corner(s_placement_guide.frame, -1, -1, 24, 2);
    ui_collect_create_guide_corner(s_placement_guide.frame, -1, -1, 2, 24);
    ui_collect_create_guide_corner(s_placement_guide.frame, 217, -1, 24, 2);
    ui_collect_create_guide_corner(s_placement_guide.frame, 239, -1, 2, 24);
    ui_collect_create_guide_corner(s_placement_guide.frame, -1, 239, 24, 2);
    ui_collect_create_guide_corner(s_placement_guide.frame, -1, 217, 2, 24);
    ui_collect_create_guide_corner(s_placement_guide.frame, 217, 239, 24, 2);
    ui_collect_create_guide_corner(s_placement_guide.frame, 239, 217, 2, 24);
}

/* 创建摆放引导框四角的短线；用于区别采集摆放辅助框和检测页 ROI 框。 */
static void ui_collect_create_guide_corner(lv_obj_t *parent,
                                           int32_t x,
                                           int32_t y,
                                           int32_t width,
                                           int32_t height)
{
    lv_obj_t *corner = lv_obj_create(parent);
    lv_obj_set_size(corner, width, height);
    lv_obj_set_pos(corner, x, y);
    lv_obj_set_style_bg_color(corner, lv_color_hex(0xD7F0FF), 0);
    lv_obj_set_style_bg_opa(corner, LV_OPA_40, 0);
    lv_obj_set_style_border_width(corner, 0, 0);
    lv_obj_set_style_radius(corner, 1, 0);
    lv_obj_set_style_pad_all(corner, 0, 0);
    lv_obj_clear_flag(corner, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建采集页相机外框；外框只做视觉装饰，不改变中间 320x240 真实预览画面尺寸。 */
static void ui_collect_create_camera_housing(lv_obj_t *parent, int32_t view_x, int32_t view_y)
{
    const int32_t shell_x = view_x - 34;
    const int32_t shell_y = view_y - 12;
    const int32_t shell_w = UI_CAMERA_VIEW_W + 68;
    const int32_t shell_h = UI_CAMERA_VIEW_H + 20;

    lv_obj_t *shell = lv_obj_create(parent);
    lv_obj_set_size(shell, shell_w, shell_h);
    lv_obj_set_pos(shell, shell_x, shell_y);
    lv_obj_set_style_bg_color(shell, lv_color_hex(0x141A1F), 0);
    lv_obj_set_style_bg_grad_color(shell, lv_color_hex(0x262F36), 0);
    lv_obj_set_style_bg_grad_dir(shell, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(shell, lv_color_hex(0x3B4A54), 0);
    lv_obj_set_style_border_opa(shell, LV_OPA_70, 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_radius(shell, 7, 0);
    lv_obj_set_style_shadow_width(shell, 12, 0);
    lv_obj_set_style_shadow_color(shell, lv_color_hex(0x05080A), 0);
    lv_obj_set_style_shadow_opa(shell, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(shell, 0, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);

    ui_collect_create_housing_accent(shell, 22, 7, shell_w - 44, 2, 0x6A818B, LV_OPA_50);
    ui_collect_create_housing_accent(shell, 15, 28, 12, shell_h - 56, 0x0E151A, LV_OPA_70);
    ui_collect_create_housing_accent(shell, shell_w - 27, 28, 12, shell_h - 56, 0x0E151A, LV_OPA_70);
    ui_collect_create_housing_accent(shell, 31, 22, 2, shell_h - 44, 0x5D747E, LV_OPA_30);
    ui_collect_create_housing_accent(shell, shell_w - 33, 22, 2, shell_h - 44, 0x5D747E, LV_OPA_30);
    ui_collect_create_housing_accent(shell, 26, shell_h - 9, shell_w - 52, 2, 0x31424A, LV_OPA_70);

    ui_collect_create_housing_screw(shell, 9, 9);
    ui_collect_create_housing_screw(shell, shell_w - 16, 9);
    ui_collect_create_housing_screw(shell, 9, shell_h - 16);
    ui_collect_create_housing_screw(shell, shell_w - 16, shell_h - 16);
}

/* 创建相机外框四角的小螺丝点；用于增强真实硬件模块的装配感。 */
static void ui_collect_create_housing_screw(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *screw = lv_obj_create(parent);
    lv_obj_set_size(screw, 7, 7);
    lv_obj_set_pos(screw, x, y);
    lv_obj_set_style_bg_color(screw, lv_color_hex(0x0E1317), 0);
    lv_obj_set_style_bg_opa(screw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(screw, lv_color_hex(0x4B5A63), 0);
    lv_obj_set_style_border_opa(screw, LV_OPA_70, 0);
    lv_obj_set_style_border_width(screw, 1, 0);
    lv_obj_set_style_radius(screw, 4, 0);
    lv_obj_set_style_pad_all(screw, 0, 0);
    lv_obj_clear_flag(screw, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *slot = lv_obj_create(screw);
    lv_obj_set_size(slot, 4, 1);
    lv_obj_set_pos(slot, 1, 3);
    lv_obj_set_style_bg_color(slot, lv_color_hex(0x5F6D74), 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_70, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建相机外框上的细装饰条；填补外框空白，但不遮挡实际摄像头画面。 */
static void ui_collect_create_housing_accent(lv_obj_t *parent,
                                             int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             int32_t height,
                                             uint32_t color,
                                             lv_opa_t opa)
{
    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_set_size(accent, width, height);
    lv_obj_set_pos(accent, x, y);
    lv_obj_set_style_bg_color(accent, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(accent, opa, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_radius(accent, width > height ? height / 2 : width / 2, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建右侧采集向导面板；这里只渲染总进度、样本分组列表和三个按钮。 */
static void ui_collect_create_guide_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = ui_widget_create_panel(parent, 490, 10, 260, 330, ui_text("样本采集", "Sample Capture"));

    s_guide_title_label = lv_obj_get_child(panel, 0);
    ui_collect_apply_panel_title(s_guide_title_label, ui_text("样本采集", "Sample Capture"));

    ui_collect_create_progress_card(panel);
    ui_collect_create_angle_list(panel);

    lv_obj_t *btn = ui_widget_create_action_button(panel,
                                                   10,
                                                   286,
                                                   78,
                                                   ui_text(LV_SYMBOL_IMAGE " 拍照", LV_SYMBOL_IMAGE " Capture"),
                                                   UI_COLOR_OK,
                                                   UI_COLOR_OK);
    s_capture_button_label = lv_obj_get_child(btn, 0);
    ui_collect_apply_label_text(s_capture_button_label, ui_text(LV_SYMBOL_IMAGE " 拍照", LV_SYMBOL_IMAGE " Capture"));
    if (s_capture_button_label != NULL) {
        lv_obj_center(s_capture_button_label);
    }
    ui_collect_style_button(btn, 0x0D5F64, 0x26AFAA, 0x38D7C3, 0x07383B);
    lv_obj_add_event_cb(btn, ui_collect_capture_event_cb, LV_EVENT_CLICKED, NULL);

    s_auto_button = ui_widget_create_action_button(panel,
                                                    92,
                                                    286,
                                                    74,
                                                    ui_text(LV_SYMBOL_PLAY " 自动", LV_SYMBOL_PLAY " Auto"),
                                                    UI_COLOR_CARD,
                                                    UI_COLOR_BORDER);
    ui_collect_set_auto_button_running(0);
    lv_obj_add_event_cb(s_auto_button, ui_collect_auto_capture_event_cb, LV_EVENT_CLICKED, NULL);

    s_reset_button = ui_widget_create_action_button(panel,
                                                    170,
                                                    286,
                                                    74,
                                                    ui_text(LV_SYMBOL_REFRESH " 清空", LV_SYMBOL_REFRESH " Reset"),
                                                    UI_COLOR_CARD,
                                                    UI_COLOR_BORDER);
    ui_collect_apply_label_text(lv_obj_get_child(s_reset_button, 0),
                                ui_text(LV_SYMBOL_REFRESH " 清空", LV_SYMBOL_REFRESH " Reset"));
    {
        lv_obj_t *reset_label = lv_obj_get_child(s_reset_button, 0);
        if (reset_label != NULL) {
            lv_obj_center(reset_label);
        }
    }
    ui_collect_style_button(s_reset_button, 0x262839, 0x3E435C, 0x69718D, 0x181A27);
    lv_obj_add_event_cb(s_reset_button, ui_collect_reset_button_event_cb, LV_EVENT_CLICKED, NULL);
}

/* 创建顶部总进度卡片；总数来自样本分组数组。 */
static void ui_collect_create_progress_card(lv_obj_t *parent)
{
    char total_text[16];
    char current_text[40];
    uint16_t total_captured = ui_collect_get_total_captured();
    uint16_t total_target = ui_collect_get_total_target();
    const ui_collect_angle_step_t *current = ui_collect_get_current_step();
    int32_t percent = total_target == 0 ? 0 : (int32_t)((total_captured * 100U) / total_target);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 232, 86);
    lv_obj_set_pos(card, 14, 42);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x17232A), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x101315), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2D7180), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x06090B), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    s_progress_title_label = title;
    lv_label_set_text(title, ui_text("样本进度", "Sample Progress"));
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(title, ui_font_body(), 0);
    lv_obj_set_pos(title, 12, 10);

    ui_collect_create_hint_button(card);

    snprintf(total_text, sizeof(total_text), "%u / %u", total_captured, total_target);
    s_progress_value_label = lv_label_create(card);
    lv_label_set_text(s_progress_value_label, total_text);
    lv_obj_set_style_text_color(s_progress_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_progress_value_label, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(s_progress_value_label, 12, 26);

    ui_collect_format_current_text(current_text, sizeof(current_text), current);

    s_progress_current_label = lv_label_create(card);
    lv_label_set_text(s_progress_current_label, current_text);
    lv_obj_set_style_text_color(s_progress_current_label, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(s_progress_current_label, ui_font_body(), 0);
    lv_obj_set_pos(s_progress_current_label, 12, 58);

    ui_collect_create_progress_bar(card, 12, 72, 208, percent);
}

/* 创建总进度卡右上角的小提示按钮；用户可以随时重新打开采集说明。 */
static void ui_collect_create_hint_button(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 22, 22);
    lv_obj_set_pos(btn, 198, 8);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x20303A), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x31444F), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x5E7683), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_60, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 11, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x05080A), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, ui_collect_hint_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "?");
    lv_obj_set_style_text_color(label, lv_color_hex(0xD7F0FF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
}

/* 创建渐变进度条；绿色到蓝色的过渡用于表现样本覆盖进度。 */
static void ui_collect_create_progress_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    s_progress_bar = lv_bar_create(parent);
    lv_obj_set_size(s_progress_bar, width, 8);
    lv_obj_set_pos(s_progress_bar, x, y);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x2B333B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(UI_COLOR_OK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_progress_bar, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_progress_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_opa(s_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, 4, LV_PART_INDICATOR);
}

/* 创建样本分组列表；完成项只用细色条提示，避免全部完成后满屏绿色。 */
static void ui_collect_create_angle_list(lv_obj_t *parent)
{
    lv_obj_t *list;

    lv_obj_t *title = lv_label_create(parent);
    s_angle_list_title_label = title;
    lv_label_set_text(title, ui_text("样本分组", "Sample Groups"));
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title, 14, 142);

    list = lv_obj_create(parent);
    lv_obj_set_size(list, 232, 108);
    lv_obj_set_pos(list, 14, 164);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_grad_color(list, lv_color_hex(0x172029), 0);
    lv_obj_set_style_bg_grad_dir(list, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_70, 0);
    lv_obj_set_style_border_color(list, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        ui_collect_create_angle_row(list, 6 + (int32_t)i * 30, i);
    }
}

/* 创建单个样本分组行；保存行对象和文字对象指针，方便后续采集时刷新界面。 */
static void ui_collect_create_angle_row(lv_obj_t *parent, int32_t y, uint32_t index)
{
    char angle_text[32];
    char count_text[12];
    const ui_collect_angle_step_t *step = &s_angle_steps[index];

    s_angle_rows[index] = lv_obj_create(parent);
    lv_obj_set_size(s_angle_rows[index], 212, 26);
    lv_obj_set_pos(s_angle_rows[index], 6, y);
    lv_obj_set_style_bg_grad_dir(s_angle_rows[index], LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(s_angle_rows[index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_angle_rows[index], 1, 0);
    lv_obj_set_style_radius(s_angle_rows[index], UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_color(s_angle_rows[index], lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_pad_all(s_angle_rows[index], 0, 0);
    lv_obj_clear_flag(s_angle_rows[index], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_angle_rows[index], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_angle_rows[index],
                        ui_collect_angle_row_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);

    s_angle_strips[index] = lv_obj_create(s_angle_rows[index]);
    lv_obj_set_size(s_angle_strips[index], 4, 16);
    lv_obj_set_pos(s_angle_strips[index], 7, 5);
    lv_obj_set_style_border_width(s_angle_strips[index], 0, 0);
    lv_obj_set_style_radius(s_angle_strips[index], 2, 0);
    lv_obj_set_style_pad_all(s_angle_strips[index], 0, 0);
    lv_obj_clear_flag(s_angle_strips[index], LV_OBJ_FLAG_SCROLLABLE);

    ui_collect_format_group_text(angle_text, sizeof(angle_text), step);
    s_angle_labels[index] = lv_label_create(s_angle_rows[index]);
    lv_label_set_text(s_angle_labels[index], angle_text);
    lv_obj_set_style_text_font(s_angle_labels[index], ui_font_body(), 0);
    lv_obj_set_pos(s_angle_labels[index], 18, 6);

    snprintf(count_text, sizeof(count_text), "%u/%u", step->captured, step->target);
    s_angle_count_labels[index] = lv_label_create(s_angle_rows[index]);
    lv_label_set_text(s_angle_count_labels[index], count_text);
    lv_obj_set_style_text_font(s_angle_count_labels[index], &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_angle_count_labels[index], 166, 6);

    ui_collect_apply_angle_row_style(index);
}

/* 给采集页按钮增加轻量渐变和按下态，保持和检测页按钮一致的质感。 */
static void ui_collect_style_button(lv_obj_t *btn,
                                    uint32_t bg_color,
                                    uint32_t grad_color,
                                    uint32_t border_color,
                                    uint32_t pressed_color)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(grad_color), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x050809), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);

    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(bg_color), LV_STATE_PRESSED);
}

/* 根据自动采集是否运行，调整 Auto 按钮文字和颜色。 */
static void ui_collect_set_auto_button_running(uint8_t running)
{
    if (s_auto_button == NULL) {
        return;
    }

    if (running) {
        ui_collect_style_button(s_auto_button, 0x163D66, 0x1B7CB1, UI_COLOR_ACCENT, 0x0D2A45);
    } else {
        ui_collect_style_button(s_auto_button, 0x17242B, 0x2F3D47, 0x46545E, 0x10171C);
    }

    lv_obj_t *label = lv_obj_get_child(s_auto_button, 0);
    if (label != NULL) {
        ui_collect_apply_label_text(label,
                                    running
                                        ? ui_text(LV_SYMBOL_PAUSE " 停止", LV_SYMBOL_PAUSE " Stop")
                                        : ui_text(LV_SYMBOL_PLAY " 自动", LV_SYMBOL_PLAY " Auto"));
        lv_obj_center(label);
    }
}

/* 设置普通标签文字，并按照当前语言切换中文/英文所需字体。 */
static void ui_collect_apply_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
}

/* 设置面板标题文字；英文标题保留略大的 Montserrat，中文标题使用中文子集字库。 */
static void ui_collect_apply_panel_title(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14, 0);
}

/* 把样本分组格式化成当前语言的短文本。 */
static void ui_collect_format_group_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *step)
{
    const char *text;

    if (step == NULL) {
        snprintf(buf, buf_size, "--");
        return;
    }

    text = ui_language_is_chinese() ? step->zh_name : step->en_name;
    snprintf(buf, buf_size, "%s", text);
}

/* 把底部引导条文字格式化成当前语言，例如“当前分组: 左偏”。 */
static void ui_collect_format_guide_group_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *step)
{
    const char *name;
    const char *hint;

    if (step == NULL) {
        snprintf(buf, buf_size, "%s", ui_text("当前分组: --", "Current group: --"));
        return;
    }

    name = ui_language_is_chinese() ? step->zh_name : step->en_name;
    hint = ui_language_is_chinese() ? step->zh_hint : step->en_hint;
    snprintf(buf,
             buf_size,
             ui_language_is_chinese() ? "%s: %s" : "%s: %s",
             name,
             hint);
}

/* 格式化总进度卡片里的当前分组说明。 */
static void ui_collect_format_current_text(char *buf, uint32_t buf_size, const ui_collect_angle_step_t *current)
{
    if (current != NULL) {
        snprintf(buf,
                 buf_size,
                 ui_language_is_chinese() ? "当前: %s  %u/%u" : "Current: %s  %u/%u",
                 ui_language_is_chinese() ? current->zh_name : current->en_name,
                 current->captured,
                 current->target);
    } else {
        snprintf(buf, buf_size, "%s", ui_text("当前: 已完成", "Current: completed"));
    }
}

/* 提示按钮回调；强制重新打开采集说明弹窗。 */
static void ui_collect_hint_button_event_cb(lv_event_t *e)
{
    (void)e;

    ui_collect_show_entry_tip(1);
}

/* Reset 按钮回调；清空所有分组的样本数量并回到第一个分组。 */
static void ui_collect_reset_button_event_cb(lv_event_t *e)
{
    (void)e;

    ui_collect_reset_samples();
}

/* 统计当前分组数据中已经拍摄的总张数。 */
static uint16_t ui_collect_get_total_captured(void)
{
    uint16_t total = 0;

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        total += s_angle_steps[i].captured;
    }

    return total;
}

/* 统计当前分组数据中的目标总张数。 */
static uint16_t ui_collect_get_total_target(void)
{
    uint16_t total = 0;

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        total += s_angle_steps[i].target;
    }

    return total;
}

/* 查找当前正在采集的分组下标；如果全部完成则返回 -1。 */
/* Return 1 when all sample group targets have been captured successfully. */
static uint8_t ui_collect_is_all_completed(void)
{
    return ui_collect_get_total_captured() >= ui_collect_get_total_target() ? 1U : 0U;
}

static int32_t ui_collect_get_current_step_index(void)
{
    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        if (s_angle_steps[i].state == UI_COLLECT_STEP_CURRENT) {
            return (int32_t)i;
        }
    }

    return -1;
}

/* 获取当前正在采集的样本分组；如果全部完成则返回 NULL。 */
static ui_collect_angle_step_t *ui_collect_get_current_step(void)
{
    int32_t index = ui_collect_get_current_step_index();

    if (index < 0) {
        return NULL;
    }

    return &s_angle_steps[index];
}

/* 选择指定样本分组作为当前采集分组；允许用户跳过还没拍满的分组。 */
static void ui_collect_select_angle(uint32_t index)
{
    if (index >= UI_COLLECT_ANGLE_COUNT) {
        return;
    }

    ui_collect_stop_auto_capture();

    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        if (i == index) {
            s_angle_steps[i].state = UI_COLLECT_STEP_CURRENT;
        } else if (s_angle_steps[i].captured >= s_angle_steps[i].target) {
            s_angle_steps[i].state = UI_COLLECT_STEP_DONE;
        } else if (s_angle_steps[i].captured > 0) {
            s_angle_steps[i].state = UI_COLLECT_STEP_PARTIAL;
        } else {
            s_angle_steps[i].state = UI_COLLECT_STEP_PENDING;
        }
    }

    ui_collect_refresh_view();
    printf("Select collect group: %s\n", s_angle_steps[index].en_name);
}

/* 分组行点击回调；用户可以直接切换到想继续拍摄的分组。 */
static int ui_collect_save_current_sample(void)
{
    char saved_path[STORAGE_PATH_MAX];
    int ret;

    if (ui_collect_get_current_step() == NULL)
    {
        ui_collect_refresh_view();
        return -RT_ERROR;
    }

    if (s_collect_capture_busy != 0U)
    {
        printf("Collect capture busy\n");
        return -RT_EBUSY;
    }

    s_collect_capture_busy = 1U;
    ret = camera_app_collect_save_current_frame(saved_path,
                                                sizeof(saved_path),
                                                UI_COLLECT_CAPTURE_TIMEOUT_MS);
    s_collect_capture_busy = 0U;

    if (ret == RT_EOK)
    {
        printf("Collect saved: %s\n", saved_path);
        ui_collect_capture_one_sample();
    }
    else if (ret == -RT_EBUSY)
    {
        printf("Collect save skipped: camera busy\n");
    }
    else
    {
        printf("Collect save failed: %s\n", storage_app_last_error());
    }

    return ret;
}

static void ui_collect_angle_row_event_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    ui_collect_select_angle(index);
}

/* 拍照按钮回调；触发相机线程保存下一帧到当前采集 session。 */
static void ui_collect_capture_event_cb(lv_event_t *e)
{
    (void)e;

    (void)ui_collect_save_current_sample();
}

/* 自动采集按钮回调；按一次启动 timer，再按一次停止 timer。 */
static void ui_collect_auto_capture_event_cb(lv_event_t *e)
{
    (void)e;

    ui_collect_toggle_auto_capture();
}

/* 自动采集 timer 回调；每触发一次就尝试保存一张当前预览帧。 */
static void ui_collect_auto_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if ((s_auto_capture_step_index < 0) ||
        (s_auto_capture_step_index >= (int32_t)UI_COLLECT_ANGLE_COUNT))
    {
        printf("Auto capture stopped: invalid group\n");
        ui_collect_stop_auto_capture();
        return;
    }

    if (s_angle_steps[s_auto_capture_step_index].captured >=
        s_angle_steps[s_auto_capture_step_index].target)
    {
        printf("Auto capture group completed: %s\n",
               s_angle_steps[s_auto_capture_step_index].en_name);
        ui_collect_stop_auto_capture();
        return;
    }

    /*
     * 真实硬件接入时不要在 timer 里无条件 captured++。
     * 推荐流程：
     * 1. timer 到期后检查当前是否已有拍照/写 SD 卡任务在执行。
     * 2. 如果空闲，触发相机线程把下一帧 RGB565 预览图保存为 BMP。
     * 3. 只有拍照成功且文件写入成功后，再调用 ui_collect_capture_one_sample()。
     * 4. 如果一次拍照+存储超过 timer 周期，应跳过本次 timer 或把周期调大，避免任务堆积。
     */
    (void)ui_collect_save_current_sample();

    if (s_angle_steps[s_auto_capture_step_index].captured >=
        s_angle_steps[s_auto_capture_step_index].target)
    {
        printf("Auto capture group completed: %s\n",
               s_angle_steps[s_auto_capture_step_index].en_name);
        ui_collect_stop_auto_capture();
    }
}

/* 提示弹窗按钮回调；点击 OK 或关闭按钮后销毁自定义弹层。 */
static void ui_collect_tip_close_event_cb(lv_event_t *e)
{
    lv_obj_t *overlay;

    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        s_collect_tip_overlay = NULL;
        camera_app_set_lcd_preview_suspended(0U);
        camera_app_set_lcd_preview_visible(1U);
        return;
    }

    overlay = (lv_obj_t *)lv_event_get_user_data(e);

    if (overlay != NULL) {
        lv_obj_delete(overlay);
    }
}

/* 显示采集提示弹窗；使用自定义轻量弹层，避免默认 msgbox 看起来过重。 */
static void ui_collect_show_entry_tip(uint8_t force)
{
    if (s_collect_tip_shown && !force) {
        return;
    }

    s_collect_tip_shown = 1;
    /*
     * The camera preview is a hardware GLCDC layer above LVGL. A modal LVGL
     * tip must hide that layer; suspending only freezes it and still covers
     * lv_layer_top().
     */
    camera_app_set_lcd_preview_suspended(0U);
    camera_app_set_lcd_preview_visible(0U);

    if (s_collect_tip_overlay != NULL && lv_obj_is_valid(s_collect_tip_overlay)) {
        lv_obj_move_foreground(s_collect_tip_overlay);
        return;
    }

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    s_collect_tip_overlay = overlay;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x030608), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, ui_collect_tip_close_event_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 344, 220);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x10181D), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x192931), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x36545E), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x010304), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_set_size(accent, 4, 46);
    lv_obj_set_pos(accent, 18, 22);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x28B7B0), 0);
    lv_obj_set_style_bg_grad_color(accent, lv_color_hex(0x5FD49C), 0);
    lv_obj_set_style_bg_grad_dir(accent, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ui_text("采集提示", "Capture Guide"));
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title, 32, 22);

    lv_obj_t *subtitle = lv_label_create(card);
    lv_label_set_text(subtitle, ui_text("采集正常样本的小范围波动", "Capture small normal variations."));
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9FB2BC), 0);
    lv_obj_set_style_text_font(subtitle, ui_font_body(), 0);
    lv_obj_set_pos(subtitle, 32, 48);

    /*
     * 正文单独放进一个“小屏幕”容器。
     * 后续如果提示内容变多，只需要继续追加 label 文本，容器会自动启用纵向滚动。
     */
    lv_obj_t *screen = lv_obj_create(card);
    lv_obj_set_size(screen, 286, 92);
    lv_obj_set_pos(screen, 32, 78);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0A1115), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x101A1F), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(screen, lv_color_hex(0x263E47), 0);
    lv_obj_set_style_border_opa(screen, LV_OPA_80, 0);
    lv_obj_set_style_border_width(screen, 1, 0);
    lv_obj_set_style_radius(screen, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_left(screen, 10, 0);
    lv_obj_set_style_pad_right(screen, 8, 0);
    lv_obj_set_style_pad_top(screen, 8, 0);
    lv_obj_set_style_pad_bottom(screen, 8, 0);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *text = lv_label_create(screen);
    lv_obj_set_width(text, 250);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(text,
                      ui_text(LV_SYMBOL_OK " 样本只采正常品，保持真实运行光照。\n"
                              LV_SYMBOL_OK " 不要每张完全不动，要覆盖轻微偏移。\n"
                              LV_SYMBOL_OK " 每个分组拍够数量后再切换。",
                              LV_SYMBOL_OK " Capture only OK samples under runtime lighting.\n"
                              LV_SYMBOL_OK " Do not keep every frame identical; include small pose shifts.\n"
                              LV_SYMBOL_OK " Finish the highlighted group before switching."));
    lv_obj_set_style_text_color(text, lv_color_hex(0xC4D2D8), 0);
    lv_obj_set_style_text_line_space(text, 5, 0);
    lv_obj_set_style_text_font(text, ui_font_body(), 0);
    lv_obj_set_pos(text, 0, 0);

    lv_obj_t *close_btn = lv_button_create(card);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_set_pos(close_btn, 304, 16);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x1E2B32), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(0x3B525C), 0);
    lv_obj_set_style_border_opa(close_btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 12, 0);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, ui_collect_tip_close_event_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xD4E4EA), 0);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_12, 0);
    lv_obj_center(close_label);

    lv_obj_t *ok_btn = lv_button_create(card);
    lv_obj_set_size(ok_btn, ui_language_is_chinese() ? 82 : 92, 30);
    lv_obj_set_pos(ok_btn, 226, 176);
    ui_collect_style_button(ok_btn, 0x0D5F64, 0x26AFAA, 0x38D7C3, 0x07383B);
    lv_obj_add_event_cb(ok_btn, ui_collect_tip_close_event_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t *ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, ui_text(LV_SYMBOL_OK " 知道了", LV_SYMBOL_OK " Got it"));
    lv_obj_set_style_text_color(ok_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(ok_label, ui_font_body(), 0);
    lv_obj_center(ok_label);
}

/* 根据样本分组状态计算行样式；刷新和创建时复用同一套颜色规则。 */
static ui_collect_step_style_t ui_collect_get_step_style(ui_collect_step_state_t state)
{
    ui_collect_step_style_t style = {
        0x1D2328,
        0x171C20,
        UI_COLOR_BORDER,
        UI_COLOR_TEXT_SUB,
        0x56616B,
        LV_OPA_50,
        LV_OPA_60,
        0,
        LV_OPA_TRANSP
    };

    if (state == UI_COLLECT_STEP_DONE) {
        style.bg_color = 0x20272C;
        style.grad_color = 0x171C20;
        style.border_color = 0x40525B;
        style.text_color = 0xAEBCC3;
        style.strip_color = 0x68D39A;
        style.border_opa = LV_OPA_80;
        style.strip_opa = LV_OPA_COVER;
    } else if (state == UI_COLLECT_STEP_PARTIAL) {
        style.bg_color = 0x232321;
        style.grad_color = 0x332D27;
        style.border_color = 0x6A5843;
        style.text_color = 0xD6CDC2;
        style.strip_color = 0xB8945A;
        style.border_opa = LV_OPA_60;
        style.strip_opa = LV_OPA_70;
    } else if (state == UI_COLLECT_STEP_CURRENT) {
        style.bg_color = 0x17314A;
        style.grad_color = 0x10222F;
        style.border_color = UI_COLOR_ACCENT;
        style.text_color = UI_COLOR_TEXT;
        style.strip_color = UI_COLOR_ACCENT;
        style.border_opa = LV_OPA_80;
        style.strip_opa = LV_OPA_COVER;
        style.shadow_width = 8;
        style.shadow_opa = LV_OPA_20;
    }

    return style;
}

/* 把某个样本分组的数据和状态应用到对应的 LVGL 行对象上。 */
static void ui_collect_apply_angle_row_style(uint32_t index)
{
    char count_text[12];
    const ui_collect_angle_step_t *step;
    ui_collect_step_style_t style;

    if (index >= UI_COLLECT_ANGLE_COUNT || s_angle_rows[index] == NULL) {
        return;
    }

    step = &s_angle_steps[index];
    style = ui_collect_get_step_style(step->state);

    /*
     * 只要某个分组已经拍过但还没有拍满，就使用黄色“未采满”主体色。
     * 如果它同时也是当前选中的分组，则保留蓝色边框和阴影，方便用户判断 Capture 会加到哪一行。
     */
    if (step->captured > 0 && step->captured < step->target) {
        style.bg_color = 0x232321;
        style.grad_color = 0x332D27;
        style.text_color = 0xD6CDC2;
        style.strip_color = 0xB8945A;
        style.strip_opa = LV_OPA_70;

        if (step->state == UI_COLLECT_STEP_CURRENT) {
            style.border_color = UI_COLOR_ACCENT;
            style.border_opa = LV_OPA_70;
            style.shadow_width = 6;
            style.shadow_opa = LV_OPA_10;
        } else {
            style.border_color = 0x6A5843;
            style.border_opa = LV_OPA_60;
        }
    }

    lv_obj_set_style_bg_color(s_angle_rows[index], lv_color_hex(style.bg_color), 0);
    lv_obj_set_style_bg_grad_color(s_angle_rows[index], lv_color_hex(style.grad_color), 0);
    lv_obj_set_style_bg_opa(s_angle_rows[index], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_angle_rows[index], lv_color_hex(style.border_color), 0);
    lv_obj_set_style_border_opa(s_angle_rows[index], style.border_opa, 0);
    lv_obj_set_style_shadow_width(s_angle_rows[index], style.shadow_width, 0);
    lv_obj_set_style_shadow_opa(s_angle_rows[index], style.shadow_opa, 0);

    if (s_angle_strips[index] != NULL) {
        lv_obj_set_style_bg_color(s_angle_strips[index], lv_color_hex(style.strip_color), 0);
        lv_obj_set_style_bg_opa(s_angle_strips[index], style.strip_opa, 0);
    }

    if (s_angle_labels[index] != NULL) {
        char angle_text[32];
        ui_collect_format_group_text(angle_text, sizeof(angle_text), step);
        lv_label_set_text(s_angle_labels[index], angle_text);
        lv_obj_set_style_text_font(s_angle_labels[index], ui_font_body(), 0);
        lv_obj_set_style_text_color(s_angle_labels[index], lv_color_hex(style.text_color), 0);
    }

    if (s_angle_count_labels[index] != NULL) {
        snprintf(count_text, sizeof(count_text), "%u/%u", step->captured, step->target);
        lv_label_set_text(s_angle_count_labels[index], count_text);
        lv_obj_set_style_text_color(s_angle_count_labels[index], lv_color_hex(style.text_color), 0);
    }
}

/* 刷新画面外的样本分组提示；相机预览内不放文字，避免遮挡真实图像。 */
static void ui_collect_refresh_placement_guide(void)
{
    char angle_text[96];
    const ui_collect_angle_step_t *current = ui_collect_get_current_step();

    if (s_placement_guide.angle_label != NULL) {
        ui_collect_format_guide_group_text(angle_text, sizeof(angle_text), current);
        lv_label_set_text(s_placement_guide.angle_label, angle_text);
        lv_obj_set_style_text_font(s_placement_guide.angle_label, ui_font_body(), 0);
    }

    if (s_placement_guide.angle_icon != NULL) {
        int16_t rotation = (current != NULL) ? current->guide_rotation_deg : 0;
        lv_obj_set_style_transform_rotation(s_placement_guide.angle_icon, (int32_t)rotation * 10, 0);
    }

    if (s_placement_guide.angle_value_label != NULL) {
        ui_collect_format_group_text(angle_text, sizeof(angle_text), current);
        lv_label_set_text(s_placement_guide.angle_value_label, angle_text);
        lv_obj_set_style_text_font(s_placement_guide.angle_value_label, ui_font_body(), 0);
    }
}

/* 刷新顶部总进度数字、当前分组文字和进度条。 */
static void ui_collect_refresh_progress(void)
{
    char total_text[16];
    char current_text[40];
    uint16_t total_captured = ui_collect_get_total_captured();
    uint16_t total_target = ui_collect_get_total_target();
    const ui_collect_angle_step_t *current = ui_collect_get_current_step();
    int32_t percent = total_target == 0 ? 0 : (int32_t)((total_captured * 100U) / total_target);

    snprintf(total_text, sizeof(total_text), "%u / %u", total_captured, total_target);

    if (s_progress_value_label != NULL) {
        lv_label_set_text(s_progress_value_label, total_text);
    }

    ui_collect_format_current_text(current_text, sizeof(current_text), current);

    if (s_progress_current_label != NULL) {
        lv_label_set_text(s_progress_current_label, current_text);
        lv_obj_set_style_text_font(s_progress_current_label, ui_font_body(), 0);
    }

    if (s_progress_bar != NULL) {
        lv_bar_set_value(s_progress_bar, percent, LV_ANIM_ON);
    }
}

/* 刷新所有分组行；采集数量和当前分组切换都会在这里反映到界面上。 */
static void ui_collect_refresh_angle_rows(void)
{
    for (uint32_t i = 0; i < UI_COLLECT_ANGLE_COUNT; i++) {
        ui_collect_apply_angle_row_style(i);
    }
}

/* 刷新采集页右侧所有会变化的数据；按钮回调和 timer 回调都会调用它。 */
static void ui_collect_refresh_view(void)
{
    ui_collect_refresh_progress();
    ui_collect_refresh_angle_rows();
    ui_collect_refresh_placement_guide();
}

#include "ui.h"
#include "ui_config.h"
#include "ui_data.h"
#include "ui_language.h"
#include "ui_settings_data.h"
#include "ui_widgets.h"
#include "pages/ui_collect.h"
#include "pages/ui_data_page.h"
#include "pages/ui_detect.h"
#include "pages/ui_settings.h"
#include "camera_app.h"
#include "camera_tcp_service.h"
#include "detect_app.h"
#include "storage_app.h"
#include "device_comm_app.h"
#include "model_app.h"
#include "sort_demo_app.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * ui.c 只负责 UI 外壳：
 * 1. 顶部状态栏
 * 2. 中间页面容器
 * 3. 底部导航栏
 * 4. 页面切换
 *
 * 具体页面内容不要继续堆在这里。Detect 页面已经拆到 pages/ui_detect.c。
 */

typedef enum {
    UI_PAGE_DETECT = 0,
    UI_PAGE_COLLECT,
    UI_PAGE_DATA,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT
} ui_page_t;

#define UI_DETECT_CAMERA_X  (UI_CONTENT_X + 10 + 86)
#define UI_DETECT_CAMERA_Y  (UI_CONTENT_Y + 10 + 52)
#define UI_COLLECT_CAMERA_X (UI_CONTENT_X + 10 + 72)
#define UI_COLLECT_CAMERA_Y (UI_CONTENT_Y + 10 + 52)
#define UI_DETECT_CAMERA_FPS 15U
#define UI_COLLECT_CAMERA_FPS 30U
#define UI_NET_ACTIVE_MS 5000U

static const char *s_page_titles[UI_PAGE_COUNT] = {
    "Detect",
    "Collect",
    "Data",
    "Settings"
};

static const char *s_page_hints[UI_PAGE_COUNT] = {
    "Detect page is implemented in pages/ui_detect.c",
    "Empty collect page placeholder",
    "Empty data page placeholder",
    "Empty settings page placeholder"
};

static lv_obj_t *s_nav_buttons[UI_PAGE_COUNT];
static lv_obj_t *s_nav_labels[UI_PAGE_COUNT];
static lv_obj_t *s_detect_page = NULL;
static lv_obj_t *s_collect_page = NULL;
static lv_obj_t *s_data_page = NULL;
static lv_obj_t *s_settings_page = NULL;
static lv_obj_t *s_empty_page = NULL;
static lv_obj_t *s_empty_title = NULL;
static lv_obj_t *s_empty_hint = NULL;
static lv_obj_t *s_top_title = NULL;
static lv_obj_t *s_run_pill = NULL;
static lv_obj_t *s_run_label = NULL;
static lv_obj_t *s_storage_pill = NULL;
static lv_obj_t *s_storage_label = NULL;
static lv_obj_t *s_modbus_pill = NULL;
static lv_obj_t *s_modbus_label = NULL;
static lv_obj_t *s_net_pill = NULL;
static lv_obj_t *s_net_label = NULL;
static lv_obj_t *s_model_pill = NULL;
static lv_obj_t *s_model_label = NULL;
static volatile uint8_t s_storage_status_dirty = 0U;
static volatile uint8_t s_storage_config_dirty = 0U;
static volatile uint8_t s_detect_result_dirty = 0U;
static uint32_t s_modbus_refresh_divider = 0U;
static ui_page_t s_current_page = UI_PAGE_DETECT;

static void ui_create_top_bar(lv_obj_t *scr);
static void ui_create_brand_mark(lv_obj_t *parent);
static void ui_create_content_area(lv_obj_t *scr);
static void ui_create_empty_page(lv_obj_t *parent);
static void ui_create_bottom_nav(lv_obj_t *scr);
static lv_obj_t *ui_create_nav_button(lv_obj_t *parent,
                                      const char *text,
                                      int32_t x,
                                      ui_page_t page);
static void ui_switch_page(ui_page_t page);
static void ui_nav_event_cb(lv_event_t *e);
static const char *ui_page_nav_text(ui_page_t page);
static void ui_refresh_language(void);
static void ui_language_changed_handler(ui_language_t language, void *user_data);
static void ui_update_run_status(ui_detect_run_state_t state);
static void ui_update_storage_status(void);
static void ui_update_modbus_status(void);
static void ui_update_net_status(void);
static void ui_update_model_status(void);
static void ui_storage_changed_handler(void *user_data);
static void ui_detect_result_published_handler(void *user_data);
static void ui_detect_command_handler(ui_detect_command_t cmd, void *user_data);
static void ui_data_command_handler(ui_data_command_t cmd, void *user_data);
static void ui_settings_command_handler(ui_settings_command_t cmd, uint16_t value, void *user_data);
static void ui_apply_camera_overlay_state(ui_page_t page);
static void ui_sync_detect_camera_overlay(void);

/* 初始化整个 UI 外壳，并创建顶栏、页面容器和底栏。 */
void ui_init(void)
{
    /*
     * UI 总入口。
     * main.c 只调用这个函数；页面文件不要直接被 main.c 调用。
     */
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    detect_app_init();

    ui_create_top_bar(scr);         // 顶部状态栏
    ui_language_set_changed_callback(ui_language_changed_handler, NULL);
    ui_detect_set_result_changed_callback(ui_sync_detect_camera_overlay);
    ui_data_set_detect_command_handler(ui_detect_command_handler, NULL);
    ui_data_set_data_command_handler(ui_data_command_handler, NULL);
    ui_settings_data_set_command_handler(ui_settings_command_handler, NULL);
    storage_app_set_state_changed_callback(ui_storage_changed_handler, NULL);
    detect_app_set_result_published_callback(ui_detect_result_published_handler, NULL);
    ui_update_run_status(ui_data_get_detect_snapshot()->run_state);
    ui_update_storage_status();
    ui_update_net_status();
    ui_update_model_status();
    (void)device_comm_app_init();
    ui_sync_detect_camera_overlay();

    ui_create_content_area(scr);    // 中间页面容器
    ui_create_bottom_nav(scr);      // 底部导航栏

    ui_switch_page(UI_PAGE_DETECT); // 初始为检测页面

    /*
     * Start the camera preview worker after the initial page has configured
     * the GLCDC layer window. Later page switches only change visibility,
     * position, FPS and overlay mode, so users no longer need to run
     * camera_preview_lcd_start from msh after every power-on.
     */
    camera_app_start_lcd_preview();
}

/* 创建顶部状态栏，包括标题、运行状态、相机、模型和时间。 */
static void ui_create_top_bar(lv_obj_t *scr)
{
    /*
     * 顶部状态栏是全局区域。
     * 后续真实系统里，RUN/相机/模型/时间这些文本会由状态模块刷新。
     */
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, UI_SCREEN_W, UI_TOP_BAR_H);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(UI_COLOR_TOP), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_brand_mark(top_bar);

    s_top_title = lv_label_create(top_bar);
    lv_label_set_text(s_top_title, ui_text("AI 视觉终端", "AI Vision Terminal"));
    lv_obj_set_style_text_color(s_top_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_top_title, ui_font_title(), 0);
    lv_obj_set_pos(s_top_title, 52, 10);

    s_run_pill = ui_widget_create_status_pill(top_bar, LV_SYMBOL_BULLET " RUN", 310, 58, 0x06351F, UI_COLOR_OK);
    s_run_label = lv_obj_get_child(s_run_pill, 0);
    ui_widget_create_status_pill(top_bar, "RA8D1", 376, 58, UI_COLOR_CARD, UI_COLOR_BORDER);
    s_storage_pill = ui_widget_create_status_pill(top_bar, "SD --", 442, 72, UI_COLOR_CARD, UI_COLOR_BORDER);
    s_storage_label = lv_obj_get_child(s_storage_pill, 0);
    s_modbus_pill = ui_widget_create_status_pill(top_bar, "MB --", 522, 66, UI_COLOR_CARD, UI_COLOR_BORDER);
    s_modbus_label = lv_obj_get_child(s_modbus_pill, 0);
    s_net_pill = ui_widget_create_status_pill(top_bar, "NET --", 596, 78, UI_COLOR_CARD, UI_COLOR_BORDER);
    s_net_label = lv_obj_get_child(s_net_pill, 0);
    s_model_pill = ui_widget_create_status_pill(top_bar, "MDL --", 682, 100, 0x3A2D12, UI_COLOR_WARN);
    s_model_label = lv_obj_get_child(s_model_pill, 0);
    ui_update_modbus_status();
    ui_update_net_status();
    ui_update_model_status();
}

/* 创建左上角品牌标识，使用轻量渐变和内侧高光，让顶栏更像产品级界面。 */
static void ui_create_brand_mark(lv_obj_t *parent)
{
    lv_obj_t *mark = lv_obj_create(parent);
    lv_obj_set_size(mark, 22, 22);
    lv_obj_set_pos(mark, 18, 11);
    lv_obj_set_style_bg_color(mark, lv_color_hex(0x28D17C), 0);
    lv_obj_set_style_bg_grad_color(mark, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_grad_dir(mark, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mark, lv_color_hex(0x7DF6D6), 0);
    lv_obj_set_style_border_opa(mark, LV_OPA_50, 0);
    lv_obj_set_style_border_width(mark, 1, 0);
    lv_obj_set_style_radius(mark, 5, 0);
    lv_obj_set_style_shadow_width(mark, 8, 0);
    lv_obj_set_style_shadow_color(mark, lv_color_hex(0x1FBBD0), 0);
    lv_obj_set_style_shadow_opa(mark, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(mark, 0, 0);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *shine = lv_obj_create(mark);
    lv_obj_set_size(shine, 5, 14);
    lv_obj_set_pos(shine, 3, 4);
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(shine, LV_OPA_30, 0);
    lv_obj_set_style_border_width(shine, 0, 0);
    lv_obj_set_style_radius(shine, 3, 0);
    lv_obj_set_style_pad_all(shine, 0, 0);
    lv_obj_clear_flag(shine, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建中间页面容器，并挂载 Detect/Collect 页面和其它空页面占位。 */
static void ui_create_content_area(lv_obj_t *scr)
{
    /*
     * 中间内容区是页面容器。
     * Detect 和 Collect 已经拆成独立页面；Data/Settings 暂时仍然共用空占位页。
     */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, UI_CONTENT_W, UI_CONTENT_H);
    lv_obj_set_pos(content, UI_CONTENT_X, UI_CONTENT_Y);
    lv_obj_set_style_bg_color(content, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_radius(content, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_detect_page = ui_detect_create(content);
    s_collect_page = ui_collect_create(content);
    s_data_page = ui_data_page_create(content);
    s_settings_page = ui_settings_create(content);
    ui_create_empty_page(content);
}

/* 创建 Data/Settings 暂时使用的空页面。 */
static void ui_create_empty_page(lv_obj_t *parent)
{
    /*
     * Data/Settings 先保留空页面。
     * 等 Collect 页面确认后，再继续逐个拆成独立页面文件。
     */
    s_empty_page = lv_obj_create(parent);
    lv_obj_set_size(s_empty_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_empty_page, 0, 0);
    lv_obj_set_style_bg_opa(s_empty_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_empty_page, 0, 0);
    lv_obj_set_style_pad_all(s_empty_page, 0, 0);
    lv_obj_clear_flag(s_empty_page, LV_OBJ_FLAG_SCROLLABLE);

    s_empty_title = lv_label_create(s_empty_page);
    lv_label_set_text(s_empty_title, "Collect");
    lv_obj_set_style_text_color(s_empty_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_empty_title, &lv_font_montserrat_24, 0);
    lv_obj_align(s_empty_title, LV_ALIGN_CENTER, 0, -16);

    s_empty_hint = lv_label_create(s_empty_page);
    lv_label_set_text(s_empty_hint, "Empty page placeholder");
    lv_obj_set_style_text_color(s_empty_hint, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(s_empty_hint, &lv_font_montserrat_16, 0);
    lv_obj_align(s_empty_hint, LV_ALIGN_CENTER, 0, 24);
}

/* 创建底部导航栏，并放置四个页面入口按钮。 */
static void ui_create_bottom_nav(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, UI_SCREEN_W, UI_BOTTOM_BAR_H);
    lv_obj_set_pos(bar, 0, UI_BOTTOM_BAR_Y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_TOP), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_nav_button(bar, ui_page_nav_text(UI_PAGE_DETECT), 10, UI_PAGE_DETECT);
    ui_create_nav_button(bar, ui_page_nav_text(UI_PAGE_COLLECT), 206, UI_PAGE_COLLECT);
    ui_create_nav_button(bar, ui_page_nav_text(UI_PAGE_DATA), 402, UI_PAGE_DATA);
    ui_create_nav_button(bar, ui_page_nav_text(UI_PAGE_SETTINGS), 598, UI_PAGE_SETTINGS);
}

/* 创建底部导航按钮，并把目标页面编号绑定到点击事件。 */
static lv_obj_t *ui_create_nav_button(lv_obj_t *parent,
                                      const char *text,
                                      int32_t x,
                                      ui_page_t page)
{
    /*
     * 底部导航按钮属于外壳逻辑，所以仍放在 ui.c。
     * 页面编号通过 user_data 传给事件回调。
     */
    bool active = (page == s_current_page);
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 178, 38);
    lv_obj_set_pos(btn, x, 7);
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? UI_COLOR_CARD : UI_COLOR_TOP), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? UI_COLOR_BORDER : UI_COLOR_TOP), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, ui_nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)page);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? UI_COLOR_TEXT : UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(label, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14, 0);
    lv_obj_center(label);

    s_nav_buttons[page] = btn;
    s_nav_labels[page] = label;

    return btn;
}

/* 切换当前显示页面，同时更新底部导航按钮的选中态。 */
static void ui_switch_page(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) {
        return;
    }

    if (s_storage_status_dirty != 0U) {
        s_storage_status_dirty = 0U;
        ui_update_storage_status();
    }

    s_current_page = page;
    if (page != UI_PAGE_DETECT) {
        sort_flow_auto_stop();
        detect_app_handle_command_from(UI_DETECT_CMD_STOP, "ui");
        ui_update_run_status(ui_data_get_detect_snapshot()->run_state);
    }
    ui_apply_camera_overlay_state(page);

    if (s_detect_page)
    {
        if (page == UI_PAGE_DETECT) {
            lv_obj_clear_flag(s_detect_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_detect_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_collect_page)
    {
        if (page == UI_PAGE_COLLECT) {
            lv_obj_clear_flag(s_collect_page, LV_OBJ_FLAG_HIDDEN);
            ui_collect_on_show();
        } else {
            lv_obj_add_flag(s_collect_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_data_page)
    {
        if (page == UI_PAGE_DATA) {
            ui_data_page_refresh();
            lv_obj_clear_flag(s_data_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_data_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_settings_page)
    {
        if (page == UI_PAGE_SETTINGS) {
            ui_settings_on_show();
            lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_empty_page)
    {
        if ((page == UI_PAGE_DETECT) || (page == UI_PAGE_COLLECT) || (page == UI_PAGE_DATA) || (page == UI_PAGE_SETTINGS)) {
            lv_obj_add_flag(s_empty_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_empty_page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if ((page != UI_PAGE_DETECT) && (page != UI_PAGE_COLLECT) && (page != UI_PAGE_DATA) && (page != UI_PAGE_SETTINGS) && s_empty_title)
    {
        lv_label_set_text(s_empty_title, s_page_titles[page]);
    }

    if ((page != UI_PAGE_DETECT) && (page != UI_PAGE_COLLECT) && (page != UI_PAGE_DATA) && (page != UI_PAGE_SETTINGS) && s_empty_hint)
    {
        lv_label_set_text(s_empty_hint, s_page_hints[page]);
    }

    for (uint32_t i = 0; i < UI_PAGE_COUNT; i++)
    {
        bool active = (i == (uint32_t)page);

        if (s_nav_buttons[i]) {
            lv_obj_set_style_bg_color(s_nav_buttons[i], lv_color_hex(active ? UI_COLOR_CARD : UI_COLOR_TOP), 0);
            lv_obj_set_style_border_color(s_nav_buttons[i], lv_color_hex(active ? UI_COLOR_BORDER : UI_COLOR_TOP), 0);
        }

        if (s_nav_labels[i]) {
            lv_obj_set_style_text_color(s_nav_labels[i], lv_color_hex(active ? UI_COLOR_TEXT : UI_COLOR_TEXT_SUB), 0);
        }
    }
}

/* 底部导航按钮点击回调：取出页面编号并切换页面。 */
static void ui_nav_event_cb(lv_event_t *e)
{
    ui_page_t page = (ui_page_t)(uintptr_t)lv_event_get_user_data(e);
    ui_switch_page(page);
}

/* 返回底部导航按钮在当前语言下应该显示的文本。 */
static const char *ui_page_nav_text(ui_page_t page)
{
    switch (page) {
        case UI_PAGE_DETECT:
            return ui_text(LV_SYMBOL_IMAGE "  检测", LV_SYMBOL_IMAGE "  Detect");
        case UI_PAGE_COLLECT:
            return ui_text(LV_SYMBOL_EYE_OPEN "  采集", LV_SYMBOL_EYE_OPEN "  Collect");
        case UI_PAGE_DATA:
            return ui_text(LV_SYMBOL_LIST "  数据", LV_SYMBOL_LIST "  Data");
        case UI_PAGE_SETTINGS:
            return ui_text(LV_SYMBOL_SETTINGS "  设置", LV_SYMBOL_SETTINGS "  Settings");
        default:
            return "";
    }
}

/* 刷新 UI 外壳里的静态文字；页面主体会后续逐页接入全局语言模块。 */
static void ui_refresh_language(void)
{
    if (s_top_title) {
        lv_label_set_text(s_top_title, ui_text("AI 视觉终端", "AI Vision Terminal"));
        lv_obj_set_style_text_font(s_top_title, ui_font_title(), 0);
    }

    for (uint32_t i = 0; i < UI_PAGE_COUNT; i++) {
        if (s_nav_labels[i]) {
            lv_label_set_text(s_nav_labels[i], ui_page_nav_text((ui_page_t)i));
            lv_obj_set_style_text_font(s_nav_labels[i],
                                       ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14,
                                       0);
            lv_obj_center(s_nav_labels[i]);
        }
    }

    ui_detect_refresh_language();
    ui_collect_refresh_language();
    ui_data_page_refresh_language();
    ui_update_storage_status();
    ui_update_net_status();
    ui_update_model_status();
}

/* 全局语言变化回调：设置页切换语言后，ui_language.c 会通知这里刷新外壳。 */
static void ui_language_changed_handler(ui_language_t language, void *user_data)
{
    (void)language;
    (void)user_data;
    ui_refresh_language();
}

/* 根据检测运行状态更新顶栏的 RUN/STOP 胶囊。 */
static void ui_update_run_status(ui_detect_run_state_t state)
{
    bool running = (state == UI_DETECT_RUN_RUNNING);

    if (s_run_label) {
        lv_label_set_text(s_run_label, running ? LV_SYMBOL_BULLET " RUN" : LV_SYMBOL_BULLET " STOP");
    }

    if (s_run_pill) {
        lv_obj_set_style_bg_color(s_run_pill, lv_color_hex(running ? 0x06351F : 0x3A1D22), 0);
        lv_obj_set_style_border_color(s_run_pill, lv_color_hex(running ? UI_COLOR_OK : UI_COLOR_STOP), 0);
    }
}

/* Detect 页面发出 Start/Stop/Refresh 命令后，用这个回调同步顶栏状态。 */
static void ui_update_storage_status(void)
{
    const ui_storage_info_t *storage = ui_data_get_storage_info();
    const char *text = "SD ERR";
    uint32_t bg_color = 0x3A1D22;
    uint32_t border_color = UI_COLOR_NG;

    if (storage != NULL) {
        if (storage->state == UI_STORAGE_OK) {
            text = "SD OK";
            bg_color = 0x102B3A;
            border_color = UI_COLOR_ACCENT;
        } else if (storage->state == UI_STORAGE_MISSING) {
            text = "SD MISS";
            bg_color = 0x3A2D12;
            border_color = UI_COLOR_WARN;
        } else if (storage->state == UI_STORAGE_FULL) {
            text = "SD FULL";
            bg_color = 0x3A2D12;
            border_color = UI_COLOR_WARN;
        }
    }

    if (s_storage_label != NULL) {
        lv_label_set_text(s_storage_label, text);
        lv_obj_center(s_storage_label);
    }

    if (s_storage_pill != NULL) {
        lv_obj_set_style_bg_color(s_storage_pill, lv_color_hex(bg_color), 0);
        lv_obj_set_style_border_color(s_storage_pill, lv_color_hex(border_color), 0);
    }
}

static void ui_storage_changed_handler(void *user_data)
{
    (void)user_data;
    s_storage_status_dirty = 1U;
    s_storage_config_dirty = 1U;
}

static void ui_update_modbus_status(void)
{
    device_comm_snapshot_t snapshot;
    const char *text = "MB --";
    uint32_t bg_color = UI_COLOR_CARD;
    uint32_t border_color = UI_COLOR_BORDER;

    if (device_comm_app_get_snapshot(&snapshot) == RT_EOK) {
        if ((snapshot.arm_online != 0U) && (snapshot.conveyor_online != 0U)) {
            text = "MB OK";
            bg_color = 0x06351F;
            border_color = UI_COLOR_OK;
        } else if (snapshot.arm_online != 0U) {
            text = "MB ARM";
            bg_color = 0x102B3A;
            border_color = UI_COLOR_ACCENT;
        } else if (snapshot.conveyor_online != 0U) {
            text = "MB MOT";
            bg_color = 0x102B3A;
            border_color = UI_COLOR_ACCENT;
        } else if (snapshot.bus_open != 0U) {
            text = "MB WAIT";
            bg_color = 0x3A2D12;
            border_color = UI_COLOR_WARN;
        }
    }

    if (s_modbus_label != NULL) {
        lv_label_set_text(s_modbus_label, text);
        lv_obj_center(s_modbus_label);
    }

    if (s_modbus_pill != NULL) {
        lv_obj_set_style_bg_color(s_modbus_pill, lv_color_hex(bg_color), 0);
        lv_obj_set_style_border_color(s_modbus_pill, lv_color_hex(border_color), 0);
    }
}

static void ui_update_net_status(void)
{
    camera_tcp_service_snapshot_t snapshot;
    const char *text = "NET --";
    uint32_t bg_color = UI_COLOR_CARD;
    uint32_t border_color = UI_COLOR_BORDER;

    if ((camera_tcp_service_get_snapshot(&snapshot) == RT_EOK) &&
        (snapshot.running != 0U) &&
        (snapshot.socket_open != 0U))
    {
        if ((snapshot.connected != 0U) ||
            (snapshot.last_ping_age_ms <= UI_NET_ACTIVE_MS) ||
            (snapshot.last_frame_age_ms <= UI_NET_ACTIVE_MS))
        {
            text = "NET OK";
            bg_color = 0x06351F;
            border_color = UI_COLOR_OK;
        }
        else
        {
            text = "NET RDY";
            bg_color = 0x102B3A;
            border_color = UI_COLOR_ACCENT;
        }
    }

    if (s_net_label != NULL)
    {
        lv_label_set_text(s_net_label, text);
        lv_obj_center(s_net_label);
    }

    if (s_net_pill != NULL)
    {
        lv_obj_set_style_bg_color(s_net_pill, lv_color_hex(bg_color), 0);
        lv_obj_set_style_border_color(s_net_pill, lv_color_hex(border_color), 0);
    }
}

static void ui_update_model_status(void)
{
    model_app_state_t model_state;
    const char *name = "--";
    uint32_t bg_color = 0x3A2D12;
    uint32_t border_color = UI_COLOR_WARN;
    char text[40];

    if ((model_app_get_state(&model_state) == RT_EOK) &&
        (model_state.metadata_loaded == RT_TRUE) &&
        (model_state.name != RT_NULL) &&
        (rt_strcmp(model_state.name, "unloaded") != 0))
    {
        name = model_state.name;
        if ((model_state.source == MODEL_APP_SOURCE_SDCARD) &&
            (model_state.files_present == RT_TRUE) &&
            (model_state.sdram_loaded == RT_TRUE))
        {
            bg_color = 0x06351F;
            border_color = UI_COLOR_OK;
        }
        else
        {
            bg_color = 0x102B3A;
            border_color = UI_COLOR_ACCENT;
        }
    }

    rt_snprintf(text, sizeof(text), "MDL %.18s", name);

    if (s_model_label != NULL)
    {
        lv_label_set_text(s_model_label, text);
        lv_obj_center(s_model_label);
    }

    if (s_model_pill != NULL)
    {
        lv_obj_set_style_bg_color(s_model_pill, lv_color_hex(bg_color), 0);
        lv_obj_set_style_border_color(s_model_pill, lv_color_hex(border_color), 0);
    }
}

void ui_notify_storage_changed(void)
{
    ui_storage_changed_handler(NULL);
}

static void ui_detect_result_published_handler(void *user_data)
{
    (void)user_data;
    s_detect_result_dirty = 1U;
}

void ui_process_pending_events(void)
{
    rt_bool_t refresh_data_page = RT_FALSE;

    s_modbus_refresh_divider++;
    if (s_modbus_refresh_divider >= 20U) {
        s_modbus_refresh_divider = 0U;
        ui_update_modbus_status();
        ui_update_net_status();
    }

    if (s_detect_result_dirty != 0U) {
        const ui_detect_snapshot_t *snapshot;

        s_detect_result_dirty = 0U;
        snapshot = ui_data_get_detect_snapshot();
        if (snapshot != NULL) {
            ui_update_run_status(snapshot->run_state);
        }
        ui_update_model_status();
        ui_update_net_status();
        ui_detect_refresh_from_snapshot();
        ui_sync_detect_camera_overlay();
        refresh_data_page = RT_TRUE;
    }

    if (s_storage_config_dirty != 0U) {
        s_storage_config_dirty = 0U;
        if ((ui_settings_data_load_config() == RT_EOK) &&
            (s_current_page == UI_PAGE_SETTINGS)) {
            ui_settings_on_show();
        }
        refresh_data_page = RT_TRUE;
    }

    if (s_storage_status_dirty != 0U) {
        s_storage_status_dirty = 0U;
        ui_update_storage_status();
        refresh_data_page = RT_TRUE;
    }

    if ((refresh_data_page == RT_TRUE) && (s_current_page == UI_PAGE_DATA)) {
        ui_data_page_refresh();
    }
}

static void ui_detect_command_handler(ui_detect_command_t cmd, void *user_data)
{
    int ret = RT_EOK;

    (void)user_data;

    switch (cmd)
    {
    case UI_DETECT_CMD_START:
        ret = sort_flow_auto_start();
        if (ret != RT_EOK)
        {
            rt_kprintf("[ui] auto flow start failed ret=%d\n", ret);
        }
        break;

    case UI_DETECT_CMD_STOP:
        ret = sort_flow_auto_stop();
        if (ret != RT_EOK)
        {
            rt_kprintf("[ui] auto flow stop failed ret=%d\n", ret);
        }
        detect_app_handle_command_from(UI_DETECT_CMD_STOP, "ui");
        break;

    case UI_DETECT_CMD_REFRESH:
    default:
        detect_app_handle_command_from(cmd, "ui");
        break;
    }

    ui_update_run_status(ui_data_get_detect_snapshot()->run_state);
    ui_update_net_status();
    ui_update_model_status();
    ui_sync_detect_camera_overlay();
}

static void ui_data_command_handler(ui_data_command_t cmd, void *user_data)
{
    char export_path[STORAGE_PATH_MAX];

    (void)user_data;

    switch (cmd)
    {
    case UI_DATA_CMD_EXPORT_CSV:
        if (storage_app_export_records_csv(export_path, sizeof(export_path)) == RT_EOK)
        {
            ui_data_page_set_status_text(ui_text("Export OK", "Export OK"));
        }
        else
        {
            ui_data_page_set_status_text(ui_text("Export failed", "Export failed"));
        }
        break;

    case UI_DATA_CMD_CLEAR_LOGS:
        if (storage_app_clear_detect_records() == RT_EOK)
        {
            ui_data_page_set_status_text(ui_text("Clear OK", "Clear OK"));
        }
        else
        {
            ui_data_page_set_status_text(ui_text("Clear failed", "Clear failed"));
        }
        break;

    case UI_DATA_CMD_INIT_STORAGE:
        if (storage_app_reset_project_storage() == RT_EOK)
        {
            ui_data_page_set_status_text(ui_text("Reset OK", "Reset OK"));
        }
        else
        {
            ui_data_page_set_status_text(ui_text("Reset failed", "Reset failed"));
        }
        break;

    default:
        ui_data_page_set_status_text(ui_text("Unknown command", "Unknown command"));
        break;
    }

    ui_update_storage_status();
    ui_data_page_refresh();
}

static void ui_settings_command_handler(ui_settings_command_t cmd, uint16_t value, void *user_data)
{
    const ui_settings_model_file_t *selected;
    int ret;

    (void)value;
    (void)user_data;

    if (cmd != UI_SETTINGS_CMD_LOAD_MODEL)
    {
        return;
    }

    selected = ui_settings_data_get_selected_model();
    if ((selected == RT_NULL) || (selected->path == RT_NULL))
    {
        rt_kprintf("[ui] model load failed: no selected model\n");
        return;
    }

    sort_flow_auto_stop();
    detect_app_handle_command_from(UI_DETECT_CMD_STOP, "ui-model");
    ui_update_run_status(ui_data_get_detect_snapshot()->run_state);

    ret = model_app_load_package(selected->path);
    if (ret == RT_EOK)
    {
        rt_kprintf("[ui] model loaded: %s\n", selected->path);
    }
    else
    {
        rt_kprintf("[ui] model load failed path=%s ret=%d last=%s\n",
                   selected->path,
                   ret,
                   model_app_last_error());
    }

    ui_update_model_status();
    ui_sync_detect_camera_overlay();
}

static void ui_apply_camera_overlay_state(ui_page_t page)
{
    switch (page)
    {
    case UI_PAGE_DETECT:
        ui_sync_detect_camera_overlay();
        camera_app_set_lcd_preview_overlay_mode(CAMERA_APP_OVERLAY_DETECT);
        camera_app_set_lcd_preview_suspended(0U);
        camera_app_set_lcd_preview_fps(UI_DETECT_CAMERA_FPS);
        camera_app_set_lcd_preview_window(1U, UI_DETECT_CAMERA_X, UI_DETECT_CAMERA_Y);
        break;

    case UI_PAGE_COLLECT:
        camera_app_set_lcd_preview_overlay_mode(CAMERA_APP_OVERLAY_COLLECT);
        camera_app_set_lcd_preview_suspended(0U);
        camera_app_set_lcd_preview_fps(UI_COLLECT_CAMERA_FPS);
        camera_app_set_lcd_preview_window(1U, UI_COLLECT_CAMERA_X, UI_COLLECT_CAMERA_Y);
        break;

    case UI_PAGE_DATA:
    case UI_PAGE_SETTINGS:
    default:
        camera_app_set_lcd_preview_overlay_mode(CAMERA_APP_OVERLAY_NONE);
        camera_app_set_detect_heatmap(NULL);
        camera_app_set_lcd_preview_suspended(1U);
        camera_app_set_lcd_preview_visible(0U);
        break;
    }
}

static void ui_sync_detect_camera_overlay(void)
{
    const ui_detect_snapshot_t *snapshot = ui_data_get_detect_snapshot();
    const ui_ai_heatmap_t *heatmap = ui_data_get_detect_heatmap();
    camera_app_heatmap_t overlay_heatmap;

    if (snapshot != NULL)
    {
        camera_app_set_detect_roi(snapshot->camera.roi_x,
                                  snapshot->camera.roi_y,
                                  snapshot->camera.roi_size);
    }

    if (snapshot == NULL)
    {
        return;
    }

    if (snapshot->stage != UI_DETECT_STAGE_OUTPUT)
    {
        return;
    }

    if ((heatmap == NULL) ||
        (heatmap->score_map == NULL) ||
        (heatmap->grid_w == 0U) ||
        (heatmap->grid_h == 0U))
    {
        camera_app_set_detect_heatmap(NULL);
        return;
    }

    overlay_heatmap.grid_w = heatmap->grid_w;
    overlay_heatmap.grid_h = heatmap->grid_h;
    overlay_heatmap.roi_x = heatmap->roi_x;
    overlay_heatmap.roi_y = heatmap->roi_y;
    overlay_heatmap.roi_w = heatmap->roi_w;
    overlay_heatmap.roi_h = heatmap->roi_h;
    overlay_heatmap.score_map = heatmap->score_map;
    overlay_heatmap.score_count = heatmap->score_count;
    camera_app_set_detect_heatmap(&overlay_heatmap);
}

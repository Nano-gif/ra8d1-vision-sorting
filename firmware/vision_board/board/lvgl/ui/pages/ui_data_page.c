#include "ui_data_page.h"
#include "../ui_config.h"
#include "../ui_data.h"
#include "../ui_language.h"
#include "../ui_widgets.h"
#include <stdio.h>

#define UI_DATA_RECORD_ROW_H 28
#define UI_DATA_SUMMARY_CARD_COUNT 4
#define UI_DATA_RECORD_COL_COUNT 5
#define UI_DATA_MAX_RECORD_ROWS 16
#define UI_DATA_STORAGE_LINE_COUNT 3
#define UI_DATA_TREND_DEFAULT_MAX_X10 100U
#define UI_DATA_TREND_HEADROOM_PERCENT 25U
#define UI_DATA_TREND_ROUND_STEP_X10 50U

static lv_obj_t *s_summary_title_labels[UI_DATA_SUMMARY_CARD_COUNT];
static lv_obj_t *s_summary_value_labels[UI_DATA_SUMMARY_CARD_COUNT];
static lv_obj_t *s_summary_sub_labels[UI_DATA_SUMMARY_CARD_COUNT];
static lv_obj_t *s_summary_accent_strips[UI_DATA_SUMMARY_CARD_COUNT];
static lv_obj_t *s_records_title_label = NULL;
static lv_obj_t *s_record_list = NULL;
static lv_obj_t *s_record_header_labels[UI_DATA_RECORD_COL_COUNT];
static lv_obj_t *s_record_image_labels[UI_DATA_MAX_RECORD_ROWS];
static lv_obj_t *s_export_button_label = NULL;
static lv_obj_t *s_clear_button_label = NULL;
static lv_obj_t *s_init_button_label = NULL;
static lv_obj_t *s_action_status_label = NULL;
static lv_obj_t *s_trend_title_label = NULL;
static lv_obj_t *s_trend_hint_label = NULL;
static lv_obj_t *s_trend_chart = NULL;
static lv_chart_series_t *s_trend_score_series = NULL;
static lv_chart_series_t *s_trend_threshold_series = NULL;
static lv_obj_t *s_storage_title_label = NULL;
static lv_obj_t *s_storage_name_labels[UI_DATA_STORAGE_LINE_COUNT];
static lv_obj_t *s_storage_value_labels[UI_DATA_STORAGE_LINE_COUNT];
static lv_obj_t *s_storage_bar = NULL;
static lv_obj_t *s_storage_bar_fill = NULL;

static void ui_data_page_create_summary_cards(lv_obj_t *parent);
static void ui_data_page_create_summary_card(lv_obj_t *parent,
                                             uint8_t index,
                                             int32_t x,
                                             const char *title,
                                             const char *value,
                                             const char *sub,
                                             uint32_t accent_color);
static void ui_data_page_create_records_panel(lv_obj_t *parent);
static void ui_data_page_create_record_header(lv_obj_t *parent);
static void ui_data_page_create_record_row(lv_obj_t *parent,
                                           int32_t y,
                                           const ui_detect_record_t *record);
static void ui_data_page_create_trend_panel(lv_obj_t *parent);
static void ui_data_page_create_storage_panel(lv_obj_t *parent);
static void ui_data_page_create_storage_line(lv_obj_t *parent,
                                             int32_t y,
                                             const char *name,
                                             const char *value);
static void ui_data_page_style_action_button(lv_obj_t *btn,
                                             uint32_t bg_color,
                                             uint32_t grad_color,
                                             uint32_t border_color,
                                             uint32_t pressed_color);
static void ui_data_page_command_event_cb(lv_event_t *e);
static void ui_data_page_reset_widget_refs(void);
static void ui_data_page_rebuild_record_rows(void);
static void ui_data_page_refresh_summary_cards(void);
static void ui_data_page_refresh_storage_panel(void);
static void ui_data_page_refresh_trend_panel(void);
static void ui_data_page_apply_label_text(lv_obj_t *label, const char *text);
static void ui_data_page_apply_panel_title(lv_obj_t *label, const char *text);
static const char *ui_data_page_summary_title_text(uint8_t index);
static const char *ui_data_page_summary_sub_text(uint8_t index);
static const char *ui_data_page_record_header_text(uint8_t index);
static const char *ui_data_page_storage_line_name(uint8_t index);
static const char *ui_data_page_storage_state_text(ui_storage_state_t state);
static uint32_t ui_data_page_storage_state_color(ui_storage_state_t state);
static uint32_t ui_data_page_storage_bar_color(const ui_storage_info_t *storage);
static void ui_data_page_update_storage_bar(uint32_t percent, const ui_storage_info_t *storage);
static void ui_data_page_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10);
static uint16_t ui_data_page_calc_trend_y_max_x10(const ui_data_summary_t *summary,
                                                  const ui_detect_snapshot_t *detect);
static void ui_data_page_update_trend_hint(uint16_t y_max_x10);

/* 创建数据中心页面：顶部统计、左侧记录、右侧趋势和存储导出。 */
lv_obj_t *ui_data_page_create(lv_obj_t *parent)
{
    ui_data_page_reset_widget_refs();

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    ui_data_page_create_summary_cards(page);
    ui_data_page_create_records_panel(page);
    ui_data_page_create_trend_panel(page);
    ui_data_page_create_storage_panel(page);

    return page;
}

/* 全局语言切换后刷新数据页静态文字；数值仍然从 ui_data 接口读取，不在这里改数据来源。 */
void ui_data_page_refresh_language(void)
{
    for (uint8_t i = 0; i < UI_DATA_SUMMARY_CARD_COUNT; i++) {
        ui_data_page_apply_label_text(s_summary_title_labels[i], ui_data_page_summary_title_text(i));
        ui_data_page_apply_label_text(s_summary_sub_labels[i], ui_data_page_summary_sub_text(i));
    }

    ui_data_page_apply_panel_title(s_records_title_label, ui_text("最近检测记录", "Recent Inspection Records"));

    for (uint8_t i = 0; i < UI_DATA_RECORD_COL_COUNT; i++) {
        ui_data_page_apply_label_text(s_record_header_labels[i], ui_data_page_record_header_text(i));
    }

    for (uint8_t i = 0; i < UI_DATA_MAX_RECORD_ROWS; i++) {
        if (s_record_image_labels[i] != NULL) {
            ui_data_page_apply_label_text(s_record_image_labels[i], ui_text("已保存", "saved"));
        }
    }

    ui_data_page_apply_label_text(s_export_button_label, ui_text(LV_SYMBOL_UPLOAD " 导出CSV", LV_SYMBOL_UPLOAD " Export CSV"));
    if (s_export_button_label != NULL) {
        lv_obj_center(s_export_button_label);
    }

    ui_data_page_apply_label_text(s_clear_button_label, ui_text(LV_SYMBOL_TRASH " 清空", LV_SYMBOL_TRASH " Clear"));
    if (s_clear_button_label != NULL) {
        lv_obj_center(s_clear_button_label);
    }

    ui_data_page_apply_label_text(s_init_button_label, LV_SYMBOL_REFRESH);
    if (s_init_button_label != NULL) {
        lv_obj_set_style_text_font(s_init_button_label, &lv_font_montserrat_14, 0);
        lv_obj_center(s_init_button_label);
    }

    ui_data_page_apply_panel_title(s_trend_title_label, ui_text("异常趋势", "Anomaly Trend"));
    ui_data_page_apply_label_text(s_trend_hint_label, ui_text("最近10次检测", "last 10 inspections"));
    ui_data_page_apply_panel_title(s_storage_title_label, ui_text("存储 / 导出", "Storage / Export"));

    for (uint8_t i = 0; i < UI_DATA_STORAGE_LINE_COUNT; i++) {
        ui_data_page_apply_label_text(s_storage_name_labels[i], ui_data_page_storage_line_name(i));
    }
}

void ui_data_page_refresh_records(void)
{
    ui_data_page_rebuild_record_rows();
}

void ui_data_page_refresh(void)
{
    ui_data_page_refresh_summary_cards();
    ui_data_page_refresh_storage_panel();
    ui_data_page_refresh_trend_panel();
    ui_data_page_rebuild_record_rows();
}

void ui_data_page_set_status_text(const char *text)
{
    if (s_action_status_label == NULL) {
        return;
    }

    if (text == NULL) {
        text = ui_text("Ready", "Ready");
    }

    lv_label_set_text(s_action_status_label, text);
    lv_obj_set_style_text_font(s_action_status_label, ui_font_body(), 0);
}

/* 清空页面里保存的对象指针；页面重建时避免继续引用旧 LVGL 对象。 */
static void ui_data_page_reset_widget_refs(void)
{
    for (uint8_t i = 0; i < UI_DATA_SUMMARY_CARD_COUNT; i++) {
        s_summary_title_labels[i] = NULL;
        s_summary_value_labels[i] = NULL;
        s_summary_sub_labels[i] = NULL;
        s_summary_accent_strips[i] = NULL;
    }

    s_records_title_label = NULL;
    s_record_list = NULL;

    for (uint8_t i = 0; i < UI_DATA_RECORD_COL_COUNT; i++) {
        s_record_header_labels[i] = NULL;
    }

    for (uint8_t i = 0; i < UI_DATA_MAX_RECORD_ROWS; i++) {
        s_record_image_labels[i] = NULL;
    }

    s_export_button_label = NULL;
    s_clear_button_label = NULL;
    s_init_button_label = NULL;
    s_action_status_label = NULL;
    s_trend_title_label = NULL;
    s_trend_hint_label = NULL;
    s_trend_chart = NULL;
    s_trend_score_series = NULL;
    s_trend_threshold_series = NULL;
    s_storage_title_label = NULL;
    s_storage_bar = NULL;
    s_storage_bar_fill = NULL;

    for (uint8_t i = 0; i < UI_DATA_STORAGE_LINE_COUNT; i++) {
        s_storage_name_labels[i] = NULL;
        s_storage_value_labels[i] = NULL;
    }
}

/* 创建顶部四个统计卡片；这些数值后续由真实记录模块填充。 */
static void ui_data_page_rebuild_record_rows(void)
{
    uint16_t count = 0U;
    const ui_detect_record_t *records;

    if (s_record_list == NULL) {
        return;
    }

    for (uint8_t i = 0; i < UI_DATA_MAX_RECORD_ROWS; i++) {
        s_record_image_labels[i] = NULL;
    }

    lv_obj_clean(s_record_list);
    records = ui_data_get_recent_records(&count);

    for (uint16_t i = 0; (i < count) && (i < UI_DATA_MAX_RECORD_ROWS); i++) {
        ui_data_page_create_record_row(s_record_list, (int32_t)i * UI_DATA_RECORD_ROW_H, &records[i]);
    }
}

static void ui_data_page_refresh_summary_cards(void)
{
    char storage_value[24];
    char record_value[12];
    char sample_value[12];
    char ng_rate_value[12];
    const ui_data_summary_t *summary = ui_data_get_data_summary();
    const ui_storage_info_t *storage = ui_data_get_storage_info();

    if ((summary == NULL) || (storage == NULL)) {
        return;
    }

    snprintf(storage_value,
             sizeof(storage_value),
             "%s %luMB",
             ui_data_page_storage_state_text(storage->state),
             (unsigned long)storage->free_mb);
    snprintf(record_value, sizeof(record_value), "%u", summary->total_records);
    snprintf(sample_value, sizeof(sample_value), "%u", summary->total_samples);
    ui_data_page_format_x10(ng_rate_value, sizeof(ng_rate_value), summary->ng_rate_x10);

    ui_data_page_apply_label_text(s_summary_value_labels[0], storage_value);
    ui_data_page_apply_label_text(s_summary_value_labels[1], record_value);
    ui_data_page_apply_label_text(s_summary_value_labels[2], sample_value);
    ui_data_page_apply_label_text(s_summary_value_labels[3], ng_rate_value);

    if (s_summary_accent_strips[0] != NULL) {
        lv_obj_set_style_bg_color(s_summary_accent_strips[0],
                                  lv_color_hex(ui_data_page_storage_state_color(storage->state)),
                                  0);
    }
}

static void ui_data_page_refresh_storage_panel(void)
{
    char usage_text[32];
    char record_text[32];
    char export_text[32];
    const ui_storage_info_t *storage = ui_data_get_storage_info();
    uint32_t percent = 0U;

    if (storage == NULL) {
        return;
    }

    if (storage->total_mb > 0U) {
        percent = (storage->used_mb * 100U) / storage->total_mb;
        if (percent > 100U) {
            percent = 100U;
        }
    }

    snprintf(usage_text,
             sizeof(usage_text),
             "%lu / %lu MB",
             (unsigned long)storage->used_mb,
             (unsigned long)storage->total_mb);
    snprintf(record_text, sizeof(record_text), "%s", storage->record_dir);
    snprintf(export_text, sizeof(export_text), "%s", storage->export_dir);

    if (s_storage_bar != NULL) {
        ui_data_page_update_storage_bar(percent, storage);
    }

    ui_data_page_apply_label_text(s_storage_value_labels[0], usage_text);
    ui_data_page_apply_label_text(s_storage_value_labels[1], record_text);
    ui_data_page_apply_label_text(s_storage_value_labels[2], export_text);
}

static void ui_data_page_refresh_trend_panel(void)
{
    const ui_data_summary_t *summary = ui_data_get_data_summary();
    const ui_detect_snapshot_t *detect = ui_data_get_detect_snapshot();
    uint16_t y_max_x10 = ui_data_page_calc_trend_y_max_x10(summary, detect);

    if ((s_trend_chart == NULL) ||
        (s_trend_score_series == NULL) ||
        (s_trend_threshold_series == NULL))
    {
        return;
    }

    if ((summary == NULL) ||
        (detect == NULL) ||
        (summary->trend_score_x10 == NULL) ||
        (summary->trend_count == 0U))
    {
        lv_chart_set_point_count(s_trend_chart, 1);
        lv_chart_set_range(s_trend_chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max_x10);
        lv_chart_set_all_value(s_trend_chart, s_trend_score_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(s_trend_chart, s_trend_threshold_series, LV_CHART_POINT_NONE);
        ui_data_page_update_trend_hint(y_max_x10);
        lv_chart_refresh(s_trend_chart);
        return;
    }

    lv_chart_set_point_count(s_trend_chart, summary->trend_count);
    lv_chart_set_range(s_trend_chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max_x10);
    lv_chart_set_all_value(s_trend_chart, s_trend_score_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_trend_chart, s_trend_threshold_series, LV_CHART_POINT_NONE);

    for (uint16_t i = 0; i < summary->trend_count; i++) {
        int32_t score = summary->trend_score_x10[i];
        int32_t threshold = detect->ai.threshold_x10;
        lv_chart_set_value_by_id(s_trend_chart, s_trend_score_series, i, score);
        lv_chart_set_value_by_id(s_trend_chart, s_trend_threshold_series, i, threshold);
    }

    ui_data_page_update_trend_hint(y_max_x10);
    lv_chart_refresh(s_trend_chart);
}

static void ui_data_page_create_summary_cards(lv_obj_t *parent)
{
    char storage_value[24];
    char record_value[12];
    char sample_value[12];
    char ng_rate_value[12];
    const ui_data_summary_t *summary = ui_data_get_data_summary();
    const ui_storage_info_t *storage = ui_data_get_storage_info();

    snprintf(storage_value,
             sizeof(storage_value),
             "%s %luGB",
             ui_data_page_storage_state_text(storage->state),
             (unsigned long)(storage->free_mb / 1024U));
    snprintf(record_value, sizeof(record_value), "%u", summary->total_records);
    snprintf(sample_value, sizeof(sample_value), "%u", summary->total_samples);
    ui_data_page_format_x10(ng_rate_value, sizeof(ng_rate_value), summary->ng_rate_x10);

    ui_data_page_create_summary_card(parent,
                                     0,
                                     10,
                                     ui_data_page_summary_title_text(0),
                                     storage_value,
                                     ui_data_page_summary_sub_text(0),
                                     ui_data_page_storage_state_color(storage->state));
    ui_data_page_create_summary_card(parent,
                                     1,
                                     198,
                                     ui_data_page_summary_title_text(1),
                                     record_value,
                                     ui_data_page_summary_sub_text(1),
                                     UI_COLOR_ACCENT);
    ui_data_page_create_summary_card(parent,
                                     2,
                                     386,
                                     ui_data_page_summary_title_text(2),
                                     sample_value,
                                     ui_data_page_summary_sub_text(2),
                                     UI_COLOR_OK);
    ui_data_page_create_summary_card(parent,
                                     3,
                                     574,
                                     ui_data_page_summary_title_text(3),
                                     ng_rate_value,
                                     ui_data_page_summary_sub_text(3),
                                     UI_COLOR_WARN);
}

/* 创建单个顶部统计卡片。 */
static void ui_data_page_create_summary_card(lv_obj_t *parent,
                                             uint8_t index,
                                             int32_t x,
                                             const char *title,
                                             const char *value,
                                             const char *sub,
                                             uint32_t accent_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 176, 58);
    lv_obj_set_pos(card, x, 10);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x171D22), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x222B31), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_grad_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x32414A), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *strip = lv_obj_create(card);
    lv_obj_set_size(strip, 4, 34);
    lv_obj_set_pos(strip, 10, 12);
    lv_obj_set_style_bg_color(strip, lv_color_hex(accent_color), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_radius(strip, 2, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(title_label, ui_font_body(), 0);
    lv_obj_set_pos(title_label, 24, 9);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(value_label, 24, 25);

    lv_obj_t *sub_label = lv_label_create(card);
    lv_label_set_text(sub_label, sub);
    lv_obj_set_width(sub_label, 66);
    lv_label_set_long_mode(sub_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(sub_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(sub_label, lv_color_hex(0x7D8A93), 0);
    lv_obj_set_style_text_font(sub_label, ui_font_body(), 0);
    lv_obj_set_pos(sub_label, 94, 34);

    if (index < UI_DATA_SUMMARY_CARD_COUNT) {
        s_summary_title_labels[index] = title_label;
        s_summary_value_labels[index] = value_label;
        s_summary_sub_labels[index] = sub_label;
        s_summary_accent_strips[index] = strip;
    }
}

/* 创建最近检测记录表；第一版使用轻量行布局，后续可替换为真实记录滚动列表。 */
static void ui_data_page_create_records_panel(lv_obj_t *parent)
{
    uint16_t count = 0;
    const ui_detect_record_t *records = ui_data_get_recent_records(&count);
    lv_obj_t *panel = ui_widget_create_panel(parent, 10, 80, 456, 260, ui_text("最近检测记录", "Recent Inspection Records"));

    s_records_title_label = lv_obj_get_child(panel, 0);
    ui_data_page_apply_panel_title(s_records_title_label, ui_text("最近检测记录", "Recent Inspection Records"));

    ui_data_page_create_record_header(panel);

    lv_obj_t *record_list = lv_obj_create(panel);
    s_record_list = record_list;
    lv_obj_set_size(record_list, 426, 152);
    lv_obj_set_pos(record_list, 14, 58);
    lv_obj_set_style_bg_opa(record_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(record_list, 0, 0);
    lv_obj_set_style_pad_all(record_list, 0, 0);
    lv_obj_set_scroll_dir(record_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(record_list, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_width(record_list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(record_list, lv_color_hex(0x38BDF8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(record_list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(record_list, 2, LV_PART_SCROLLBAR);

    for (uint16_t i = 0; (i < count) && (i < UI_DATA_MAX_RECORD_ROWS); i++) {
        ui_data_page_create_record_row(record_list, (int32_t)i * UI_DATA_RECORD_ROW_H, &records[i]);
    }

    lv_obj_t *export_btn = ui_widget_create_action_button(panel,
                                                          14,
                                                          218,
                                                          110,
                                                          ui_text(LV_SYMBOL_UPLOAD " 导出CSV", LV_SYMBOL_UPLOAD " Export CSV"),
                                                          UI_COLOR_CARD,
                                                          UI_COLOR_BORDER);
    s_export_button_label = lv_obj_get_child(export_btn, 0);
    ui_data_page_apply_label_text(s_export_button_label, ui_text(LV_SYMBOL_UPLOAD " 导出CSV", LV_SYMBOL_UPLOAD " Export CSV"));
    if (s_export_button_label != NULL) {
        lv_obj_center(s_export_button_label);
    }
    ui_data_page_style_action_button(export_btn, 0x0D5F64, 0x26AFAA, 0x38D7C3, 0x07383B);
    lv_obj_add_event_cb(export_btn,
                        ui_data_page_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_DATA_CMD_EXPORT_CSV);

    lv_obj_t *clear_btn = ui_widget_create_action_button(panel,
                                                          132,
                                                          218,
                                                          96,
                                                          ui_text(LV_SYMBOL_TRASH " 清空", LV_SYMBOL_TRASH " Clear"),
                                                          UI_COLOR_CARD,
                                                          UI_COLOR_BORDER);
    s_clear_button_label = lv_obj_get_child(clear_btn, 0);
    ui_data_page_apply_label_text(s_clear_button_label, ui_text(LV_SYMBOL_TRASH " 清空", LV_SYMBOL_TRASH " Clear"));
    if (s_clear_button_label != NULL) {
        lv_obj_center(s_clear_button_label);
    }
    ui_data_page_style_action_button(clear_btn, 0x2A2520, 0x4A3526, 0x8A6942, 0x181512);
    lv_obj_add_event_cb(clear_btn,
                        ui_data_page_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_DATA_CMD_CLEAR_LOGS);

    s_action_status_label = lv_label_create(panel);
    lv_obj_set_width(s_action_status_label, 196);
    lv_label_set_long_mode(s_action_status_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s_action_status_label, lv_color_hex(0x8EA0AA), 0);
    lv_obj_set_style_text_font(s_action_status_label, ui_font_body(), 0);
    lv_obj_set_pos(s_action_status_label, 242, 224);
    ui_data_page_set_status_text(NULL);
}

/* 创建记录表头。 */
static void ui_data_page_create_record_header(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, 426, 24);
    lv_obj_set_pos(header, 14, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x12181D), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x2D3942), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t xs[] = {10, 82, 154, 226, 354};

    for (uint32_t i = 0; i < 5; i++) {
        lv_obj_t *label = lv_label_create(header);
        lv_label_set_text(label, ui_data_page_record_header_text((uint8_t)i));
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
        lv_obj_set_style_text_font(label, ui_font_body(), 0);
        lv_obj_set_pos(label, xs[i], 6);
        s_record_header_labels[i] = label;
    }
}

/* 创建单行检测记录。 */
static void ui_data_page_create_record_row(lv_obj_t *parent,
                                           int32_t y,
                                           const ui_detect_record_t *record)
{
    char score_text[12];
    ui_data_page_format_x10(score_text, sizeof(score_text), record->score_x10);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 426, 24);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(record->anomaly ? 0x241B21 : 0x17211C), 0);
    lv_obj_set_style_bg_grad_color(row, lv_color_hex(0x151A1E), 0);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_grad_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(record->anomaly ? 0x6E3A42 : 0x2B4D3B), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_60, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *time = lv_label_create(row);
    lv_label_set_text(time, record->time);
    lv_obj_set_style_text_color(time, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(time, 10, 5);

    lv_obj_t *result = lv_label_create(row);
    lv_label_set_text(result, record->anomaly ? "NG" : "OK");
    lv_obj_set_style_text_color(result, lv_color_hex(record->anomaly ? UI_COLOR_NG : UI_COLOR_OK), 0);
    lv_obj_set_style_text_font(result, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(result, 88, 5);

    lv_obj_t *score = lv_label_create(row);
    lv_label_set_text(score, score_text);
    lv_obj_set_style_text_color(score, lv_color_hex(0xDDE7EA), 0);
    lv_obj_set_style_text_font(score, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(score, 158, 5);

    lv_obj_t *model = lv_label_create(row);
    lv_label_set_text(model, record->model_name);
    lv_obj_set_width(model, 92);
    lv_label_set_long_mode(model, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(model, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(model, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(model, 226, 5);

    lv_obj_t *image = lv_label_create(row);
    lv_label_set_text(image, (record->image_path && record->image_path[0]) ? ui_text("已保存", "saved") : "-");
    lv_obj_set_style_text_color(image, lv_color_hex(0xBFD2DA), 0);
    lv_obj_set_style_text_font(image, ui_font_body(), 0);
    lv_obj_set_pos(image, 354, 5);

    {
        uint32_t row_index = (uint32_t)(y / UI_DATA_RECORD_ROW_H);
        if (row_index < UI_DATA_MAX_RECORD_ROWS && record->image_path && record->image_path[0]) {
            s_record_image_labels[row_index] = image;
        }
    }
}

/* 创建右上角异常趋势图。 */
static void ui_data_page_create_trend_panel(lv_obj_t *parent)
{
    const ui_data_summary_t *summary = ui_data_get_data_summary();
    const ui_detect_snapshot_t *detect = ui_data_get_detect_snapshot();
    uint16_t point_count = ((summary != NULL) && (summary->trend_count > 0U)) ? summary->trend_count : 1U;
    uint16_t y_max_x10 = ui_data_page_calc_trend_y_max_x10(summary, detect);
    lv_obj_t *panel = ui_widget_create_panel(parent, 480, 80, 270, 140, ui_text("异常趋势", "Anomaly Trend"));

    s_trend_title_label = lv_obj_get_child(panel, 0);
    ui_data_page_apply_panel_title(s_trend_title_label, ui_text("异常趋势", "Anomaly Trend"));

    lv_obj_t *hint = lv_label_create(panel);
    s_trend_hint_label = hint;
    lv_label_set_text(hint, ui_text("最近10次检测", "last 10 inspections"));
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(hint, ui_font_body(), 0);
    lv_obj_set_pos(hint, 148, 14);

    lv_obj_t *chart = lv_chart_create(panel);
    s_trend_chart = chart;
    lv_obj_set_size(chart, 238, 84);
    lv_obj_set_pos(chart, 16, 42);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, point_count);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max_x10);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0E1418), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x26343C), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 4, 4, LV_PART_INDICATOR);

    lv_chart_series_t *score_series = lv_chart_add_series(chart,
                                                          lv_color_hex(UI_COLOR_ACCENT),
                                                          LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *threshold_series = lv_chart_add_series(chart,
                                                              lv_color_hex(UI_COLOR_WARN),
                                                              LV_CHART_AXIS_PRIMARY_Y);
    s_trend_score_series = score_series;
    s_trend_threshold_series = threshold_series;

    if ((summary == NULL) ||
        (detect == NULL) ||
        (summary->trend_score_x10 == NULL) ||
        (summary->trend_count == 0U))
    {
        lv_chart_set_all_value(chart, score_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_value(chart, threshold_series, LV_CHART_POINT_NONE);
        ui_data_page_update_trend_hint(y_max_x10);
        lv_chart_refresh(chart);
        return;
    }

    for (uint16_t i = 0; i < summary->trend_count; i++) {
        int32_t score = summary->trend_score_x10[i];
        int32_t threshold = detect->ai.threshold_x10;
        lv_chart_set_next_value(chart, score_series, score);
        lv_chart_set_next_value(chart, threshold_series, threshold);
    }

    ui_data_page_update_trend_hint(y_max_x10);
    lv_chart_refresh(chart);
}

/* 创建右下角存储和导出概览。 */
static void ui_data_page_create_storage_panel(lv_obj_t *parent)
{
    char usage_text[32];
    char record_text[32];
    char sample_text[32];
    char export_text[32];
    const ui_storage_info_t fallback_storage = {
        UI_STORAGE_ERROR,
        0U,
        0U,
        0U,
        "-",
        "-",
        "-"
    };
    const ui_storage_info_t *storage = ui_data_get_storage_info();
    uint32_t percent = 0;

    if (storage == NULL) {
        storage = &fallback_storage;
    }

    if (storage->total_mb > 0) {
        percent = (storage->used_mb * 100U) / storage->total_mb;
        if (percent > 100U) {
            percent = 100U;
        }
    }

    lv_obj_t *panel = ui_widget_create_panel(parent, 480, 230, 270, 110, ui_text("存储 / 导出", "Storage / Export"));

    s_storage_title_label = lv_obj_get_child(panel, 0);
    ui_data_page_apply_panel_title(s_storage_title_label, ui_text("存储 / 导出", "Storage / Export"));

    lv_obj_t *init_btn = lv_button_create(panel);
    lv_obj_set_size(init_btn, 22, 22);
    lv_obj_set_pos(init_btn, 228, 8);
    lv_obj_set_style_bg_color(init_btn, lv_color_hex(0x20303A), 0);
    lv_obj_set_style_bg_grad_color(init_btn, lv_color_hex(0x31444F), 0);
    lv_obj_set_style_bg_grad_dir(init_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(init_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(init_btn, lv_color_hex(0x5E7683), 0);
    lv_obj_set_style_border_opa(init_btn, LV_OPA_60, 0);
    lv_obj_set_style_border_width(init_btn, 1, 0);
    lv_obj_set_style_radius(init_btn, 11, 0);
    lv_obj_set_style_shadow_width(init_btn, 6, 0);
    lv_obj_set_style_shadow_color(init_btn, lv_color_hex(0x05080A), 0);
    lv_obj_set_style_shadow_opa(init_btn, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(init_btn, lv_color_hex(0x14242C), LV_STATE_PRESSED);
    lv_obj_clear_flag(init_btn, LV_OBJ_FLAG_SCROLLABLE);

    s_init_button_label = lv_label_create(init_btn);
    ui_data_page_apply_label_text(s_init_button_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(s_init_button_label, lv_color_hex(0xD7F0FF), 0);
    lv_obj_set_style_text_font(s_init_button_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_init_button_label);
    lv_obj_add_event_cb(init_btn,
                        ui_data_page_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_DATA_CMD_INIT_STORAGE);

    snprintf(usage_text,
             sizeof(usage_text),
             "%lu / %lu MB",
             (unsigned long)storage->used_mb,
             (unsigned long)storage->total_mb);
    snprintf(record_text, sizeof(record_text), "%s", storage->record_dir);
    snprintf(sample_text, sizeof(sample_text), "%s", storage->sample_dir);
    snprintf(export_text, sizeof(export_text), "%s", storage->export_dir);

    lv_obj_t *bar = lv_obj_create(panel);
    s_storage_bar = bar;
    lv_obj_set_size(bar, 226, 8);
    lv_obj_set_pos(bar, 18, 38);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A3338), LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x3A454C), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bar, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fill = lv_obj_create(bar);
    s_storage_bar_fill = fill;
    lv_obj_set_pos(fill, 1, 1);
    lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(fill, 1, LV_PART_MAIN);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    ui_data_page_update_storage_bar(percent, storage);

    ui_data_page_create_storage_line(panel, 52, ui_data_page_storage_line_name(0), usage_text);
    ui_data_page_create_storage_line(panel, 70, ui_data_page_storage_line_name(1), record_text);
    ui_data_page_create_storage_line(panel, 88, ui_data_page_storage_line_name(2), export_text);

    (void)sample_text;
}

/* 创建存储面板内的一行键值文本。 */
static void ui_data_page_create_storage_line(lv_obj_t *parent,
                                             int32_t y,
                                             const char *name,
                                             const char *value)
{
    uint8_t line_index = 0;
    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(name_label, ui_font_body(), 0);
    lv_obj_set_pos(name_label, 18, y);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_set_width(value_label, 178);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xC9D6DC), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(value_label, 72, y);

    if (y == 70) {
        line_index = 1;
    } else if (y == 88) {
        line_index = 2;
    }

    if (line_index < UI_DATA_STORAGE_LINE_COUNT) {
        s_storage_name_labels[line_index] = name_label;
        s_storage_value_labels[line_index] = value_label;
    }
}

/* 给 Data 页按钮增加和其它页面一致的渐变质感。 */
static void ui_data_page_style_action_button(lv_obj_t *btn,
                                             uint32_t bg_color,
                                             uint32_t grad_color,
                                             uint32_t border_color,
                                             uint32_t pressed_color)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(grad_color), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);

    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(pressed_color), LV_STATE_PRESSED);
}

/* Data 页按钮回调；只发命令，不直接操作 SD 卡。 */
static void ui_data_page_command_event_cb(lv_event_t *e)
{
    ui_data_command_t cmd = (ui_data_command_t)(uintptr_t)lv_event_get_user_data(e);

    ui_data_emit_data_command(cmd);
}

/* 设置普通标签文字，并根据当前语言选择中文子集字库或默认英文字体。 */
static void ui_data_page_apply_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
}

/* 设置面板标题文字；中文标题用中文字体，英文标题保留原来的 14px Montserrat。 */
static void ui_data_page_apply_panel_title(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14, 0);
}

/* 返回顶部统计卡片标题；只影响显示文案，不改变统计数据来源。 */
static const char *ui_data_page_summary_title_text(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text("SD卡", "SD CARD");
        case 1:
            return ui_text("检测记录", "RECORDS");
        case 2:
            return ui_text("采集样本", "SAMPLES");
        case 3:
            return ui_text("异常率", "NG RATE");
        default:
            return "";
    }
}

/* 返回顶部统计卡片右下角的小说明。 */
static const char *ui_data_page_summary_sub_text(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text("剩余", "free");
        case 1:
            return ui_text("日志", "logs");
        case 2:
            return ui_text("图片", "images");
        case 3:
            return ui_text("批次", "batch");
        default:
            return "";
    }
}

/* 返回检测记录表头文字；列位置仍保持原来的紧凑表格布局。 */
static const char *ui_data_page_record_header_text(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text("时间", "TIME");
        case 1:
            return ui_text("结果", "RESULT");
        case 2:
            return ui_text("分数", "SCORE");
        case 3:
            return ui_text("模型", "MODEL");
        case 4:
            return ui_text("图片", "IMAGE");
        default:
            return "";
    }
}

/* 返回存储信息行名称；路径和容量值仍由 ui_data 提供。 */
static const char *ui_data_page_storage_line_name(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text("已用", "used");
        case 1:
            return ui_text("记录", "record");
        case 2:
            return ui_text("导出", "export");
        default:
            return "";
    }
}

/* 把存储状态转成 UI 文本。 */
static const char *ui_data_page_storage_state_text(ui_storage_state_t state)
{
    if (state == UI_STORAGE_OK) {
        return "OK";
    } else if (state == UI_STORAGE_MISSING) {
        return "MISS";
    } else if (state == UI_STORAGE_FULL) {
        return "FULL";
    }

    return "ERR";
}

/* 把存储状态转成顶部卡片强调色。 */
static uint32_t ui_data_page_storage_state_color(ui_storage_state_t state)
{
    if (state == UI_STORAGE_OK) {
        return UI_COLOR_ACCENT;
    } else if (state == UI_STORAGE_FULL) {
        return UI_COLOR_WARN;
    } else if (state == UI_STORAGE_MISSING || state == UI_STORAGE_ERROR) {
        return UI_COLOR_WARN;
    }

    return UI_COLOR_BORDER;
}

static uint32_t ui_data_page_storage_bar_color(const ui_storage_info_t *storage)
{
    if ((storage == NULL) || (storage->state == UI_STORAGE_MISSING) || (storage->state == UI_STORAGE_ERROR)) {
        return UI_COLOR_WARN;
    }

    if (storage->state == UI_STORAGE_FULL) {
        return UI_COLOR_WARN;
    }

    return UI_COLOR_ACCENT;
}

/* 格式化放大 10 倍保存的小数值，例如 62 -> 6.2。 */
static void ui_data_page_update_storage_bar(uint32_t percent, const ui_storage_info_t *storage)
{
    int32_t fill_width;

    if ((s_storage_bar == NULL) || (s_storage_bar_fill == NULL)) {
        return;
    }

    if (percent > 100U) {
        percent = 100U;
    }

    fill_width = (int32_t)((224U * percent) / 100U);
    if (fill_width <= 0) {
        lv_obj_add_flag(s_storage_bar_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_storage_bar_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_storage_bar_fill, fill_width, 6);
    lv_obj_set_style_bg_color(s_storage_bar_fill,
                              lv_color_hex(ui_data_page_storage_bar_color(storage)),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_storage_bar_fill, LV_OPA_COVER, LV_PART_MAIN);
}

static void ui_data_page_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10)
{
    snprintf(buf, buf_size, "%u.%u", value_x10 / 10U, value_x10 % 10U);
}

static uint16_t ui_data_page_calc_trend_y_max_x10(const ui_data_summary_t *summary,
                                                  const ui_detect_snapshot_t *detect)
{
    uint32_t max_x10 = UI_DATA_TREND_DEFAULT_MAX_X10;
    uint32_t min_visible_x10 = UI_DATA_TREND_DEFAULT_MAX_X10;

    if ((detect != NULL) && (detect->ai.threshold_x10 > 0U)) {
        min_visible_x10 = (uint32_t)detect->ai.threshold_x10 * 2U;
        if (detect->ai.threshold_x10 > max_x10) {
            max_x10 = detect->ai.threshold_x10;
        }
    }

    if ((summary != NULL) && (summary->trend_score_x10 != NULL)) {
        for (uint16_t i = 0; i < summary->trend_count; i++) {
            if (summary->trend_score_x10[i] > max_x10) {
                max_x10 = summary->trend_score_x10[i];
            }
        }
    }

    max_x10 += (max_x10 * UI_DATA_TREND_HEADROOM_PERCENT) / 100U;
    max_x10 = ((max_x10 + UI_DATA_TREND_ROUND_STEP_X10 - 1U) / UI_DATA_TREND_ROUND_STEP_X10) *
              UI_DATA_TREND_ROUND_STEP_X10;

    if (max_x10 < min_visible_x10) {
        max_x10 = min_visible_x10;
    }
    if (max_x10 < UI_DATA_TREND_DEFAULT_MAX_X10) {
        max_x10 = UI_DATA_TREND_DEFAULT_MAX_X10;
    }
    if (max_x10 > UINT16_MAX) {
        max_x10 = UINT16_MAX;
    }

    return (uint16_t)max_x10;
}

static void ui_data_page_update_trend_hint(uint16_t y_max_x10)
{
    char text[40];

    if (s_trend_hint_label == NULL) {
        return;
    }

    snprintf(text,
             sizeof(text),
             "%s 0-%u.%u",
             ui_text("最近10次", "last 10"),
             y_max_x10 / 10U,
             y_max_x10 % 10U);
    ui_data_page_apply_label_text(s_trend_hint_label, text);
}

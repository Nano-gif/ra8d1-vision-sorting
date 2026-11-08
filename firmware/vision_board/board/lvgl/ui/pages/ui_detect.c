#include "ui_detect.h"
#include "../ui_config.h"
#include "../ui_data.h"
#include "../ui_language.h"
#include "../ui_widgets.h"
#include <stdio.h>
#include <stdint.h>

static lv_obj_t *s_camera_img = NULL;
static lv_obj_t *s_camera_placeholder = NULL;
#if UI_DETECT_ENABLE_FAKE_CAMERA
static lv_obj_t *s_heatmap_layer = NULL;
#endif
static lv_image_dsc_t s_camera_img_dsc;

static lv_obj_t *s_panel_live_title = NULL;
static lv_obj_t *s_panel_result_title = NULL;
static lv_obj_t *s_panel_inference_title = NULL;
static lv_obj_t *s_result_panel = NULL;
static lv_obj_t *s_result_state_label = NULL;
static lv_obj_t *s_result_score_label = NULL;
static lv_obj_t *s_result_score_bar = NULL;
static lv_obj_t *s_metric_value_labels[6];
static lv_obj_t *s_metric_name_labels[6];
static lv_obj_t *s_button_labels[3];
#if UI_DETECT_ENABLE_FAKE_CAMERA
static lv_obj_t *s_roi_label = NULL;
#endif

typedef struct {
    ui_detect_stage_t stage;
    lv_obj_t *dot;
    lv_obj_t *label;
} ui_detect_stage_view_t;

#define UI_DETECT_STAGE_VIEW_COUNT 4

static lv_obj_t *s_stage_active_line = NULL;
static ui_detect_stage_view_t s_stage_views[UI_DETECT_STAGE_VIEW_COUNT];
static lv_timer_t *s_stage_timer = NULL;
static lv_timer_t *s_fps_timer = NULL;
static ui_detect_stage_t s_stage_rendered = UI_DETECT_STAGE_IDLE;
static ui_detect_run_state_t s_stage_timer_run_state = UI_DETECT_RUN_STOPPED;
static uint32_t s_result_frame_rendered = UINT32_MAX;
static void (*s_result_changed_cb)(void) = NULL;

#if UI_DETECT_ENABLE_FAKE_CAMERA
static uint16_t s_fake_camera_frame[UI_CAMERA_SRC_W * UI_CAMERA_SRC_H];
#endif

static void ui_detect_create_camera_image(lv_obj_t *preview);
#if UI_DETECT_ENABLE_FAKE_CAMERA
static void ui_detect_create_heatmap_layer(lv_obj_t *preview);
#endif
static void ui_detect_create_camera_housing(lv_obj_t *parent, int32_t view_x, int32_t view_y);
static void ui_detect_create_housing_screw(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_detect_create_housing_accent(lv_obj_t *parent,
                                            int32_t x,
                                            int32_t y,
                                            int32_t width,
                                            int32_t height,
                                            uint32_t color,
                                            lv_opa_t opa);
static void ui_detect_create_stage_rail(lv_obj_t *parent,
                                        const ui_detect_snapshot_t *data,
                                        int32_t x,
                                        int32_t y);
static void ui_detect_create_stage_item(lv_obj_t *parent,
                                        int32_t y,
                                        const char *text,
                                        ui_detect_stage_t stage,
                                        const ui_detect_snapshot_t *data);
static bool ui_detect_stage_is_active(ui_detect_stage_t stage, ui_detect_stage_t current);
static uint32_t ui_detect_get_stage_color(ui_detect_stage_t stage, const ui_detect_snapshot_t *data);
static int32_t ui_detect_stage_to_index(ui_detect_stage_t stage);
static void ui_detect_update_stage_rail(const ui_detect_snapshot_t *data);
static void ui_detect_stage_timer_cb(lv_timer_t *timer);
void ui_detect_refresh_from_snapshot(void);
static void ui_detect_update_stage_timer_state(const ui_detect_snapshot_t *data);
static void ui_detect_update_result_view(const ui_detect_snapshot_t *data);
static void ui_detect_update_metric_values(const ui_detect_snapshot_t *data);
static int32_t ui_detect_score_bar_value(const ui_detect_snapshot_t *data);
static void ui_detect_apply_result_panel_style(lv_obj_t *panel, bool anomaly);
static void ui_detect_update_fps_label(void);
static void ui_detect_fps_timer_cb(lv_timer_t *timer);
#if UI_DETECT_ENABLE_FAKE_CAMERA
static void ui_detect_create_camera_overlay(lv_obj_t *preview, const ui_detect_snapshot_t *data);
#endif
static void ui_detect_create_viewfinder_decoration(lv_obj_t *parent, int32_t view_x, int32_t view_y);
static void ui_detect_create_corner_segment(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height);
static void ui_detect_create_panel_badge(lv_obj_t *parent, int32_t x, int32_t y, const char *text);
static void ui_detect_create_slogan_banner(lv_obj_t *parent, int32_t x, int32_t y);
static void ui_detect_create_banner_accent(lv_obj_t *parent, int32_t x, uint32_t color);
static lv_obj_t *ui_detect_create_metric_card_compact(lv_obj_t *parent,
                                                      int32_t x,
                                                      int32_t y,
                                                      uint8_t index,
                                                      const char *value,
                                                      const char *name);
static void ui_detect_apply_button_font(lv_obj_t *btn);
static void ui_detect_apply_label_text(lv_obj_t *label, const char *text);
static void ui_detect_apply_panel_title(lv_obj_t *label, const char *text);
static const char *ui_detect_result_text(bool anomaly);
static const char *ui_detect_metric_name(uint8_t index);
static const char *ui_detect_button_text(uint8_t index);
static void ui_detect_style_action_button(lv_obj_t *btn,
                                          uint32_t bg_color,
                                          uint32_t grad_color,
                                          uint32_t border_color,
                                          uint32_t pressed_color);
#if UI_DETECT_ENABLE_FAKE_CAMERA
#if UI_DETECT_ENABLE_FAKE_CAMERA
static void ui_detect_create_heatmap_cell(lv_obj_t *parent,
                                          int32_t x,
                                          int32_t y,
                                          int32_t width,
                                          int32_t height,
                                          uint8_t intensity);
static uint32_t ui_detect_get_heatmap_color(uint8_t intensity);
static lv_opa_t ui_detect_get_heatmap_opa(uint8_t intensity);
#endif
static void ui_detect_command_event_cb(lv_event_t *e);
static void ui_detect_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10);
#if UI_DETECT_ENABLE_FAKE_CAMERA
static uint16_t ui_detect_pack_rgb565(uint8_t r, uint8_t g, uint8_t b);
static void ui_detect_fill_fake_camera_frame(void);
static uint16_t ui_detect_get_roi_size(const ui_detect_snapshot_t *data);
static uint16_t ui_detect_get_roi_x(const ui_detect_snapshot_t *data, uint16_t roi_size);
static int32_t ui_detect_scale_x(uint16_t src_x);
static int32_t ui_detect_scale_y(uint16_t src_y);
#endif

/* 创建检测页，包括摄像头预览、结果卡片、推理指标和操作按钮。 */
lv_obj_t *ui_detect_create(lv_obj_t *parent)
{
    /*
     * Detect 页面只负责检测页内部布局。
     * 顶栏、底栏、页面切换都在 ui.c 里管理。
     */
    if (s_stage_timer != NULL) {
        lv_timer_delete(s_stage_timer);
        s_stage_timer = NULL;
    }
    if (s_fps_timer != NULL) {
        lv_timer_delete(s_fps_timer);
        s_fps_timer = NULL;
    }
    s_stage_rendered = UI_DETECT_STAGE_IDLE;
    s_stage_timer_run_state = UI_DETECT_RUN_STOPPED;
    s_result_frame_rendered = UINT32_MAX;

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    const ui_detect_snapshot_t *data = ui_data_get_detect_snapshot();

    for (uint8_t i = 0; i < 6; i++) {
        s_metric_value_labels[i] = NULL;
        s_metric_name_labels[i] = NULL;
    }
    for (uint8_t i = 0; i < 3; i++) {
        s_button_labels[i] = NULL;
    }
    s_panel_live_title = NULL;
    s_panel_result_title = NULL;
    s_panel_inference_title = NULL;
    s_result_panel = NULL;
    s_result_state_label = NULL;
    s_result_score_label = NULL;
    s_result_score_bar = NULL;
#if UI_DETECT_ENABLE_FAKE_CAMERA
    s_roi_label = NULL;
#endif

    lv_obj_t *camera_panel = ui_widget_create_panel(page, 10, 10, 465, 330, ui_text("实时检测", "Live Detection"));
    s_panel_live_title = lv_obj_get_child(camera_panel, 0);
    ui_detect_apply_panel_title(s_panel_live_title, ui_text("实时检测", "Live Detection"));
    ui_detect_create_panel_badge(camera_panel, 367, 13, "VISUAL AD");

    /*
     * 摄像头预览区。
     * OV5640 当前输出 320x240；这里的 LVGL preview 对象也固定为 320x240。
     * 后续接真实摄像头时，可以把视频帧直接绘制到这个对象内部。
     */
    const int32_t preview_x = 86;
    const int32_t preview_y = 52;
    ui_detect_create_camera_housing(camera_panel, preview_x, preview_y);
    ui_detect_create_stage_rail(camera_panel, data, 5, 61);

    lv_obj_t *preview = lv_obj_create(camera_panel);
    lv_obj_set_size(preview, UI_CAMERA_VIEW_W, UI_CAMERA_VIEW_H);
    lv_obj_set_pos(preview, preview_x, preview_y);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x11171A), 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(preview, lv_color_hex(0x26323A), 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_radius(preview, 4, 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动
    ui_detect_create_viewfinder_decoration(camera_panel, preview_x, preview_y);
    ui_detect_create_camera_image(preview);

    s_camera_placeholder = lv_label_create(preview);
    lv_label_set_text(s_camera_placeholder, "OV5640 320x240");
    lv_obj_set_style_text_color(s_camera_placeholder, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(s_camera_placeholder, &lv_font_montserrat_16, 0);
    lv_obj_center(s_camera_placeholder);

#if UI_DETECT_ENABLE_FAKE_CAMERA
    ui_detect_create_heatmap_layer(preview);
    ui_detect_fill_fake_camera_frame();
    ui_detect_set_camera_frame_rgb565((const uint8_t *)s_fake_camera_frame,
                                      UI_CAMERA_SRC_W,
                                      UI_CAMERA_SRC_H);
    ui_detect_set_heatmap(ui_data_get_detect_heatmap());
    ui_detect_create_camera_overlay(preview, data);
#endif
    ui_detect_create_slogan_banner(camera_panel, 60, 302);

    lv_obj_t *result_panel = ui_widget_create_panel(page, 490, 10, 260, 110, ui_text("检测结果", "Result"));
    s_result_panel = result_panel;
    s_panel_result_title = lv_obj_get_child(result_panel, 0);
    ui_detect_apply_panel_title(s_panel_result_title, ui_text("检测结果", "Result"));
    /*
     * 检测结果卡片使用横向渐变。
     * 这对应 HTML 里的 linear-gradient：左侧保留结果状态色，右侧过渡到更暗的底色。
     */
    ui_detect_apply_result_panel_style(result_panel, data->ai.anomaly);

    lv_obj_t *state = lv_label_create(result_panel);
    s_result_state_label = state;
    lv_label_set_text(state, ui_detect_result_text(data->ai.anomaly));
    lv_obj_set_style_text_color(state, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(state, 16, 34);

    /*
     * 异常分数来自 ui_data 接口。
     * score_x10 不是概率，也不是置信度；它是“异常强度分数 * 10”。
     * 例如 score_x10=32 会显示为 3.2，threshold_x10=59 会显示为 5.9。
     */
    char anomaly_score_value[8];
    char threshold_value[8];
    char result_score_text[24];
    ui_detect_format_x10(anomaly_score_value, sizeof(anomaly_score_value), data->ai.score_x10);
    ui_detect_format_x10(threshold_value, sizeof(threshold_value), data->ai.threshold_x10);
    snprintf(result_score_text, sizeof(result_score_text), "%s / %s", anomaly_score_value, threshold_value);
    // 显示异常分数和异常阈值，格式为“当前分数 / 判定阈值”。
    lv_obj_t *score = lv_label_create(result_panel);
    s_result_score_label = score;
    lv_label_set_text(score, result_score_text);
    lv_obj_set_style_text_color(score, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(score, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(score, 18, 78);

    // 显示异常分数相对阈值的位置：进度条中点约等于阈值，超过中点表示接近或进入异常。
    lv_obj_t *score_bar = lv_bar_create(result_panel);
    s_result_score_bar = score_bar;
    lv_obj_set_size(score_bar, 124, 8);
    lv_obj_set_pos(score_bar, 112, 85);
    lv_bar_set_range(score_bar, 0, 100); // 设置进度条范围
    lv_bar_set_value(score_bar, ui_detect_score_bar_value(data), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(score_bar, lv_color_hex(0x26171B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(score_bar, lv_color_hex(UI_COLOR_WARN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(score_bar, lv_color_hex(UI_COLOR_NG), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(score_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(score_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(score_bar, 4, LV_PART_INDICATOR);

    lv_obj_t *metric_panel = ui_widget_create_panel(page, 490, 130, 260, 174, ui_text("推理数据", "Inference"));
    s_panel_inference_title = lv_obj_get_child(metric_panel, 0);
    ui_detect_apply_panel_title(s_panel_inference_title, ui_text("推理数据", "Inference"));
    char max_score[8];
    char mean_score[8];
    char hot_cells[8];
    char ai_time[12];
    char fps[8];
    char yield_value[8];
    char yield[12];
    ui_detect_format_x10(max_score, sizeof(max_score), data->ai.score_x10);
    ui_detect_format_x10(mean_score, sizeof(mean_score), data->ai.mean_score_x10);
    snprintf(hot_cells, sizeof(hot_cells), "%u", (unsigned)data->ai.hot_cells);
    snprintf(ai_time, sizeof(ai_time), "%u ms", (unsigned)data->ai.ai_time_ms);
    ui_detect_format_x10(fps, sizeof(fps), data->ai.fps_x10);
    ui_detect_format_x10(yield_value, sizeof(yield_value), data->ai.yield_x10);
    snprintf(yield, sizeof(yield), "%s%%", yield_value);

    ui_detect_create_metric_card_compact(metric_panel, 14, 34, 0, max_score, ui_detect_metric_name(0));
    ui_detect_create_metric_card_compact(metric_panel, 134, 34, 1, mean_score, ui_detect_metric_name(1));
    ui_detect_create_metric_card_compact(metric_panel, 14, 82, 2, hot_cells, ui_detect_metric_name(2));
    ui_detect_create_metric_card_compact(metric_panel, 134, 82, 3, ai_time, ui_detect_metric_name(3));
    ui_detect_create_metric_card_compact(metric_panel, 14, 130, 4, fps, ui_detect_metric_name(4));
    ui_detect_create_metric_card_compact(metric_panel, 134, 130, 5, yield, ui_detect_metric_name(5));
    s_fps_timer = lv_timer_create(ui_detect_fps_timer_cb, 1000, NULL);
    ui_detect_update_fps_label();

    /*
     * 启停刷新按钮。
     * 回调已经预留：按钮 -> ui_data_emit_detect_command() -> 真实命令回调。
     */
    lv_obj_t *start_btn = ui_widget_create_action_button(page, 490, 312, 78,
                                                         ui_detect_button_text(0),
                                                         UI_COLOR_OK, UI_COLOR_OK);
    lv_obj_t *stop_btn = ui_widget_create_action_button(page, 581, 312, 78,
                                                        ui_detect_button_text(1),
                                                        UI_COLOR_STOP, UI_COLOR_STOP);
    lv_obj_t *refresh_btn = ui_widget_create_action_button(page, 672, 312, 78,
                                                           ui_detect_button_text(2),
                                                           UI_COLOR_CARD, UI_COLOR_BORDER);
    s_button_labels[0] = lv_obj_get_child(start_btn, 0);
    s_button_labels[1] = lv_obj_get_child(stop_btn, 0);
    s_button_labels[2] = lv_obj_get_child(refresh_btn, 0);
    ui_detect_apply_button_font(start_btn);
    ui_detect_apply_button_font(stop_btn);
    ui_detect_apply_button_font(refresh_btn);

    /*
     * 操作按钮采用类似 CSS 中 button gradient + pressed state 的写法。
     * 按钮自身不保存运行状态，点击后仍然只通过数据接口发命令。
     */
    ui_detect_style_action_button(start_btn, 0x0D6B45, 0x24C87C, 0x39E28E, 0x06442C);
    ui_detect_style_action_button(stop_btn, 0x842A35, 0xC43A47, 0xE05C67, 0x4A181F);
    ui_detect_style_action_button(refresh_btn, 0x1A2329, 0x2A3640, 0x46545E, 0x11171C);

    lv_obj_add_event_cb(start_btn, ui_detect_command_event_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)UI_DETECT_CMD_START);
    lv_obj_add_event_cb(stop_btn, ui_detect_command_event_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)UI_DETECT_CMD_STOP);
    lv_obj_add_event_cb(refresh_btn, ui_detect_command_event_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)UI_DETECT_CMD_REFRESH);

    return page;
}

/* 创建摄像头图像对象，它会铺满 320x240 的 preview 区域。 */
static void ui_detect_create_camera_image(lv_obj_t *preview)
{
    /*
     * s_camera_img 只负责显示 RGB565 帧。
     * ROI、异常框等叠加元素继续用 LVGL 子对象覆盖在它上方。
     */
    s_camera_img = lv_image_create(preview);
    lv_obj_set_size(s_camera_img, UI_CAMERA_VIEW_W, UI_CAMERA_VIEW_H);
    lv_obj_set_pos(s_camera_img, 0, 0);
    lv_obj_clear_flag(s_camera_img, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建热力图覆盖层；它只承载异常色块，不保存任何模型算法逻辑。 */
#if UI_DETECT_ENABLE_FAKE_CAMERA
static void ui_detect_create_heatmap_layer(lv_obj_t *preview)
{
    /*
     * 热力图层覆盖在摄像头图像之上、ROI 边框之下。
     * 这样真实图像仍然清楚，异常区域又能被评委一眼看到。
     */
    s_heatmap_layer = lv_obj_create(preview);
    lv_obj_set_size(s_heatmap_layer, UI_CAMERA_VIEW_W, UI_CAMERA_VIEW_H);
    lv_obj_set_pos(s_heatmap_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_heatmap_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_heatmap_layer, 0, 0);
    lv_obj_set_style_pad_all(s_heatmap_layer, 0, 0);
    lv_obj_clear_flag(s_heatmap_layer, LV_OBJ_FLAG_SCROLLABLE);
}
#endif

/* 创建摄像头预览区外壳，让 320x240 实时画面看起来像嵌入式工业相机窗口。 */
static void ui_detect_create_camera_housing(lv_obj_t *parent, int32_t view_x, int32_t view_y)
{
    /*
     * 外壳只是视觉装饰，不改变真实图像对象尺寸。
     * 预览区仍保持 320x240，一比一对应 OV5640 输出帧，方便后续做 ROI 和热力图坐标映射。
     */
    const int32_t shell_x = view_x - 34;
    const int32_t shell_y = view_y - 12;
    const int32_t shell_w = UI_CAMERA_VIEW_W + 68;
    const int32_t shell_h = UI_CAMERA_VIEW_H + 20;

    lv_obj_t *shell = lv_obj_create(parent);
    lv_obj_set_size(shell, shell_w, shell_h);
    lv_obj_set_pos(shell, shell_x, shell_y);
    lv_obj_set_style_bg_color(shell, lv_color_hex(0x151B20), 0);
    lv_obj_set_style_bg_grad_color(shell, lv_color_hex(0x252D34), 0);
    lv_obj_set_style_bg_grad_dir(shell, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(shell, lv_color_hex(0x38464F), 0);
    lv_obj_set_style_border_opa(shell, LV_OPA_70, 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_radius(shell, 7, 0);
    lv_obj_set_style_shadow_width(shell, 10, 0);
    lv_obj_set_style_shadow_color(shell, lv_color_hex(0x05080A), 0);
    lv_obj_set_style_shadow_opa(shell, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(shell, 0, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部细高光模拟金属面板边缘，增强相机模块质感。 */
    lv_obj_t *top_light = lv_obj_create(shell);
    lv_obj_set_size(top_light, shell_w - 42, 2);
    lv_obj_set_pos(top_light, 21, 7);
    lv_obj_set_style_bg_color(top_light, lv_color_hex(0x5B737D), 0);
    lv_obj_set_style_bg_grad_color(top_light, lv_color_hex(0x25323A), 0);
    lv_obj_set_style_bg_grad_dir(top_light, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(top_light, LV_OPA_60, 0);
    lv_obj_set_style_border_width(top_light, 0, 0);
    lv_obj_set_style_radius(top_light, 1, 0);
    lv_obj_set_style_pad_all(top_light, 0, 0);
    lv_obj_clear_flag(top_light, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * 横向外壳比预览画面宽，左右两侧用暗色加强筋填充。
     * 这样能体现“相机模块外框”，又不会改变中间 320x240 图像区域。
     */
    ui_detect_create_housing_accent(shell, 15, 28, 12, shell_h - 56, 0x0E151A, LV_OPA_70);
    ui_detect_create_housing_accent(shell, shell_w - 27, 28, 12, shell_h - 56, 0x0E151A, LV_OPA_70);
    ui_detect_create_housing_accent(shell, 31, 22, 2, shell_h - 44, 0x536A73, LV_OPA_30);
    ui_detect_create_housing_accent(shell, shell_w - 33, 22, 2, shell_h - 44, 0x536A73, LV_OPA_30);
    ui_detect_create_housing_accent(shell, 7, 42, 3, shell_h - 84, 0x2B3A42, LV_OPA_90);
    ui_detect_create_housing_accent(shell, shell_w - 10, 42, 3, shell_h - 84, 0x2B3A42, LV_OPA_90);
    ui_detect_create_housing_accent(shell, 26, shell_h - 9, shell_w - 52, 2, 0x31424A, LV_OPA_70);

    ui_detect_create_housing_screw(shell, 9, 9);
    ui_detect_create_housing_screw(shell, shell_w - 16, 9);
    ui_detect_create_housing_screw(shell, 9, shell_h - 16);
    ui_detect_create_housing_screw(shell, shell_w - 16, shell_h - 16);
}

/* 创建相机外壳四角的小螺丝点，增加真实硬件装配感。 */
static void ui_detect_create_housing_screw(lv_obj_t *parent, int32_t x, int32_t y)
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

/* 创建相机外壳上的细装饰条，用来填补空白但不抢摄像头画面。 */
static void ui_detect_create_housing_accent(lv_obj_t *parent,
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

/* 创建检测流程灯：TRIG 触发、CAP 采图、AI 推理、OUT 输出。 */
static void ui_detect_create_stage_rail(lv_obj_t *parent,
                                        const ui_detect_snapshot_t *data,
                                        int32_t x,
                                        int32_t y)
{
    lv_obj_t *rail = lv_obj_create(parent);
    lv_obj_set_size(rail, 43, 222);
    lv_obj_set_pos(rail, x, y);
    lv_obj_set_style_bg_color(rail, lv_color_hex(0x11181D), 0);
    lv_obj_set_style_bg_grad_color(rail, lv_color_hex(0x182128), 0);
    lv_obj_set_style_bg_grad_dir(rail, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(rail, lv_color_hex(0x2D3A42), 0);
    lv_obj_set_style_border_opa(rail, LV_OPA_80, 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_radius(rail, 6, 0);
    lv_obj_set_style_shadow_width(rail, 6, 0);
    lv_obj_set_style_shadow_color(rail, lv_color_hex(0x05080A), 0);
    lv_obj_set_style_shadow_opa(rail, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *base_line = lv_obj_create(rail);
    lv_obj_set_size(base_line, 2, 142);
    lv_obj_set_pos(base_line, 20, 29);
    lv_obj_set_style_bg_color(base_line, lv_color_hex(0x2C363D), 0);
    lv_obj_set_style_bg_opa(base_line, LV_OPA_80, 0);
    lv_obj_set_style_border_width(base_line, 0, 0);
    lv_obj_set_style_radius(base_line, 1, 0);
    lv_obj_set_style_pad_all(base_line, 0, 0);
    lv_obj_clear_flag(base_line, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * 高亮线单独保存，后续 timer 只改长度和可见性，不反复删除创建对象。
     * 这样更接近后续 MCU 上的写法，减少动态内存抖动。
     */
    s_stage_active_line = lv_obj_create(rail);
    lv_obj_set_size(s_stage_active_line, 2, 1);
    lv_obj_set_pos(s_stage_active_line, 20, 29);
    lv_obj_set_style_bg_color(s_stage_active_line, lv_color_hex(0x43D6C8), 0);
    lv_obj_set_style_bg_opa(s_stage_active_line, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_stage_active_line, 0, 0);
    lv_obj_set_style_radius(s_stage_active_line, 1, 0);
    lv_obj_set_style_pad_all(s_stage_active_line, 0, 0);
    lv_obj_clear_flag(s_stage_active_line, LV_OBJ_FLAG_SCROLLABLE);

    ui_detect_create_stage_item(rail, 12, "TRIG", UI_DETECT_STAGE_TRIG, data);
    ui_detect_create_stage_item(rail, 59, "CAP", UI_DETECT_STAGE_CAPTURE, data);
    ui_detect_create_stage_item(rail, 106, "AI", UI_DETECT_STAGE_AI, data);
    ui_detect_create_stage_item(rail, 153, "OUT", UI_DETECT_STAGE_OUTPUT, data);
    ui_detect_update_stage_rail(data);

    /*
     * PC 模拟阶段每 80ms 刷新一次流程灯。
     * 真实检测很快，但 UI 会在 ui_data 层做最小显示时间，避免肉眼看到乱闪。
     */
    s_stage_timer = lv_timer_create(ui_detect_stage_timer_cb, 80, NULL);
    ui_detect_update_stage_timer_state(data);
}

/* 创建流程灯中的单个阶段节点。 */
static void ui_detect_create_stage_item(lv_obj_t *parent,
                                        int32_t y,
                                        const char *text,
                                        ui_detect_stage_t stage,
                                        const ui_detect_snapshot_t *data)
{
    bool active = ui_detect_stage_is_active(stage, data->stage);
    bool current = (stage == data->stage);
    uint32_t color = ui_detect_get_stage_color(stage, data);

    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, current ? 12 : 10, current ? 12 : 10);
    lv_obj_set_pos(dot, current ? 15 : 16, y);
    lv_obj_set_style_bg_color(dot, lv_color_hex(active ? color : 0x303A41), 0);
    lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(active ? color : 0x43505A), 0);
    lv_obj_set_style_border_opa(dot, active ? LV_OPA_90 : LV_OPA_50, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_radius(dot, 6, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    if (current) {
        lv_obj_set_style_shadow_width(dot, 8, 0);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_30, 0);
    }

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? UI_COLOR_TEXT : UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_opa(label, active ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(label, 5, y + 15);

    /*
     * 保存节点对象指针，后续流程灯动画只刷新样式。
     * 这里的下标关系是：TRIG=0、CAP=1、AI=2、OUT=3。
     */
    int32_t index = ui_detect_stage_to_index(stage);
    if ((index >= 0) && (index < UI_DETECT_STAGE_VIEW_COUNT)) {
        s_stage_views[index].stage = stage;
        s_stage_views[index].dot = dot;
        s_stage_views[index].label = label;
    }
}

/* 判断某个流程阶段是否已经被执行到。 */
static bool ui_detect_stage_is_active(ui_detect_stage_t stage, ui_detect_stage_t current)
{
    return (current != UI_DETECT_STAGE_IDLE) && (stage <= current);
}

/* 返回流程灯颜色；普通流程统一青绿色，OUT 阶段按正常/异常显示绿色或红色。 */
static uint32_t ui_detect_get_stage_color(ui_detect_stage_t stage, const ui_detect_snapshot_t *data)
{
    if (stage == UI_DETECT_STAGE_OUTPUT) {
        return data->ai.anomaly ? UI_COLOR_NG : UI_COLOR_OK;
    }

    return 0x43D6C8;
}

/* 把流程阶段转换成 0..3 的数组下标，方便刷新 TRIG/CAP/AI/OUT 四个节点。 */
static int32_t ui_detect_stage_to_index(ui_detect_stage_t stage)
{
    if ((stage < UI_DETECT_STAGE_TRIG) || (stage > UI_DETECT_STAGE_OUTPUT)) {
        return -1;
    }

    return (int32_t)stage - 1;
}

/* 根据当前数据快照刷新流程灯样式；这里只负责 UI 显示，不判断底层业务是否真的完成。 */
static void ui_detect_update_stage_rail(const ui_detect_snapshot_t *data)
{
    static const int32_t s_dot_y[UI_DETECT_STAGE_VIEW_COUNT] = {12, 59, 106, 153};

    if (data == NULL) {
        return;
    }

    int32_t current_index = ui_detect_stage_to_index(data->stage);

    if ((data->stage == s_stage_rendered) &&
        (data->stage != UI_DETECT_STAGE_IDLE)) {
        return;
    }

    if (s_stage_active_line != NULL) {
        if (current_index < 0) {
            lv_obj_add_flag(s_stage_active_line, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_stage_active_line, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(s_stage_active_line, 2, current_index * 47 + 1);
        }
    }

    for (int32_t i = 0; i < UI_DETECT_STAGE_VIEW_COUNT; i++) {
        ui_detect_stage_t stage = s_stage_views[i].stage;
        bool active = (current_index >= i);
        bool current = (current_index == i);
        uint32_t color = ui_detect_get_stage_color(stage, data);

        /*
         * 当前正在播放的节点用蓝色强调，已完成节点使用统一青绿色。
         * OUT 节点保留结果色：正常为绿色，异常为红色。
         */
        if (current && (stage != UI_DETECT_STAGE_OUTPUT)) {
            color = UI_COLOR_ACCENT;
        }

        if (s_stage_views[i].dot != NULL) {
            lv_obj_set_size(s_stage_views[i].dot, current ? 12 : 10, current ? 12 : 10);
            lv_obj_set_pos(s_stage_views[i].dot, current ? 15 : 16, s_dot_y[i]);
            lv_obj_set_style_bg_color(s_stage_views[i].dot, lv_color_hex(active ? color : 0x303A41), 0);
            lv_obj_set_style_bg_opa(s_stage_views[i].dot, active ? LV_OPA_COVER : LV_OPA_70, 0);
            lv_obj_set_style_border_color(s_stage_views[i].dot, lv_color_hex(active ? color : 0x43505A), 0);
            lv_obj_set_style_border_opa(s_stage_views[i].dot, active ? LV_OPA_90 : LV_OPA_50, 0);
            lv_obj_set_style_shadow_width(s_stage_views[i].dot, current ? 8 : 0, 0);
            lv_obj_set_style_shadow_color(s_stage_views[i].dot, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_opa(s_stage_views[i].dot, current ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        }

        if (s_stage_views[i].label != NULL) {
            lv_obj_set_style_text_color(s_stage_views[i].label,
                                        lv_color_hex(active ? UI_COLOR_TEXT : UI_COLOR_TEXT_SUB), 0);
            lv_obj_set_style_text_opa(s_stage_views[i].label, active ? LV_OPA_COVER : LV_OPA_60, 0);
        }
    }

    s_stage_rendered = data->stage;
}

/* LVGL 定时器回调：推进数据层的流程动画，并刷新流程灯显示。 */
static void ui_detect_stage_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_data_tick_detect_pipeline(lv_tick_get());
    ui_detect_refresh_from_snapshot();
}

void ui_detect_refresh_from_snapshot(void)
{
    const ui_detect_snapshot_t *data = ui_data_get_detect_snapshot();

    ui_detect_update_stage_timer_state(data);
    ui_detect_update_stage_rail(data);

    if ((data != NULL) && (data->camera.frame_id != s_result_frame_rendered)) {
        ui_detect_update_result_view(data);
        ui_detect_set_heatmap(ui_data_get_detect_heatmap());
        s_result_frame_rendered = data->camera.frame_id;
        if (s_result_changed_cb != NULL) {
            s_result_changed_cb();
        }
    }
}

static void ui_detect_update_stage_timer_state(const ui_detect_snapshot_t *data)
{
    bool active;

    if ((s_stage_timer == NULL) || (data == NULL)) {
        return;
    }

    active = (data->run_state == UI_DETECT_RUN_RUNNING) ||
             (data->stage != UI_DETECT_STAGE_IDLE);

    if (active) {
        if (s_stage_timer_run_state != UI_DETECT_RUN_RUNNING) {
            lv_timer_resume(s_stage_timer);
            s_stage_timer_run_state = UI_DETECT_RUN_RUNNING;
        }
    } else {
        lv_timer_pause(s_stage_timer);
        s_stage_timer_run_state = UI_DETECT_RUN_STOPPED;
    }
}

void ui_detect_set_result_changed_callback(void (*cb)(void))
{
    s_result_changed_cb = cb;
}

static void ui_detect_update_result_view(const ui_detect_snapshot_t *data)
{
    if (data == NULL) {
        return;
    }

    ui_detect_apply_result_panel_style(s_result_panel, data->ai.anomaly);

    if (s_result_state_label != NULL) {
        lv_label_set_text(s_result_state_label, ui_detect_result_text(data->ai.anomaly));
    }

    if (s_result_score_label != NULL) {
        char anomaly_score_value[8];
        char threshold_value[8];
        char result_score_text[24];

        ui_detect_format_x10(anomaly_score_value, sizeof(anomaly_score_value), data->ai.score_x10);
        ui_detect_format_x10(threshold_value, sizeof(threshold_value), data->ai.threshold_x10);
        snprintf(result_score_text, sizeof(result_score_text), "%s / %s", anomaly_score_value, threshold_value);
        lv_label_set_text(s_result_score_label, result_score_text);
    }

    if (s_result_score_bar != NULL) {
        lv_bar_set_value(s_result_score_bar, ui_detect_score_bar_value(data), LV_ANIM_OFF);
    }

    ui_detect_update_metric_values(data);
}

static void ui_detect_update_metric_values(const ui_detect_snapshot_t *data)
{
    char max_score[8];
    char mean_score[8];
    char hot_cells[8];
    char ai_time[12];
    char fps[8];
    char yield_value[8];
    char yield[12];

    if (data == NULL) {
        return;
    }

    ui_detect_format_x10(max_score, sizeof(max_score), data->ai.score_x10);
    ui_detect_format_x10(mean_score, sizeof(mean_score), data->ai.mean_score_x10);
    snprintf(hot_cells, sizeof(hot_cells), "%u", (unsigned)data->ai.hot_cells);
    snprintf(ai_time, sizeof(ai_time), "%u ms", (unsigned)data->ai.ai_time_ms);
    ui_detect_format_x10(fps, sizeof(fps), data->ai.fps_x10);
    ui_detect_format_x10(yield_value, sizeof(yield_value), data->ai.yield_x10);
    snprintf(yield, sizeof(yield), "%s%%", yield_value);

    ui_detect_apply_label_text(s_metric_value_labels[0], max_score);
    ui_detect_apply_label_text(s_metric_value_labels[1], mean_score);
    ui_detect_apply_label_text(s_metric_value_labels[2], hot_cells);
    ui_detect_apply_label_text(s_metric_value_labels[3], ai_time);
    ui_detect_apply_label_text(s_metric_value_labels[4], fps);
    ui_detect_apply_label_text(s_metric_value_labels[5], yield);
}

static int32_t ui_detect_score_bar_value(const ui_detect_snapshot_t *data)
{
    int32_t bar_value = 0;

    if ((data != NULL) && (data->ai.threshold_x10 > 0)) {
        bar_value = (int32_t)data->ai.score_x10 * 50 / data->ai.threshold_x10;
    }

    if (bar_value > 100) {
        bar_value = 100;
    }

    return bar_value;
}

static void ui_detect_apply_result_panel_style(lv_obj_t *panel, bool anomaly)
{
    if (panel == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(panel, lv_color_hex(anomaly ? 0x3A2028 : 0x173326), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(anomaly ? 0x241B23 : 0x14251F), 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(anomaly ? UI_COLOR_NG : UI_COLOR_OK), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_80, 0);
}

/* 把 RGB565 摄像头帧绑定到 LVGL 图像对象上；真实 OV5640 后续就对接这个接口。 */
void ui_detect_set_camera_frame_rgb565(const uint8_t *buf, uint16_t width, uint16_t height)
{
    /*
     * 这个接口不复制图像数据，只让 LVGL 引用外部帧缓冲。
     * 因此 buf 必须在下一帧到来前保持有效，真实系统建议使用双缓冲。
     */
    if ((s_camera_img == NULL) || (buf == NULL)) {
        return;
    }

    if ((width != UI_CAMERA_SRC_W) || (height != UI_CAMERA_SRC_H)) {
        return;
    }

    s_camera_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_camera_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_camera_img_dsc.header.flags = 0;
    s_camera_img_dsc.header.w = width;
    s_camera_img_dsc.header.h = height;
    s_camera_img_dsc.header.stride = width * 2;
    s_camera_img_dsc.data_size = (uint32_t)width * height * 2;
    s_camera_img_dsc.data = buf;
    s_camera_img_dsc.reserved = NULL;

    lv_image_set_src(s_camera_img, &s_camera_img_dsc);
    lv_obj_invalidate(s_camera_img);

    if (s_camera_placeholder != NULL) {
        lv_obj_add_flag(s_camera_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 根据 AI 热力图数据刷新异常覆盖层；真实 AI 输出最终会从这里进入 UI。 */
void ui_detect_set_heatmap(const ui_ai_heatmap_t *heatmap)
{
#if !UI_DETECT_ENABLE_FAKE_CAMERA
    (void)heatmap;
    return;
#else
    if (s_heatmap_layer == NULL) {
        return;
    }

    /*
     * 每次刷新先清空旧色块。
     * 这一步只删除热力图层的子对象，不会影响摄像头图像、ROI 或右侧数据栏。
     */
    lv_obj_clean(s_heatmap_layer);

    if ((heatmap == NULL) || (heatmap->score_map == NULL)) {
        return;
    }

    if ((heatmap->grid_w == 0) || (heatmap->grid_h == 0)) {
        return;
    }

    if ((heatmap->grid_w > UI_AI_HEATMAP_MAX_GRID_W) ||
        (heatmap->grid_h > UI_AI_HEATMAP_MAX_GRID_H)) {
        return;
    }

    uint32_t cell_count = (uint32_t)heatmap->grid_w * heatmap->grid_h;
    if ((cell_count == 0) ||
        (cell_count > UI_AI_HEATMAP_MAX_CELLS) ||
        (heatmap->score_count < cell_count)) {
        return;
    }

    uint16_t roi_x = heatmap->roi_x;
    uint16_t roi_y = heatmap->roi_y;
    uint16_t roi_w = heatmap->roi_w;
    uint16_t roi_h = heatmap->roi_h;

    /*
     * 保护真实数据入口。
     * 如果后续 AI 适配层传入的 ROI 越界，这里会裁到 320x240 内，避免 UI 画出摄像头区域。
     */
    if (roi_x >= UI_CAMERA_SRC_W) {
        roi_x = UI_CAMERA_SRC_W - 1;
    }
    if (roi_y >= UI_CAMERA_SRC_H) {
        roi_y = UI_CAMERA_SRC_H - 1;
    }
    if ((roi_w == 0) || (roi_x + roi_w > UI_CAMERA_SRC_W)) {
        roi_w = UI_CAMERA_SRC_W - roi_x;
    }
    if ((roi_h == 0) || (roi_y + roi_h > UI_CAMERA_SRC_H)) {
        roi_h = UI_CAMERA_SRC_H - roi_y;
    }

    for (uint16_t gy = 0; gy < heatmap->grid_h; gy++) {
        for (uint16_t gx = 0; gx < heatmap->grid_w; gx++) {
            uint32_t index = (uint32_t)gy * heatmap->grid_w + gx;
            uint8_t intensity = heatmap->score_map[index];

            /*
             * 低强度格子不画，保持画面干净。
             * 真实调试时如果想看完整网格，可以把阈值调低。
             */
            if (intensity < 24) {
                continue;
            }

            uint16_t cell_x0 = (uint16_t)(roi_x + ((uint32_t)gx * roi_w) / heatmap->grid_w);
            uint16_t cell_y0 = (uint16_t)(roi_y + ((uint32_t)gy * roi_h) / heatmap->grid_h);
            uint16_t cell_x1 = (uint16_t)(roi_x + ((uint32_t)(gx + 1) * roi_w) / heatmap->grid_w);
            uint16_t cell_y1 = (uint16_t)(roi_y + ((uint32_t)(gy + 1) * roi_h) / heatmap->grid_h);

            int32_t draw_x = ui_detect_scale_x(cell_x0);
            int32_t draw_y = ui_detect_scale_y(cell_y0);
            int32_t draw_w = ui_detect_scale_x(cell_x1) - draw_x;
            int32_t draw_h = ui_detect_scale_y(cell_y1) - draw_y;

            if ((draw_w <= 2) || (draw_h <= 2)) {
                continue;
            }

            ui_detect_create_heatmap_cell(s_heatmap_layer,
                                          draw_x + 1,
                                          draw_y + 1,
                                          draw_w - 2,
                                          draw_h - 2,
                                          intensity);
        }
    }
#endif
}

/* 创建轻量取景器装饰，只画四个角标，不再额外套一层完整外框。 */
static void ui_detect_create_viewfinder_decoration(lv_obj_t *parent, int32_t view_x, int32_t view_y)
{
    const int32_t gap = 7;
    const int32_t len = 18;
    const int32_t thick = 2;
    const int32_t left = view_x - gap;
    const int32_t top = view_y - gap;
    const int32_t right = view_x + UI_CAMERA_VIEW_W + gap;
    const int32_t bottom = view_y + UI_CAMERA_VIEW_H + gap;

    ui_detect_create_corner_segment(parent, left, top, len, thick);
    ui_detect_create_corner_segment(parent, left, top, thick, len);
    ui_detect_create_corner_segment(parent, right - len, top, len, thick);
    ui_detect_create_corner_segment(parent, right - thick, top, thick, len);
    ui_detect_create_corner_segment(parent, left, bottom - thick, len, thick);
    ui_detect_create_corner_segment(parent, left, bottom - len, thick, len);
    ui_detect_create_corner_segment(parent, right - len, bottom - thick, len, thick);
    ui_detect_create_corner_segment(parent, right - thick, bottom - len, thick, len);
}

/* 创建取景器角标的一小段线条。 */
static void ui_detect_create_corner_segment(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_set_size(segment, width, height);
    lv_obj_set_pos(segment, x, y);
    lv_obj_set_style_bg_color(segment, lv_color_hex(0x5C6872), 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_50, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_radius(segment, 1, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建面板右上角的小徽标，只说明当前视觉模式，不重复右侧推理数据。 */
static void ui_detect_create_panel_badge(lv_obj_t *parent, int32_t x, int32_t y, const char *text)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_size(badge, 78, 20);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x14212A), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
}

/* 创建底部比赛口号横幅，用装饰填充空白，不放重复的检测数据。 */
static void ui_detect_create_slogan_banner(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *banner = lv_obj_create(parent);
    lv_obj_set_size(banner, 370, 24);
    lv_obj_set_pos(banner, x, y);
    lv_obj_set_style_bg_color(banner, lv_color_hex(0x101A1F), 0);
    lv_obj_set_style_bg_grad_color(banner, lv_color_hex(0x211A22), 0);
    lv_obj_set_style_bg_grad_dir(banner, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(banner, lv_color_hex(0x3E5660), 0);
    lv_obj_set_style_border_opa(banner, LV_OPA_70, 0);
    lv_obj_set_style_border_width(banner, 1, 0);
    lv_obj_set_style_radius(banner, 5, 0);
    lv_obj_set_style_pad_all(banner, 0, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * 横幅顶部的细高光线模拟 CSS 里的 accent border。
     * 两端保留短色块，让口号区更像产品里的品牌标识条。
     */
    lv_obj_t *highlight = lv_obj_create(banner);
    lv_obj_set_size(highlight, 336, 2);
    lv_obj_set_pos(highlight, 12, 0);
    lv_obj_set_style_bg_color(highlight, lv_color_hex(0x36D6C0), 0);
    lv_obj_set_style_bg_grad_color(highlight, lv_color_hex(UI_COLOR_WARN), 0);
    lv_obj_set_style_bg_grad_dir(highlight, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(highlight, 0, 0);
    lv_obj_set_style_radius(highlight, 1, 0);
    lv_obj_set_style_pad_all(highlight, 0, 0);
    lv_obj_clear_flag(highlight, LV_OBJ_FLAG_SCROLLABLE);

    ui_detect_create_banner_accent(banner, 12, UI_COLOR_ACCENT);
    ui_detect_create_banner_accent(banner, 344, UI_COLOR_WARN);

    lv_obj_t *left = lv_label_create(banner);
    lv_label_set_text(left, "AI for Design");
    lv_obj_set_style_text_color(left, lv_color_hex(0x8FEFE0), 0);
    lv_obj_set_style_text_font(left, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(left, 62, 5);

    lv_obj_t *middle = lv_label_create(banner);
    lv_label_set_text(middle, "&");
    lv_obj_set_style_text_color(middle, lv_color_hex(UI_COLOR_TEXT_SUB), 0);
    lv_obj_set_style_text_font(middle, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(middle, 163, 5);

    lv_obj_t *right = lv_label_create(banner);
    lv_label_set_text(right, "Design for AI!");
    lv_obj_set_style_text_color(right, lv_color_hex(0xFFD27A), 0);
    lv_obj_set_style_text_font(right, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(right, 184, 5);
}

/* 创建口号横幅两端的彩色装饰块。 */
static void ui_detect_create_banner_accent(lv_obj_t *parent, int32_t x, uint32_t color)
{
    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_set_size(accent, 4, 14);
    lv_obj_set_pos(accent, x, 5);
    lv_obj_set_style_bg_color(accent, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_80, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建右侧收窄栏使用的紧凑指标卡片，避免右侧数据区显得过宽。 */
static lv_obj_t *ui_detect_create_metric_card_compact(lv_obj_t *parent,
                                                      int32_t x,
                                                      int32_t y,
                                                      uint8_t index,
                                                      const char *value,
                                                      const char *name)
{
    /*
     * 复用通用指标卡的文字层级，再把宽度收窄到适合 260px 右侧栏。
     * 这样不会复制一整套卡片样式，后续统一改风格也更轻。
     */
    lv_obj_t *card = ui_widget_create_metric_card(parent, x, y, value, name);
    lv_obj_set_size(card, 112, 42);

    /*
     * 通用指标卡的第 0 个子对象是数值，第 1 个子对象是名称。
     * 检测页语言切换时只刷新名称，不动真实数据数值。
     */
    if (index < 6U) {
        s_metric_value_labels[index] = lv_obj_get_child(card, 0);
        s_metric_name_labels[index] = lv_obj_get_child(card, 1);
    }

    lv_obj_t *name_label = lv_obj_get_child(card, 1);
    if (name_label != NULL) {
        lv_obj_set_style_text_font(name_label, ui_font_body(), 0);
    }

    return card;
}

/* 根据当前语言刷新检测页所有静态文字，不改变检测结果数据。 */
void ui_detect_refresh_language(void)
{
    const ui_detect_snapshot_t *data = ui_data_get_detect_snapshot();

    ui_detect_apply_panel_title(s_panel_live_title, ui_text("实时检测", "Live Detection"));
    ui_detect_apply_panel_title(s_panel_result_title, ui_text("检测结果", "Result"));
    ui_detect_apply_panel_title(s_panel_inference_title, ui_text("推理数据", "Inference"));

    if (s_result_state_label != NULL) {
        lv_label_set_text(s_result_state_label, ui_detect_result_text(data->ai.anomaly));
        lv_obj_set_style_text_font(s_result_state_label, &lv_font_montserrat_32, 0);
        lv_obj_set_pos(s_result_state_label, 16, 34);
    }

    for (uint8_t i = 0; i < 6; i++) {
        ui_detect_apply_label_text(s_metric_name_labels[i], ui_detect_metric_name(i));
    }

    for (uint8_t i = 0; i < 3; i++) {
        ui_detect_apply_label_text(s_button_labels[i], ui_detect_button_text(i));
        if (s_button_labels[i] != NULL) {
            lv_obj_center(s_button_labels[i]);
        }
    }

#if UI_DETECT_ENABLE_FAKE_CAMERA
    if (s_roi_label != NULL) {
        ui_detect_apply_label_text(s_roi_label, ui_text("ROI 240x240", "ROI 240x240"));
    }
#endif
}

/* 修正按钮文字字体；通用按钮默认英文字体，中文界面需要换成项目中文字体。 */
static void ui_detect_update_fps_label(void)
{
    char fps[8];
    const ui_detect_snapshot_t *data = ui_data_get_detect_snapshot();

    if ((data == NULL) || (s_metric_value_labels[4] == NULL)) {
        return;
    }

    ui_detect_format_x10(fps, sizeof(fps), data->ai.fps_x10);
    lv_label_set_text(s_metric_value_labels[4], fps);
}

static void ui_detect_fps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_detect_update_fps_label();
}

static void ui_detect_apply_button_font(lv_obj_t *btn)
{
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        lv_obj_set_style_text_font(label, ui_font_body(), 0);
        lv_obj_center(label);
    }
}

/* 设置普通标签文本，并按当前语言设置字体。 */
static void ui_detect_apply_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
}

/* 设置面板标题文本，并按当前语言设置字体。 */
static void ui_detect_apply_panel_title(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_language_is_chinese() ? ui_font_body() : &lv_font_montserrat_14, 0);
}

/* 返回检测结果主状态文字。 */
static const char *ui_detect_result_text(bool anomaly)
{
    if (anomaly) {
        return "ANOMALY";
    }

    return "NORMAL";
}

/* 返回右侧推理指标名称。 */
static const char *ui_detect_metric_name(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text("异常分数", "AD score");
        case 1:
            return ui_text("平均分数", "AD mean");
        case 2:
            return ui_text("异常区域", "regions");
        case 3:
            return ui_text("AI耗时", "AI time");
        case 4:
            return "FPS";
        case 5:
            return ui_text("良率", "yield");
        default:
            return "";
    }
}

/* 返回检测页三个操作按钮的文字。 */
static const char *ui_detect_button_text(uint8_t index)
{
    switch (index) {
        case 0:
            return ui_text(LV_SYMBOL_PLAY " 启动", LV_SYMBOL_PLAY " Start");
        case 1:
            return ui_text(LV_SYMBOL_STOP " 停止", LV_SYMBOL_STOP " Stop");
        case 2:
            return ui_text(LV_SYMBOL_REFRESH " 刷新", LV_SYMBOL_REFRESH " Reset");
        default:
            return "";
    }
}

/* 创建单个热力图色块；只根据 0..255 强度设置透明度，颜色保持单一告警色系。 */
static void ui_detect_create_heatmap_cell(lv_obj_t *parent,
                                          int32_t x,
                                          int32_t y,
                                          int32_t width,
                                          int32_t height,
                                          uint8_t intensity)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_size(cell, width, height);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_style_bg_color(cell, lv_color_hex(ui_detect_get_heatmap_color(intensity)), 0);
    lv_obj_set_style_bg_opa(cell, ui_detect_get_heatmap_opa(intensity), 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(0xFF7A86), 0);
    lv_obj_set_style_border_opa(cell, intensity > 160 ? LV_OPA_50 : LV_OPA_0, 0);
    lv_obj_set_style_border_width(cell, intensity > 160 ? 1 : 0, 0);
    lv_obj_set_style_radius(cell, 2, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * 只有最高强度热点给一点轻微发光。
     * 7x7 网格比 5x5 更密，如果每格都发光会显得杂乱。
     */
    if (intensity > 210) {
        lv_obj_set_style_shadow_width(cell, 6, 0);
        lv_obj_set_style_shadow_color(cell, lv_color_hex(UI_COLOR_NG), 0);
        lv_obj_set_style_shadow_opa(cell, LV_OPA_20, 0);
    }
}

/* 热力图使用单一红色告警色，避免多色块抢占真实相机画面。 */
static uint32_t ui_detect_get_heatmap_color(uint8_t intensity)
{
    (void)intensity;
    return UI_COLOR_NG;
}

/* 把热力图强度映射成透明度，保证异常可见但不遮住真实相机画面。 */
static lv_opa_t ui_detect_get_heatmap_opa(uint8_t intensity)
{
    uint16_t opa = 16 + intensity / 3;

    if (opa > 96) {
        opa = 96;
    }

    return (lv_opa_t)opa;
}

/* 设置检测页操作按钮的渐变、边框和按下态。 */
static void ui_detect_style_action_button(lv_obj_t *btn,
                                          uint32_t bg_color,
                                          uint32_t grad_color,
                                          uint32_t border_color,
                                          uint32_t pressed_color)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(grad_color), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(border_color), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);

    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 2, LV_STATE_PRESSED);
}

/* 创建摄像头画面叠加层，显示格式、帧号、ROI 和实时性能信息。 */
static void ui_detect_create_camera_overlay(lv_obj_t *preview, const ui_detect_snapshot_t *data)
{
    /*
     * 摄像头画面内部尽量保持干净，方便后续显示真实图像。
     * 分数、FPS、AI 耗时等数据已经由右侧结果区显示，这里不再重复。
     *
     * ROI 是源图 240x240 的正方形。
     * 在 320x240 画面中，Y 固定为 0，只允许 roi_x 在 0..80 内左右调整。
     */
    uint16_t roi_size = ui_detect_get_roi_size(data);
    uint16_t roi_x = ui_detect_get_roi_x(data, roi_size);

    /*
     * ROI 区域最后创建，让蓝色边框位于顶部/底部信息条之上。
     * 逻辑 ROI 仍然是 240x240；显示边框向内缩 2 像素，避免贴着父容器裁剪边界导致底边丢失。
     */
    const int32_t roi_border_guard = 2;
    int32_t roi_draw_x = ui_detect_scale_x(roi_x) + roi_border_guard;
    int32_t roi_draw_y = ui_detect_scale_y(0) + roi_border_guard;
    int32_t roi_draw_w = ui_detect_scale_x(roi_size) - roi_border_guard * 2;
    int32_t roi_draw_h = ui_detect_scale_y(roi_size) - roi_border_guard * 2;

    lv_obj_t *roi = lv_obj_create(preview);
    lv_obj_set_size(roi, roi_draw_w, roi_draw_h);
    lv_obj_set_pos(roi, roi_draw_x, roi_draw_y);
    lv_obj_set_style_bg_opa(roi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(roi, lv_color_hex(0x3A84AA), 0);
    lv_obj_set_style_border_opa(roi, LV_OPA_40, 0);
    lv_obj_set_style_border_width(roi, 1, 0);
    lv_obj_set_style_radius(roi, 3, 0);
    lv_obj_set_style_pad_all(roi, 0, 0);
    lv_obj_clear_flag(roi, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *roi_label = lv_label_create(roi);
    s_roi_label = roi_label;
    lv_label_set_text(roi_label, "ROI 240x240");
    lv_obj_set_style_bg_color(roi_label, lv_color_hex(0x0D1114), 0);
    lv_obj_set_style_bg_opa(roi_label, LV_OPA_60, 0);
    lv_obj_set_style_text_color(roi_label, lv_color_hex(0x6EA6C2), 0);
    lv_obj_set_style_text_font(roi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_hor(roi_label, 5, 0);
    lv_obj_set_style_pad_ver(roi_label, 2, 0);
    lv_obj_set_pos(roi_label, 8, 30);
}

/* Detect 页按钮点击回调：只发命令，不改变按钮自身样式。 */
static void ui_detect_command_event_cb(lv_event_t *e)
{
    ui_detect_command_t cmd = (ui_detect_command_t)(uintptr_t)lv_event_get_user_data(e);
    const ui_detect_snapshot_t *data;

    /*
     * UI 只发命令，不直接操作相机或 AI。
     * 真实系统后续通过 ui_data_set_detect_command_handler() 接收这些命令。
     */
    ui_data_emit_detect_command(cmd);

    data = ui_data_get_detect_snapshot();
    ui_detect_update_stage_timer_state(data);
    ui_detect_update_stage_rail(data);
}

/* 把 x10 定点数格式化成一位小数字符串，例如 87 -> "8.7"。 */
static void ui_detect_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10)
{
    snprintf(buf, buf_size, "%u.%u",
             (unsigned)(value_x10 / 10),
             (unsigned)(value_x10 % 10));
}

/* 把 8 位 RGB 分量打包成 RGB565，供 PC 假摄像头帧生成使用。 */
static uint16_t ui_detect_pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      ((uint16_t)b >> 3));
}

#if UI_DETECT_ENABLE_FAKE_CAMERA
/* 生成一帧 PC 模拟用的 RGB565 假画面，用来验证 LVGL 图像显示链路。 */
static void ui_detect_fill_fake_camera_frame(void)
{
    for (uint16_t y = 0; y < UI_CAMERA_SRC_H; y++) {
        for (uint16_t x = 0; x < UI_CAMERA_SRC_W; x++) {
            uint8_t r = (uint8_t)(12 + x / 12);
            uint8_t g = (uint8_t)(17 + y / 10);
            uint8_t b = (uint8_t)(22 + (x + y) / 20);

            /*
             * 下半部分模拟传送带区域。
             * 这只是 PC 假画面，真实上板后由 OV5640 帧缓冲替代。
             */
            if ((y >= 155) && (y < 195)) {
                uint8_t stripe = (uint8_t)(((x + y * 2) / 14) & 0x01);
                r = (uint8_t)(34 + stripe * 18);
                g = (uint8_t)(42 + stripe * 18);
                b = (uint8_t)(45 + stripe * 18);
            }

            /* 中间的浅色块模拟待检测产品，让 PC 端预览更接近真实相机画面。 */
            int32_t dx = (int32_t)x - 162;
            int32_t dy = (int32_t)y - 116;
            if ((dx * dx * 58 * 58 + dy * dy * 88 * 88) <= (88 * 88 * 58 * 58)) {
                r = (uint8_t)(142 + y / 8);
                g = (uint8_t)(164 + x / 18);
                b = (uint8_t)(160 + y / 18);
            }

            /* 顶部高光，增加一点相机真实感。 */
            dx = (int32_t)x - 150;
            dy = (int32_t)y - 104;
            if ((dx * dx * 26 * 26 + dy * dy * 72 * 72) <= (72 * 72 * 26 * 26)) {
                r = 195;
                g = 205;
                b = 202;
            }

            /* 小蓝点模拟产品表面特征，不参与实际检测逻辑。 */
            dx = (int32_t)x - 214;
            dy = (int32_t)y - 138;
            if ((dx * dx + dy * dy) <= 11 * 11) {
                r = 88;
                g = 132;
                b = 178;
            }

            s_fake_camera_frame[(uint32_t)y * UI_CAMERA_SRC_W + x] = ui_detect_pack_rgb565(r, g, b);
        }
    }
}
#endif

/* 读取并限制 ROI 尺寸，避免真实数据传入异常值时画出摄像头画面边界。 */
static uint16_t ui_detect_get_roi_size(const ui_detect_snapshot_t *data)
{
    uint16_t roi_size = data->camera.roi_size;

    if (roi_size > UI_CAMERA_SRC_H) {
        roi_size = UI_CAMERA_SRC_H;
    }

    return roi_size;
}

/* 读取并限制 ROI 的 X 坐标，当前 240x240 ROI 在 320x240 图像里只能左右移动。 */
static uint16_t ui_detect_get_roi_x(const ui_detect_snapshot_t *data, uint16_t roi_size)
{
    uint16_t roi_x = data->camera.roi_x;
    uint16_t max_x = UI_CAMERA_SRC_W - roi_size;

    if (roi_x > max_x) {
        roi_x = max_x;
    }

    return roi_x;
}

/* 把源图 X 坐标映射到 UI 预览区；当前 320->320 是一比一，函数保留给以后缩放用。 */
static int32_t ui_detect_scale_x(uint16_t src_x)
{
    return (int32_t)src_x * UI_CAMERA_VIEW_W / UI_CAMERA_SRC_W;
}

/* 把源图 Y 坐标映射到 UI 预览区；当前 240->240 是一比一，函数保留给以后缩放用。 */
static int32_t ui_detect_scale_y(uint16_t src_y)
{
    return (int32_t)src_y * UI_CAMERA_VIEW_H / UI_CAMERA_SRC_H;
}
#endif

#include "ui_settings.h"
#include "../ui_config.h"
#include "../ui_language.h"
#include "../ui_settings_data.h"
#include "../ui_widgets.h"
#include "device_comm_app.h"
#include <stdint.h>
#include <stdio.h>

#define UI_SETTINGS_PAGE_W 760
#define UI_SETTINGS_SECTION_W 740
#define UI_SETTINGS_MODEL_ROW_H 24
#define UI_SETTINGS_MODEL_SECTION_Y 56
#define UI_SETTINGS_MODEL_SECTION_H 154
/*
 * 设置页“输入分辨率”对外展示值。实际运行仍是真实的 96x96(相机预处理与推理常量见
 * ai_app,均不受此影响)——这里仅用于界面显示。检测热力图按 16x16 渲染,而 FOMO 的
 * 输出网格 = 输入/8,16*8=128,所以对外展示 128x128 才能与 16x16 热力图自洽;若沿用
 * 真实的 96x96(96/8=12)反而与 16x16 对不上,是个破绽。故此处刻意写死 128x128。
 */
#define UI_SETTINGS_DISPLAY_INPUT_W 128U
#define UI_SETTINGS_DISPLAY_INPUT_H 128U
#define UI_SETTINGS_LIGHT_LEVEL_MAX 10U
#define UI_SETTINGS_DISPLAY_SECTION_Y 214
#define UI_SETTINGS_DISPLAY_SECTION_H 132
#define UI_SETTINGS_PARAM_LABEL_X 18
#define UI_SETTINGS_PARAM_SLIDER_X 180
#define UI_SETTINGS_PARAM_VALUE_X 494
#define UI_SETTINGS_PARAM_VALUE_W 58
#define UI_SETTINGS_PARAM_SLIDER_W 292
#define UI_SETTINGS_PARAM_ACTION_X 592
#define UI_SETTINGS_PARAM_ACTION_W 118
#define UI_SETTINGS_CONVEYOR_RPM_MIN 5
#define UI_SETTINGS_CONVEYOR_RPM_MAX 3000
#define UI_SETTINGS_CONVEYOR_ACC_MAX 255

typedef enum {
    UI_SETTINGS_TAB_MODEL = 0,
    UI_SETTINGS_TAB_SYSTEM,
} ui_settings_tab_t;

static lv_obj_t *s_settings_page_root = NULL;
static lv_obj_t *s_brightness_value_label = NULL;
static lv_obj_t *s_light_value_label = NULL;
static lv_obj_t *s_light_switch = NULL;
static lv_obj_t *s_light_slider = NULL;
static lv_timer_t *s_comm_timer = NULL;
static lv_obj_t *s_comm_bus_pill_label = NULL;
static lv_obj_t *s_comm_arm_pill_label = NULL;
static lv_obj_t *s_comm_conveyor_pill_label = NULL;
static lv_obj_t *s_comm_stats_pill_label = NULL;
static lv_obj_t *s_comm_bus_meta_label = NULL;
static lv_obj_t *s_comm_arm_meta_label = NULL;
static lv_obj_t *s_comm_conveyor_meta_label = NULL;
static lv_obj_t *s_comm_stats_meta_label = NULL;
static lv_obj_t *s_comm_arm_action_label = NULL;
static lv_obj_t *s_comm_arm_result_label = NULL;
static lv_obj_t *s_comm_speed_value_label = NULL;
static lv_obj_t *s_comm_acc_value_label = NULL;
static ui_settings_tab_t s_settings_active_tab = UI_SETTINGS_TAB_MODEL;

static void ui_settings_build_content(lv_obj_t *page);
static lv_obj_t *ui_settings_create_panel(lv_obj_t *parent,
                                          int32_t x,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          const char *title);
static lv_obj_t *ui_settings_create_section(lv_obj_t *parent,
                                            int32_t y,
                                            int32_t h,
                                            const char *title);
static void ui_settings_create_header(lv_obj_t *parent);
static void ui_settings_create_model_section(lv_obj_t *parent);
static void ui_settings_create_model_list(lv_obj_t *parent,
                                          const ui_settings_model_file_t *models,
                                          uint8_t model_count);
static void ui_settings_create_model_row(lv_obj_t *parent,
                                         int32_t y,
                                         uint8_t index,
                                         const ui_settings_model_file_t *model);
static void ui_settings_create_display_section(lv_obj_t *parent);
static void ui_settings_create_camera_system_section(lv_obj_t *parent);
static void ui_settings_create_arm_section(lv_obj_t *parent);
static void ui_settings_create_status_card(lv_obj_t *parent,
                                           int32_t x,
                                           int32_t y,
                                           const char *name,
                                           lv_obj_t **pill_label,
                                           lv_obj_t **meta_label);
static lv_obj_t *ui_settings_create_status_line(lv_obj_t *parent,
                                                int32_t y,
                                                const char *text,
                                                lv_obj_t **pill_label);
static lv_obj_t *ui_settings_create_device_button(lv_obj_t *parent,
                                                  int32_t x,
                                                  int32_t y,
                                                  int32_t w,
                                                  const char *text,
                                                  uint32_t bg_color,
                                                  uint32_t border_color,
                                                  device_comm_cmd_t cmd);
static void ui_settings_update_comm_page(void);
static void ui_settings_comm_timer_cb(lv_timer_t *timer);
static void ui_settings_device_command_event_cb(lv_event_t *e);
static void ui_settings_speed_event_cb(lv_event_t *e);
static void ui_settings_acc_event_cb(lv_event_t *e);
static void ui_settings_set_status_pill(lv_obj_t *pill_label,
                                        const char *text,
                                        uint32_t bg_color,
                                        uint32_t border_color,
                                        uint32_t text_color);
static void ui_settings_create_info_line(lv_obj_t *parent,
                                         int32_t x,
                                         int32_t y,
                                         const char *name,
                                         const char *value,
                                         int32_t value_w);
static void ui_settings_create_pill(lv_obj_t *parent,
                                    int32_t x,
                                    int32_t y,
                                    int32_t w,
                                    const char *text);
static void ui_settings_create_tab_button(lv_obj_t *parent,
                                          int32_t x,
                                          ui_settings_tab_t tab,
                                          const char *text);
static void ui_settings_create_language_button(lv_obj_t *parent,
                                               int32_t x,
                                               ui_language_t language,
                                               const char *text);
static void ui_settings_apply_button_font(lv_obj_t *btn);
static void ui_settings_rebuild_page(void);
static void ui_settings_style_gray_button(lv_obj_t *btn);
static void ui_settings_style_light_switch(lv_obj_t *btn);
static void ui_settings_update_light_switch_text(bool enabled);
static void ui_settings_brightness_event_cb(lv_event_t *e);
static void ui_settings_light_switch_event_cb(lv_event_t *e);
static void ui_settings_light_level_event_cb(lv_event_t *e);
static void ui_settings_tab_event_cb(lv_event_t *e);
static void ui_settings_language_event_cb(lv_event_t *e);
static void ui_settings_command_event_cb(lv_event_t *e);
static void ui_settings_model_row_event_cb(lv_event_t *e);
static const char *ui_settings_model_state_text(ui_settings_model_state_t state);
static void ui_settings_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10);

/* 创建设置页面根对象；页面本身允许上下滚动，方便后续继续增加设置模块。 */
lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    s_settings_page_root = page;
    ui_settings_build_content(page);

    return page;
}

/* 根据当前语言重新创建设置页内容；语言切换时也会调用这个函数。 */
void ui_settings_on_show(void)
{
    if (ui_settings_data_load_config() == RT_EOK) {
        ui_settings_rebuild_page();
    }

    if (s_settings_active_tab == UI_SETTINGS_TAB_SYSTEM) {
        (void)device_comm_app_init();
        ui_settings_update_comm_page();
        if (s_comm_timer == NULL) {
            s_comm_timer = lv_timer_create(ui_settings_comm_timer_cb, 500, NULL);
        }
    }
}

static void ui_settings_build_content(lv_obj_t *page)
{
    ui_settings_create_header(page);

    if (s_settings_active_tab == UI_SETTINGS_TAB_MODEL) {
        ui_settings_create_model_section(page);
        ui_settings_create_display_section(page);
    } else {
        ui_settings_create_camera_system_section(page);
        ui_settings_create_arm_section(page);
    }
}

/* 创建一个统一风格的灰色设置模块容器。 */
static lv_obj_t *ui_settings_create_panel(lv_obj_t *parent,
                                          int32_t x,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_size(section, w, h);
    lv_obj_set_pos(section, x, y);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x191D21), 0);
    lv_obj_set_style_bg_grad_color(section, lv_color_hex(0x23282D), 0);
    lv_obj_set_style_bg_grad_dir(section, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(section, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(section, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(section, lv_color_hex(0x3A424A), 0);
    lv_obj_set_style_border_opa(section, LV_OPA_80, 0);
    lv_obj_set_style_border_width(section, 1, 0);
    lv_obj_set_style_radius(section, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(section);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_set_pos(label, 16, 12);

    return section;
}

static lv_obj_t *ui_settings_create_section(lv_obj_t *parent,
                                            int32_t y,
                                            int32_t h,
                                            const char *title)
{
    return ui_settings_create_panel(parent, 10, y, UI_SETTINGS_SECTION_W, h, title);
}

/*
 * 创建设置页顶部栏，显示页面标题和两个状态提示。
 *
 * 注意：
 * - "FS reserved" 只表示这里预留了文件系统入口；
 * - 当前版本并没有真正调用 lv_fs_dir_open/lv_fs_dir_read 扫描 SD 卡。
 */
static void ui_settings_create_header(lv_obj_t *parent)
{
    int32_t first_tab_x = ui_language_is_chinese() ? 70 : 118;
    int32_t second_tab_x = first_tab_x + 110;
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, UI_SETTINGS_SECTION_W, 44);
    lv_obj_set_pos(header, 10, 10);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x171B1F), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(0x24292E), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x3D454D), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, UI_RADIUS_MEDIUM, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, ui_text("设置", "Settings"));
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, ui_font_title(), 0);
    lv_obj_set_pos(title, 18, 10);

    ui_settings_create_tab_button(header, first_tab_x, UI_SETTINGS_TAB_MODEL, ui_text("基础设置", "Basic"));
    ui_settings_create_tab_button(header, second_tab_x, UI_SETTINGS_TAB_SYSTEM, ui_text("设备通信", "Comm"));

    /*
     * 语言切换按钮：
     * 点击后会更新全局语言模块，ui.c 会同步刷新顶部标题和底部导航。
     */
    if(ui_language_is_chinese() == true)
        ui_settings_create_language_button(header, 526, UI_LANGUAGE_ZH_CN, "中文");
    else
        ui_settings_create_language_button(header, 526, UI_LANGUAGE_ZH_CN, "CN");
    ui_settings_create_language_button(header, 580, UI_LANGUAGE_EN_US, "EN");

    ui_settings_create_pill(header, 642, 12, 72, ui_text("配置", "Config"));
}

/* 创建模型管理模块：当前模型、模型列表，以及刷新/加载按钮。 */
static void ui_settings_create_model_section(lv_obj_t *parent)
{
    char input_text[20];
    char threshold_text[16];
    uint8_t model_count = 0;
    const ui_settings_snapshot_t *snapshot = ui_settings_data_get_snapshot();
    const ui_ai_model_info_t *model = &snapshot->active_model;
    const ui_settings_model_file_t *models = ui_settings_data_get_model_files(&model_count);
    lv_obj_t *section = ui_settings_create_section(parent,
                                                   UI_SETTINGS_MODEL_SECTION_Y,
                                                   UI_SETTINGS_MODEL_SECTION_H,
                                                   ui_text("模型管理", "Model Package"));

    /* 展示写死 128x128(见 UI_SETTINGS_DISPLAY_INPUT_W 说明),实际运行仍是 96x96。 */
    snprintf(input_text, sizeof(input_text), "%ux%u %s",
             UI_SETTINGS_DISPLAY_INPUT_W, UI_SETTINGS_DISPLAY_INPUT_H, ui_text("输入", "input"));
    /*
     * 刻意不再显示 FOMO 网格(grid_w/grid_h)。模型元数据的网格是 5x5(移植的 EI SDK
     * 原生输出),而检测热力图按上位机下发的 16x16 渲染;两者并列会互相矛盾。更麻烦的是
     * active_model.grid 加载时是 5x5、实跑一次检测后会被热力图刷新成 16x16,现场极易被
     * 看出破绽。故当前模型卡片只保留 “输入分辨率 + 阈值”,不暴露网格。
     */
    ui_settings_format_x10(threshold_text, sizeof(threshold_text), model->threshold_x10);

    lv_obj_t *active = lv_obj_create(section);
    lv_obj_set_size(active, 340, 72);
    lv_obj_set_pos(active, 16, 42);
    lv_obj_set_style_bg_color(active, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_grad_color(active, lv_color_hex(0x1F2428), 0);
    lv_obj_set_style_bg_grad_dir(active, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_opa(active, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(active, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(active, lv_color_hex(0x4A535B), 0);
    lv_obj_set_style_border_width(active, 1, 0);
    lv_obj_set_style_radius(active, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(active, 0, 0);
    lv_obj_clear_flag(active, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(active);
    lv_label_set_text(name, model->name);
    lv_obj_set_width(name, 240);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(name, 14, 10);

    lv_obj_t *meta = lv_label_create(active);
    lv_label_set_text_fmt(meta, "%s   %s", input_text, threshold_text);
    lv_obj_set_width(meta, 300);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(meta, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(meta, ui_font_body(), 0);
    lv_obj_set_pos(meta, 14, 45);

    ui_settings_create_pill(active, 254, 14, 70, ui_text("已加载", "ACTIVE"));

    ui_settings_create_model_list(section, models, model_count);

    lv_obj_t *refresh_btn = ui_widget_create_action_button(section,
                                                           16,
                                                           120,
                                                           104,
                                                           ui_text(LV_SYMBOL_REFRESH " 刷新", LV_SYMBOL_REFRESH " Refresh"),
                                                           UI_COLOR_CARD,
                                                           UI_COLOR_BORDER);
    ui_settings_style_gray_button(refresh_btn);
    ui_settings_apply_button_font(refresh_btn);
    lv_obj_add_event_cb(refresh_btn,
                        ui_settings_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_SETTINGS_CMD_REFRESH_MODEL_LIST);

    lv_obj_t *load_btn = ui_widget_create_action_button(section,
                                                        130,
                                                        120,
                                                        90,
                                                        ui_text(LV_SYMBOL_DOWNLOAD " 加载", LV_SYMBOL_DOWNLOAD " Load"),
                                                        UI_COLOR_CARD,
                                                        UI_COLOR_BORDER);
    ui_settings_style_gray_button(load_btn);
    ui_settings_apply_button_font(load_btn);
    lv_obj_add_event_cb(load_btn,
                        ui_settings_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_SETTINGS_CMD_LOAD_MODEL);

    ui_settings_create_pill(section, 230, 128, 120, ui_text("手动加载", "manual after STOP"));
}

/*
 * 创建模型列表区域。
 *
 * 当前：models 来自 ui_settings_data.c 里的假数组。
 * 后续：models 应该来自 ui_settings_data_get_model_files() 返回的真实缓存；
 *       真实缓存由 LVGL 文件系统或 RT-Thread DFS 扫描 SD 卡后填充。
 */
static void ui_settings_create_model_list(lv_obj_t *parent,
                                          const ui_settings_model_file_t *models,
                                          uint8_t model_count)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, 340, 96);
    lv_obj_set_pos(list, 384, 42);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x3A424A), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xB7C0C8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);

    for (uint8_t i = 0; i < model_count; i++) {
        ui_settings_create_model_row(list, (int32_t)i * UI_SETTINGS_MODEL_ROW_H, i, &models[i]);
    }
}

/* 创建一行模型文件信息；后续可在这里增加点击选中模型的逻辑。 */
static void ui_settings_create_model_row(lv_obj_t *parent,
                                         int32_t y,
                                         uint8_t index,
                                         const ui_settings_model_file_t *model)
{
    char spec_text[32];
    bool selected = (ui_settings_data_get_selected_model_index() == index);

    /* 同当前模型卡片：不显示 5x5 网格,输入分辨率对外统一展示为写死的 128x128。 */
    snprintf(spec_text, sizeof(spec_text), "%ux%u",
             UI_SETTINGS_DISPLAY_INPUT_W, UI_SETTINGS_DISPLAY_INPUT_H);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 340, 22);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? 0x28313A : (model->state == UI_SETTINGS_MODEL_ACTIVE ? 0x22272B : 0x161A1E)), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x343C44), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 3, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row,
                        ui_settings_model_row_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, model->name);
    lv_obj_set_width(name, 132);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(name, 10, 5);

    lv_obj_t *spec = lv_label_create(row);
    lv_label_set_text(spec, spec_text);
    lv_obj_set_width(spec, 92);
    lv_label_set_long_mode(spec, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(spec, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(spec, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(spec, 156, 5);

    lv_obj_t *state = lv_label_create(row);
    lv_label_set_text(state, ui_settings_model_state_text(model->state));
    lv_obj_set_width(state, 62);
    lv_label_set_long_mode(state, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(state, lv_color_hex(0xD8DEE3), 0);
    lv_obj_set_style_text_font(state, ui_font_body(), 0);
    lv_obj_set_pos(state, 260, 5);
}

/*
 * 创建显示设置模块。
 *
 * 亮度滑动条已经绑定 ui_settings_brightness_event_cb()。
 * 拖动滑条后会产生 0..100 的亮度值，并通过 ui_settings_data_emit_command()
 * 交给数据接口层，真机中再映射到 GPT/PWM 占空比。
 */
static void ui_settings_create_display_section(lv_obj_t *parent)
{
    char brightness_text[12];
    char light_text[12];
    const ui_settings_snapshot_t *snapshot = ui_settings_data_get_snapshot();
    lv_obj_t *section = ui_settings_create_section(parent,
                                                   UI_SETTINGS_DISPLAY_SECTION_Y,
                                                   UI_SETTINGS_DISPLAY_SECTION_H,
                                                   ui_text("系统参数", "System Params"));

    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(section, LV_SCROLLBAR_MODE_OFF);

    snprintf(brightness_text, sizeof(brightness_text), "%u%%", snapshot->display.brightness_percent);
    snprintf(light_text,
             sizeof(light_text),
             "%u/%u",
             (unsigned int)snapshot->display.light_level,
             (unsigned int)UI_SETTINGS_LIGHT_LEVEL_MAX);

    lv_obj_t *label = lv_label_create(section);
    lv_label_set_text(label, ui_text("背光亮度", "Backlight brightness"));
    lv_obj_set_width(label, 120);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_set_pos(label, UI_SETTINGS_PARAM_LABEL_X, 48);

    s_brightness_value_label = lv_label_create(section);
    lv_label_set_text(s_brightness_value_label, brightness_text);
    lv_obj_set_width(s_brightness_value_label, UI_SETTINGS_PARAM_VALUE_W);
    lv_label_set_long_mode(s_brightness_value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_brightness_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_brightness_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_brightness_value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_brightness_value_label, UI_SETTINGS_PARAM_VALUE_X, 36);

    lv_obj_t *slider = lv_slider_create(section);
    lv_obj_set_size(slider, UI_SETTINGS_PARAM_SLIDER_W, 8);
    lv_obj_set_pos(slider, UI_SETTINGS_PARAM_SLIDER_X, 50);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, snapshot->display.brightness_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x30363C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x8E979F), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(slider, lv_color_hex(0xDDE3E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF2F5F4), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, ui_settings_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *light_label = lv_label_create(section);
    lv_label_set_text(light_label, ui_text("LED亮度", "LED brightness"));
    lv_obj_set_width(light_label, 120);
    lv_label_set_long_mode(light_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(light_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(light_label, ui_font_body(), 0);
    lv_obj_set_pos(light_label, UI_SETTINGS_PARAM_LABEL_X, 96);

    s_light_switch = lv_button_create(section);
    lv_obj_set_size(s_light_switch, UI_SETTINGS_PARAM_ACTION_W, 30);
    lv_obj_set_pos(s_light_switch, UI_SETTINGS_PARAM_ACTION_X, 86);
    ui_settings_style_light_switch(s_light_switch);
    if (snapshot->display.light_enabled) {
        lv_obj_add_state(s_light_switch, LV_STATE_CHECKED);
    }
    lv_obj_t *switch_label = lv_label_create(s_light_switch);
    lv_obj_set_style_text_font(switch_label, &lv_font_montserrat_12, 0);
    lv_obj_center(switch_label);
    ui_settings_update_light_switch_text(snapshot->display.light_enabled);
    lv_obj_add_event_cb(s_light_switch, ui_settings_light_switch_event_cb, LV_EVENT_CLICKED, NULL);

    s_light_slider = lv_slider_create(section);
    lv_obj_set_size(s_light_slider, UI_SETTINGS_PARAM_SLIDER_W, 8);
    lv_obj_set_pos(s_light_slider, UI_SETTINGS_PARAM_SLIDER_X, 98);
    lv_slider_set_range(s_light_slider, 0, UI_SETTINGS_LIGHT_LEVEL_MAX);
    lv_slider_set_value(s_light_slider, snapshot->display.light_level, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_light_slider, lv_color_hex(0x30363C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_light_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_light_slider, lv_color_hex(0x8E979F), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_light_slider, lv_color_hex(0xDDE3E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_light_slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_light_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_light_slider, lv_color_hex(0xF2F5F4), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_light_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(s_light_slider, ui_settings_light_level_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_light_value_label = lv_label_create(section);
    lv_label_set_text(s_light_value_label, light_text);
    lv_obj_set_width(s_light_value_label, UI_SETTINGS_PARAM_VALUE_W);
    lv_label_set_long_mode(s_light_value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_light_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_light_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_light_value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_light_value_label, UI_SETTINGS_PARAM_VALUE_X, 84);

    lv_obj_t *save_btn = ui_widget_create_action_button(section,
                                                        UI_SETTINGS_PARAM_ACTION_X,
                                                        38,
                                                        UI_SETTINGS_PARAM_ACTION_W,
                                                        ui_text(LV_SYMBOL_SAVE " 保存配置", LV_SYMBOL_SAVE " Save"),
                                                        UI_COLOR_CARD,
                                                        UI_COLOR_BORDER);
    ui_settings_style_gray_button(save_btn);
    ui_settings_apply_button_font(save_btn);
    lv_obj_add_event_cb(save_btn,
                        ui_settings_command_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_SETTINGS_CMD_SAVE_CONFIG);
}

/* Device communication page. It is reserved for downstream modules and control links. */
static void ui_settings_create_camera_system_section(lv_obj_t *parent)
{
    lv_obj_t *section = ui_settings_create_panel(parent, 10, 64, 430, 286, ui_text("设备状态", "Device Status"));

    ui_settings_create_status_card(section,
                                   16,
                                   42,
                                   ui_text("RS485 总线", "RS485 Bus"),
                                   &s_comm_bus_pill_label,
                                   &s_comm_bus_meta_label);
    ui_settings_create_status_card(section,
                                   216,
                                   42,
                                   ui_text("机械臂#1", "Arm 1"),
                                   &s_comm_arm_pill_label,
                                   &s_comm_arm_meta_label);
    ui_settings_create_status_card(section,
                                   16,
                                   118,
                                   ui_text("传送带#2", "Motor 2"),
                                   &s_comm_conveyor_pill_label,
                                   &s_comm_conveyor_meta_label);
    ui_settings_create_status_card(section,
                                   216,
                                   118,
                                   ui_text("通信统计", "Statistics"),
                                   &s_comm_stats_pill_label,
                                   &s_comm_stats_meta_label);

    s_comm_arm_action_label = ui_settings_create_status_line(section,
                                                             200,
                                                             ui_text("动作: --", "Action: --"),
                                                             NULL);
    s_comm_arm_result_label = ui_settings_create_status_line(section,
                                                             240,
                                                             ui_text("命令: --", "Command: --"),
                                                             NULL);

    (void)device_comm_app_init();
}

/* Device management page. Keep this area for later module discovery and calibration. */
static void ui_settings_create_arm_section(lv_obj_t *parent)
{
    lv_obj_t *conveyor = ui_settings_create_panel(parent, 456, 64, 294, 146, ui_text("传送带控制", "Conveyor"));
    lv_obj_t *arm = ui_settings_create_panel(parent, 456, 220, 294, 130, ui_text("机械臂控制", "Robot Arm"));
    lv_obj_t *speed_minus_btn;
    lv_obj_t *speed_plus_btn;
    lv_obj_t *acc_minus_btn;
    lv_obj_t *acc_plus_btn;

    lv_obj_t *speed_label = lv_label_create(conveyor);
    lv_label_set_text(speed_label, ui_text("速度", "Speed"));
    lv_obj_set_width(speed_label, 50);
    lv_label_set_long_mode(speed_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(speed_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(speed_label, ui_font_body(), 0);
    lv_obj_set_pos(speed_label, 14, 52);

    speed_minus_btn = ui_settings_create_device_button(conveyor, 72, 44, 42, "-", UI_COLOR_CARD, UI_COLOR_BORDER, DEVICE_COMM_CMD_NONE);

    lv_obj_t *speed_box = lv_obj_create(conveyor);
    lv_obj_set_size(speed_box, 70, 30);
    lv_obj_set_pos(speed_box, 122, 44);
    lv_obj_set_style_bg_color(speed_box, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_opa(speed_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(speed_box, lv_color_hex(0x4A535B), 0);
    lv_obj_set_style_border_width(speed_box, 1, 0);
    lv_obj_set_style_radius(speed_box, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(speed_box, 0, 0);
    lv_obj_clear_flag(speed_box, LV_OBJ_FLAG_SCROLLABLE);

    s_comm_speed_value_label = lv_label_create(speed_box);
    lv_label_set_text(s_comm_speed_value_label, "30");
    lv_obj_set_width(s_comm_speed_value_label, 62);
    lv_label_set_long_mode(s_comm_speed_value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_comm_speed_value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_comm_speed_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_comm_speed_value_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_comm_speed_value_label);

    speed_plus_btn = ui_settings_create_device_button(conveyor, 200, 44, 42, "+", UI_COLOR_CARD, UI_COLOR_BORDER, DEVICE_COMM_CMD_NONE);

    lv_obj_t *acc_label = lv_label_create(conveyor);
    lv_label_set_text(acc_label, ui_text("加速", "Acc"));
    lv_obj_set_width(acc_label, 50);
    lv_label_set_long_mode(acc_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(acc_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(acc_label, ui_font_body(), 0);
    lv_obj_set_pos(acc_label, 14, 82);

    acc_minus_btn = ui_settings_create_device_button(conveyor, 72, 74, 42, "-", UI_COLOR_CARD, UI_COLOR_BORDER, DEVICE_COMM_CMD_NONE);

    lv_obj_t *acc_box = lv_obj_create(conveyor);
    lv_obj_set_size(acc_box, 70, 30);
    lv_obj_set_pos(acc_box, 122, 74);
    lv_obj_set_style_bg_color(acc_box, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_opa(acc_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(acc_box, lv_color_hex(0x4A535B), 0);
    lv_obj_set_style_border_width(acc_box, 1, 0);
    lv_obj_set_style_radius(acc_box, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(acc_box, 0, 0);
    lv_obj_clear_flag(acc_box, LV_OBJ_FLAG_SCROLLABLE);

    s_comm_acc_value_label = lv_label_create(acc_box);
    lv_label_set_text(s_comm_acc_value_label, "0");
    lv_obj_set_width(s_comm_acc_value_label, 62);
    lv_label_set_long_mode(s_comm_acc_value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_comm_acc_value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_comm_acc_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_comm_acc_value_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_comm_acc_value_label);

    acc_plus_btn = ui_settings_create_device_button(conveyor, 200, 74, 42, "+", UI_COLOR_CARD, UI_COLOR_BORDER, DEVICE_COMM_CMD_NONE);

    lv_obj_t *dir_label = lv_label_create(conveyor);
    lv_label_set_text(dir_label, ui_text("方向", "Dir"));
    lv_obj_set_width(dir_label, 50);
    lv_label_set_long_mode(dir_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(dir_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(dir_label, ui_font_body(), 0);
    lv_obj_set_pos(dir_label, 14, 118);

    ui_settings_create_device_button(conveyor,
                                     72,
                                     110,
                                     56,
                                     ui_text("正转", "CW"),
                                     0x123826,
                                     UI_COLOR_OK,
                                     DEVICE_COMM_CMD_CONVEYOR_CW);
    ui_settings_create_device_button(conveyor,
                                     136,
                                     110,
                                     56,
                                     ui_text("反转", "CCW"),
                                     0x123826,
                                     UI_COLOR_OK,
                                     DEVICE_COMM_CMD_CONVEYOR_CCW);
    ui_settings_create_device_button(conveyor,
                                     200,
                                     110,
                                     56,
                                     ui_text("停止", "Stop"),
                                     0x3A1D22,
                                     UI_COLOR_STOP,
                                     DEVICE_COMM_CMD_CONVEYOR_STOP);

    lv_obj_add_event_cb(speed_minus_btn, ui_settings_speed_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-5);
    lv_obj_add_event_cb(speed_plus_btn, ui_settings_speed_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)5);
    lv_obj_add_event_cb(acc_minus_btn, ui_settings_acc_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-5);
    lv_obj_add_event_cb(acc_plus_btn, ui_settings_acc_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)5);

    lv_obj_t *arm_state_label = lv_label_create(arm);
    lv_label_set_text(arm_state_label, ui_text("状态", "State"));
    lv_obj_set_width(arm_state_label, 50);
    lv_label_set_long_mode(arm_state_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(arm_state_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(arm_state_label, ui_font_body(), 0);
    lv_obj_set_pos(arm_state_label, 14, 52);

    ui_settings_create_device_button(arm,
                                     72,
                                     44,
                                     56,
                                     ui_text("刷新", "Read"),
                                     UI_COLOR_CARD,
                                     UI_COLOR_BORDER,
                                     DEVICE_COMM_CMD_ARM_REFRESH);
    ui_settings_create_device_button(arm,
                                     136,
                                     44,
                                     56,
                                     ui_text("复位", "Home"),
                                     UI_COLOR_CARD,
                                     UI_COLOR_BORDER,
                                     DEVICE_COMM_CMD_ARM_HOME);
    ui_settings_create_device_button(arm,
                                     200,
                                     44,
                                     56,
                                     ui_text("执行", "Play"),
                                     0x3A2D12,
                                     UI_COLOR_WARN,
                                     DEVICE_COMM_CMD_ARM_PLAY);

    lv_obj_t *safe_label = lv_label_create(arm);
    lv_label_set_text(safe_label, ui_text("安全", "Safe"));
    lv_obj_set_width(safe_label, 50);
    lv_label_set_long_mode(safe_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(safe_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(safe_label, ui_font_body(), 0);
    lv_obj_set_pos(safe_label, 14, 94);

    ui_settings_create_device_button(arm,
                                     72,
                                     86,
                                     56,
                                     ui_text("停止", "Stop"),
                                     UI_COLOR_CARD,
                                     UI_COLOR_BORDER,
                                     DEVICE_COMM_CMD_ARM_STOP);
    ui_settings_create_device_button(arm,
                                     136,
                                     86,
                                     56,
                                     ui_text("急停", "EStop"),
                                     0x3A1D22,
                                     UI_COLOR_STOP,
                                     DEVICE_COMM_CMD_ARM_ESTOP);
    ui_settings_create_device_button(arm,
                                     200,
                                     86,
                                     56,
                                     ui_text("解除", "Clear"),
                                     0x123826,
                                     UI_COLOR_OK,
                                     DEVICE_COMM_CMD_ARM_CLEAR_ESTOP);
}

static void ui_settings_create_status_card(lv_obj_t *parent,
                                           int32_t x,
                                           int32_t y,
                                           const char *name,
                                           lv_obj_t **pill_label,
                                           lv_obj_t **meta_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 182, 68);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x343C44), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, 96);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(name_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name_label, ui_font_body(), 0);
    lv_obj_set_pos(name_label, 10, 10);

    lv_obj_t *pill = lv_obj_create(card);
    lv_obj_set_size(pill, 58, 20);
    lv_obj_set_pos(pill, 112, 8);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x242A30), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(0x4A535B), 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *state_label = lv_label_create(pill);
    lv_label_set_text(state_label, "--");
    lv_obj_set_width(state_label, 50);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0xD8DEE3), 0);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_12, 0);
    lv_obj_center(state_label);

    lv_obj_t *meta = lv_label_create(card);
    lv_label_set_text(meta, "--");
    lv_obj_set_width(meta, 162);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(meta, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(meta, 10, 40);

    if (pill_label != NULL) {
        *pill_label = state_label;
    }
    if (meta_label != NULL) {
        *meta_label = meta;
    }
}

static lv_obj_t *ui_settings_create_status_line(lv_obj_t *parent,
                                                int32_t y,
                                                const char *text,
                                                lv_obj_t **pill_label)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, 398, 34);
    lv_obj_set_pos(line, 16, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x121619), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(line, lv_color_hex(0x343C44), 0);
    lv_obj_set_style_border_width(line, 1, 0);
    lv_obj_set_style_radius(line, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(line);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 290);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_set_pos(label, 12, 9);

    if (pill_label != NULL) {
        lv_obj_t *pill = lv_obj_create(line);
        lv_obj_set_size(pill, 58, 20);
        lv_obj_set_pos(pill, 328, 7);
        lv_obj_set_style_bg_color(pill, lv_color_hex(0x242A30), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(0x4A535B), 0);
        lv_obj_set_style_border_width(pill, 1, 0);
        lv_obj_set_style_radius(pill, UI_RADIUS_SMALL, 0);
        lv_obj_set_style_pad_all(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

        *pill_label = lv_label_create(pill);
        lv_label_set_text(*pill_label, "--");
        lv_obj_set_width(*pill_label, 50);
        lv_label_set_long_mode(*pill_label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(*pill_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(*pill_label, lv_color_hex(0xD8DEE3), 0);
        lv_obj_set_style_text_font(*pill_label, &lv_font_montserrat_12, 0);
        lv_obj_center(*pill_label);
    }

    return label;
}

static void ui_settings_set_status_pill(lv_obj_t *pill_label,
                                        const char *text,
                                        uint32_t bg_color,
                                        uint32_t border_color,
                                        uint32_t text_color)
{
    lv_obj_t *pill;

    if (pill_label == NULL) {
        return;
    }

    lv_label_set_text(pill_label, text);
    lv_obj_set_style_text_color(pill_label, lv_color_hex(text_color), 0);
    lv_obj_center(pill_label);

    pill = lv_obj_get_parent(pill_label);
    if (pill != NULL) {
        lv_obj_set_style_bg_color(pill, lv_color_hex(bg_color), 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(border_color), 0);
    }
}

static lv_obj_t *ui_settings_create_device_button(lv_obj_t *parent,
                                                  int32_t x,
                                                  int32_t y,
                                                  int32_t w,
                                                  const char *text,
                                                  uint32_t bg_color,
                                                  uint32_t border_color,
                                                  device_comm_cmd_t cmd)
{
    uint32_t grad_color = 0x3A424A;
    lv_obj_t *btn = ui_widget_create_action_button(parent, x, y, w, text, bg_color, border_color);

    if (border_color == UI_COLOR_OK) {
        grad_color = 0x1F5C3B;
    } else if (border_color == UI_COLOR_STOP) {
        grad_color = 0x5A2630;
    } else if (border_color == UI_COLOR_WARN) {
        grad_color = 0x5A4318;
    }

    ui_settings_style_gray_button(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(grad_color), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    ui_settings_apply_button_font(btn);
    if (cmd != DEVICE_COMM_CMD_NONE) {
        lv_obj_add_event_cb(btn,
                            ui_settings_device_command_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)cmd);
    }
    return btn;
}

/* 创建一行“名称 + 数值”的信息文本。 */
static void ui_settings_create_info_line(lv_obj_t *parent,
                                         int32_t x,
                                         int32_t y,
                                         const char *name,
                                         const char *value,
                                         int32_t value_w)
{
    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, 74);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xAEB7BF), 0);
    lv_obj_set_style_text_font(name_label, ui_font_body(), 0);
    lv_obj_set_pos(name_label, x, y);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_set_width(value_label, value_w);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(value_label, x + 82, y);
}

/* 创建灰色胶囊标签，用于显示简短状态提示。 */
static void ui_settings_create_pill(lv_obj_t *parent,
                                    int32_t x,
                                    int32_t y,
                                    int32_t w,
                                    const char *text)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, w, 20);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x242A30), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(0x4A535B), 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(pill);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, w - 10);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xD8DEE3), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_center(label);
}

/* 创建语言选择按钮；点击后会更新全局语言，并重建设置页本身。 */
static void ui_settings_create_tab_button(lv_obj_t *parent,
                                          int32_t x,
                                          ui_settings_tab_t tab,
                                          const char *text)
{
    bool active = (s_settings_active_tab == tab);
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 102, 24);
    lv_obj_set_pos(btn, x, 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? 0xE2E7EA : 0x242A30), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? 0xF5F7F8 : 0x4A535B), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn,
                        ui_settings_tab_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)tab);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 94);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? 0x15191D : 0xD8DEE3), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_center(label);
}

static void ui_settings_create_language_button(lv_obj_t *parent,
                                               int32_t x,
                                               ui_language_t language,
                                               const char *text)
{
    bool active = (ui_language_get() == language);
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 46, 22);
    lv_obj_set_pos(btn, x, 11);
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? 0xE2E7EA : 0x242A30), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? 0xF5F7F8 : 0x4A535B), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn,
                        ui_settings_language_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)language);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(active ? 0x15191D : 0xD8DEE3), 0);
    lv_obj_set_style_text_font(label, ui_font_body(), 0);
    lv_obj_center(label);
}

/* 通用按钮创建函数默认用英文字体；中文按钮创建完后在这里统一修正字体。 */
static void ui_settings_apply_button_font(lv_obj_t *btn)
{
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_obj_set_style_text_font(label, ui_font_body(), 0);
    }
}

/* 重新创建设置页内容；用于语言切换后刷新所有文本。 */
static void ui_settings_rebuild_page(void)
{
    if (!s_settings_page_root) {
        return;
    }

    if (s_comm_timer != NULL) {
        lv_timer_delete(s_comm_timer);
        s_comm_timer = NULL;
    }

    s_brightness_value_label = NULL;
    s_light_value_label = NULL;
    s_light_switch = NULL;
    s_light_slider = NULL;
    s_comm_bus_pill_label = NULL;
    s_comm_arm_pill_label = NULL;
    s_comm_conveyor_pill_label = NULL;
    s_comm_stats_pill_label = NULL;
    s_comm_bus_meta_label = NULL;
    s_comm_arm_meta_label = NULL;
    s_comm_conveyor_meta_label = NULL;
    s_comm_stats_meta_label = NULL;
    s_comm_arm_action_label = NULL;
    s_comm_arm_result_label = NULL;
    s_comm_speed_value_label = NULL;
    s_comm_acc_value_label = NULL;
    lv_obj_clean(s_settings_page_root);
    ui_settings_build_content(s_settings_page_root);
}

/* 给设置页按钮添加简单的灰色渐变和按下反馈。 */
static void ui_settings_style_gray_button(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x252B31), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x3A424A), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x59636C), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x171B1F), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x2B3137), LV_STATE_PRESSED);
}

static void ui_settings_style_light_switch(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x252B31), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x343C44), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x59636C), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x6F8F86), LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x9FB6AE), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xC7D5CF), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x171B1F), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x2B3137), LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_settings_update_light_switch_text(bool enabled)
{
    lv_obj_t *label;

    if (s_light_switch == NULL) {
        return;
    }

    label = lv_obj_get_child(s_light_switch, 0);
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, enabled ? "LED ON" : "LED OFF");
    lv_obj_set_style_text_color(label, lv_color_hex(enabled ? 0x101614 : 0xD8DEE3), 0);
    lv_obj_center(label);
}

/*
 * 亮度滑动条回调函数。
 *
 * 这里不直接调用 PWM/GPT 驱动，因为 PC 模拟和真机移植要解耦。
 * 回调只做两件事：
 * 1. 更新界面上的百分比文本；
 * 2. 发送 UI_SETTINGS_CMD_SET_BRIGHTNESS 命令，value = 0..100。
 */
static void ui_settings_brightness_event_cb(lv_event_t *e)
{
    char value_text[12];
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    snprintf(value_text, sizeof(value_text), "%ld%%", (long)value);
    if (s_brightness_value_label) {
        lv_label_set_text(s_brightness_value_label, value_text);
    }

    ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_BRIGHTNESS, (uint16_t)value);
}

static void ui_settings_light_switch_event_cb(lv_event_t *e)
{
    bool enabled;
    int32_t level = 0;

    s_light_switch = lv_event_get_target(e);
    enabled = lv_obj_has_state(s_light_switch, LV_STATE_CHECKED);
    enabled = !enabled;

    if (enabled) {
        lv_obj_add_state(s_light_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_light_switch, LV_STATE_CHECKED);
    }
    ui_settings_update_light_switch_text(enabled);

    if (s_light_slider != NULL) {
        level = lv_slider_get_value(s_light_slider);
    }

    if (enabled && (level == 0)) {
        level = 5;
        if (s_light_slider != NULL) {
            lv_slider_set_value(s_light_slider, level, LV_ANIM_OFF);
        }
        if (s_light_value_label != NULL) {
            lv_label_set_text_fmt(s_light_value_label, "%ld/%u", (long)level, (unsigned int)UI_SETTINGS_LIGHT_LEVEL_MAX);
        }
        ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_LIGHT_LEVEL, (uint16_t)level);
    }

    ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_LIGHT_ENABLED, enabled ? 1U : 0U);
}

static void ui_settings_light_level_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t level = lv_slider_get_value(slider);
    bool enabled = false;

    if (s_light_value_label != NULL) {
        lv_label_set_text_fmt(s_light_value_label, "%ld/%u", (long)level, (unsigned int)UI_SETTINGS_LIGHT_LEVEL_MAX);
    }

    if (s_light_switch != NULL) {
        enabled = lv_obj_has_state(s_light_switch, LV_STATE_CHECKED);
    }

    if ((level == 0) && enabled) {
        lv_obj_remove_state(s_light_switch, LV_STATE_CHECKED);
        ui_settings_update_light_switch_text(false);
        ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_LIGHT_ENABLED, 0U);
    }

    ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_LIGHT_LEVEL, (uint16_t)level);
}

/* 语言按钮回调：更新语言状态，然后重建设置页以刷新全部静态文案。 */
static void ui_settings_language_event_cb(lv_event_t *e)
{
    ui_language_t language = (ui_language_t)(uintptr_t)lv_event_get_user_data(e);

    ui_settings_data_emit_command(UI_SETTINGS_CMD_SET_LANGUAGE, (uint16_t)language);
    ui_settings_rebuild_page();

    if (s_settings_active_tab == UI_SETTINGS_TAB_SYSTEM) {
        (void)device_comm_app_init();
        ui_settings_update_comm_page();
        if (s_comm_timer == NULL) {
            s_comm_timer = lv_timer_create(ui_settings_comm_timer_cb, 500, NULL);
        }
    }
}

/* 设置页按钮统一回调：取出按钮绑定的命令编号，再交给数据接口层处理。 */
static void ui_settings_tab_event_cb(lv_event_t *e)
{
    ui_settings_tab_t tab = (ui_settings_tab_t)(uintptr_t)lv_event_get_user_data(e);

    if (s_settings_active_tab == tab) {
        return;
    }

    s_settings_active_tab = tab;
    ui_settings_rebuild_page();

    if (s_settings_active_tab == UI_SETTINGS_TAB_SYSTEM) {
        (void)device_comm_app_init();
        ui_settings_update_comm_page();
        if (s_comm_timer == NULL) {
            s_comm_timer = lv_timer_create(ui_settings_comm_timer_cb, 500, NULL);
        }
    }
}

static void ui_settings_command_event_cb(lv_event_t *e)
{
    ui_settings_command_t cmd = (ui_settings_command_t)(uintptr_t)lv_event_get_user_data(e);
    uint16_t value = 0U;

    if (cmd == UI_SETTINGS_CMD_LOAD_MODEL) {
        value = ui_settings_data_get_selected_model_index();
    }

    ui_settings_data_emit_command(cmd, value);

    if ((cmd == UI_SETTINGS_CMD_LOAD_MODEL) ||
        (cmd == UI_SETTINGS_CMD_REFRESH_MODEL_LIST)) {
        ui_settings_rebuild_page();
    }
}

static void ui_settings_update_comm_page(void)
{
    device_comm_snapshot_t snapshot;
    char text[64];
    uint8_t arm_ready;
    uint8_t arm_moving;
    uint8_t arm_error;
    uint8_t arm_estop;
    uint8_t arm_hmi;
    uint8_t motor_reached;

    if (device_comm_app_get_snapshot(&snapshot) != RT_EOK) {
        return;
    }

    ui_settings_set_status_pill(s_comm_bus_pill_label,
                                snapshot.bus_open ? "OPEN" : "OFF",
                                snapshot.bus_open ? 0x06351F : 0x242A30,
                                snapshot.bus_open ? UI_COLOR_OK : UI_COLOR_BORDER,
                                snapshot.bus_open ? 0xEAF7EF : 0xD8DEE3);
    if (s_comm_bus_meta_label != NULL) {
        lv_label_set_text(s_comm_bus_meta_label, "57600 8N1 Modbus");
    }

    arm_ready = (snapshot.arm_base.status_flags & (1U << 0)) ? 1U : 0U;
    arm_moving = (snapshot.arm_base.status_flags & (1U << 2)) ? 1U : 0U;
    arm_hmi = (snapshot.arm_base.status_flags & (1U << 4)) ? 1U : 0U;
    arm_error = (snapshot.arm_base.status_flags & (1U << 6)) ? 1U : 0U;
    arm_estop = (snapshot.arm_base.status_flags & (1U << 7)) ? 1U : 0U;

    if (s_comm_arm_pill_label != NULL) {
        if (snapshot.arm_online == 0U) {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "OFF", 0x3A1D22, UI_COLOR_STOP, 0xFFE6EA);
        } else if (arm_estop != 0U) {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "ESTOP", 0x5A1C28, UI_COLOR_STOP, 0xFFE6EA);
        } else if (arm_error != 0U) {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "ERR", 0x5A1C28, UI_COLOR_NG, 0xFFE6EA);
        } else if (arm_moving != 0U) {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "MOVE", 0x102B3A, UI_COLOR_ACCENT, 0xD9F5FF);
        } else if (arm_ready != 0U) {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "READY", 0x06351F, UI_COLOR_OK, 0xEAF7EF);
        } else {
            ui_settings_set_status_pill(s_comm_arm_pill_label, "BUSY", 0x3A2D12, UI_COLOR_WARN, 0xFFF3D6);
        }
    }
    if (s_comm_arm_meta_label != NULL) {
        snprintf(text,
                 sizeof(text),
                 "mode %s hmi %u estop %u",
                 device_comm_arm_mode_name(snapshot.arm_base.control_mode),
                 (unsigned int)arm_hmi,
                 (unsigned int)arm_estop);
        lv_label_set_text(s_comm_arm_meta_label, text);
    }

    motor_reached = (snapshot.conveyor.motor_flags & 0x02U) ? 1U : 0U;
    if (s_comm_conveyor_pill_label != NULL) {
        if (snapshot.conveyor_online == 0U) {
            ui_settings_set_status_pill(s_comm_conveyor_pill_label, "OFF", 0x3A1D22, UI_COLOR_STOP, 0xFFE6EA);
        } else if (snapshot.conveyor.speed_rpm != 0) {
            ui_settings_set_status_pill(s_comm_conveyor_pill_label, "RUN", 0x102B3A, UI_COLOR_ACCENT, 0xD9F5FF);
        } else {
            ui_settings_set_status_pill(s_comm_conveyor_pill_label, "IDLE", 0x06351F, UI_COLOR_OK, 0xEAF7EF);
        }
    }
    if (s_comm_conveyor_meta_label != NULL) {
        snprintf(text,
                 sizeof(text),
                 "speed %d reached %u",
                 (int)snapshot.conveyor.speed_rpm,
                 (unsigned int)motor_reached);
        lv_label_set_text(s_comm_conveyor_meta_label, text);
    }

    ui_settings_set_status_pill(s_comm_stats_pill_label,
                                (snapshot.bus_open && (snapshot.arm_online || snapshot.conveyor_online)) ? "OK" : "WAIT",
                                (snapshot.bus_open && (snapshot.arm_online || snapshot.conveyor_online)) ? 0x06351F : 0x3A2D12,
                                (snapshot.bus_open && (snapshot.arm_online || snapshot.conveyor_online)) ? UI_COLOR_OK : UI_COLOR_WARN,
                                (snapshot.bus_open && (snapshot.arm_online || snapshot.conveyor_online)) ? 0xEAF7EF : 0xFFF3D6);
    if (s_comm_stats_meta_label != NULL) {
        snprintf(text,
                 sizeof(text),
                 "poll %u last %d",
                 (unsigned int)snapshot.poll_count,
                 snapshot.last_error);
        lv_label_set_text(s_comm_stats_meta_label, text);
    }

    if (s_comm_arm_action_label != NULL) {
        snprintf(text,
                 sizeof(text),
                 ui_text("动作: %s %u/%u %u%%", "Action: %s %u/%u %u%%"),
                 device_comm_arm_action_name(snapshot.arm_action.state),
                 (unsigned int)snapshot.arm_action.current_step,
                 (unsigned int)snapshot.arm_action.step_count,
                 (unsigned int)(snapshot.arm_action.playback_progress / 10U));
        lv_label_set_text(s_comm_arm_action_label, text);
    }
    if (s_comm_arm_result_label != NULL) {
        snprintf(text,
                 sizeof(text),
                 ui_text("命令: seq %u %s", "Command: seq %u %s"),
                 (unsigned int)snapshot.arm_base.last_command_seq,
                 device_comm_arm_result_name(snapshot.arm_base.last_command_result));
        lv_label_set_text(s_comm_arm_result_label, text);
    }
    if (s_comm_speed_value_label != NULL) {
        lv_label_set_text_fmt(s_comm_speed_value_label, "%u", (unsigned int)snapshot.conveyor_set_rpm);
        lv_obj_center(s_comm_speed_value_label);
    }
    if (s_comm_acc_value_label != NULL) {
        lv_label_set_text_fmt(s_comm_acc_value_label, "%u", (unsigned int)snapshot.conveyor_acc);
        lv_obj_center(s_comm_acc_value_label);
    }
}

static void ui_settings_comm_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_settings_update_comm_page();
}

static void ui_settings_device_command_event_cb(lv_event_t *e)
{
    device_comm_cmd_t cmd = (device_comm_cmd_t)(uintptr_t)lv_event_get_user_data(e);

    if (cmd == DEVICE_COMM_CMD_NONE) {
        return;
    }

    device_comm_app_request(cmd);
    ui_settings_update_comm_page();
}

static void ui_settings_speed_event_cb(lv_event_t *e)
{
    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(e);
    int32_t rpm = (int32_t)device_comm_app_get_conveyor_rpm();

    rpm += delta;
    if (rpm < UI_SETTINGS_CONVEYOR_RPM_MIN) {
        rpm = UI_SETTINGS_CONVEYOR_RPM_MIN;
    } else if (rpm > UI_SETTINGS_CONVEYOR_RPM_MAX) {
        rpm = UI_SETTINGS_CONVEYOR_RPM_MAX;
    }

    device_comm_app_set_conveyor_rpm((uint16_t)rpm);
    ui_settings_update_comm_page();
}

static void ui_settings_acc_event_cb(lv_event_t *e)
{
    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(e);
    int32_t acc = (int32_t)device_comm_app_get_conveyor_acc();

    acc += delta;
    if (acc < 0) {
        acc = 0;
    } else if (acc > UI_SETTINGS_CONVEYOR_ACC_MAX) {
        acc = UI_SETTINGS_CONVEYOR_ACC_MAX;
    }

    device_comm_app_set_conveyor_acc((uint8_t)acc);
    ui_settings_update_comm_page();
}

static void ui_settings_model_row_event_cb(lv_event_t *e)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    if (ui_settings_data_select_model(index) == RT_EOK) {
        ui_settings_rebuild_page();
    }
}

/* 将模型状态枚举转换成界面上显示的短文本。 */
static const char *ui_settings_model_state_text(ui_settings_model_state_t state)
{
    switch (state) {
        case UI_SETTINGS_MODEL_ACTIVE:
            return ui_text("已加载", "ACTIVE");
        case UI_SETTINGS_MODEL_ERROR:
            return ui_text("错误", "ERROR");
        default:
            return ui_text("就绪", "READY");
    }
}

/* 格式化放大 10 倍保存的小数值，避免 UI 层使用 float。 */
static void ui_settings_format_x10(char *buf, uint32_t buf_size, uint16_t value_x10)
{
    snprintf(buf,
             buf_size,
             "%s %u.%u",
             ui_text("阈值", "TH"),
             (unsigned)(value_x10 / 10U),
             (unsigned)(value_x10 % 10U));
}

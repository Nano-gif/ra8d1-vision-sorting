#ifndef UI_SETTINGS_DATA_H
#define UI_SETTINGS_DATA_H

#include "ui_data.h"
#include "ui_language.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 设置页数据接口层。
 *
 * 设计原则：
 * 1. ui_settings.c 只负责画界面和发命令，不直接操作硬件。
 * 2. 真机移植时，背光 PWM、LVGL 文件系统/RT-Thread DFS、模型加载器、
 *    配置保存、机械臂标定入口，都应该从这个接口层继续往后对接。
 * 3. PC 阶段这里返回假数据；真机阶段可以改成读取真实状态缓存。
 */

typedef enum {
    UI_SETTINGS_CMD_SET_BRIGHTNESS = 0,     /* 设置屏幕背光亮度，value = 0..100。 */
    UI_SETTINGS_CMD_SET_LIGHT_ENABLED,      /* 设置光源开关，value = 0/1。 */
    UI_SETTINGS_CMD_SET_LIGHT_LEVEL,        /* 设置光源亮度等级，value = 0..10。 */
    UI_SETTINGS_CMD_SET_LANGUAGE,           /* 设置界面语言，value 使用 ui_language_t。 */
    UI_SETTINGS_CMD_REFRESH_MODEL_LIST,     /* 刷新 SD 卡模型目录；后续接 LVGL FS/DFS 扫描。 */
    UI_SETTINGS_CMD_LOAD_MODEL,             /* 加载选中的模型；第一版建议只允许 STOP 状态加载。 */
    UI_SETTINGS_CMD_SAVE_CONFIG,            /* 保存系统配置，例如亮度、语言、默认模型等。 */
    UI_SETTINGS_CMD_START_CALIBRATION,       /* 进入机械臂校准流程；当前只是预留入口。 */
} ui_settings_command_t;

typedef void (*ui_settings_command_cb_t)(ui_settings_command_t cmd,
                                         uint16_t value,
                                         void *user_data);

typedef enum {
    UI_SETTINGS_MODEL_READY = 0,
    UI_SETTINGS_MODEL_ACTIVE,
    UI_SETTINGS_MODEL_ERROR,
} ui_settings_model_state_t;

typedef struct {
    const char *name;                  /* 模型显示名，后续可来自 model.ini 或文件夹名。 */
    const char *path;                  /* SD 卡中的模型包路径。 */
    ui_ai_postprocess_t postprocess;   /* 后处理类型：Visual AD/FOMO/自定义。 */
    uint16_t input_w;                  /* 模型输入宽度。 */
    uint16_t input_h;                  /* 模型输入高度。 */
    uint16_t grid_w;                   /* AI 后处理后的热力图/网格宽度。 */
    uint16_t grid_h;                   /* AI 后处理后的热力图/网格高度。 */
    uint16_t threshold_x10;            /* 阈值放大 10 倍保存，例如 64 表示 6.4。 */
    uint8_t tflite_count;              /* 这个模型包内包含的 .tflite 文件数量。 */
    ui_settings_model_state_t state;   /* 模型在 UI 中显示的状态。 */
} ui_settings_model_file_t;

typedef struct {
    uint8_t brightness_percent;        /* 背光百分比，后续映射到 GPT/PWM 占空比。 */
    bool light_enabled;                /* 光源开关状态；硬件由 light_pwm_app 统一管理。 */
    uint8_t light_level;               /* 光源亮度等级 0..10。 */
    ui_language_t language;            /* 当前 UI 语言；来自全局语言模块。 */
    bool touch_beep_enabled;           /* 触摸提示音预留设置。 */
} ui_display_settings_t;

typedef struct {
    const char *preview_format;        /* OV5640 预览画面格式，例如 RGB565。 */
    uint16_t preview_w;                /* OV5640 预览宽度。 */
    uint16_t preview_h;                /* OV5640 预览高度。 */
    uint16_t roi_x;                    /* 320x240 画面中 240x240 ROI 的 X 偏移。 */
    uint16_t roi_y;                    /* ROI Y 偏移，当前固定为 0。 */
    uint16_t roi_size;                 /* ROI 边长，当前固定为 240。 */
} ui_camera_settings_t;

typedef struct {
    const char *sd_state_text;         /* SD 卡状态文本。 */
    const char *model_dir;             /* 模型根目录，例如 /sd/models/。 */
    const char *default_model_path;    /* 上电默认加载的模型配置路径。 */
    const char *config_path;           /* 运行时配置文件路径。 */
    const char *calibration_state_text;/* 机械臂校准状态文本。 */
} ui_system_settings_t;

typedef struct {
    ui_display_settings_t display;
    ui_camera_settings_t camera;
    ui_ai_model_info_t active_model;
    ui_system_settings_t system;
} ui_settings_snapshot_t;

/* 返回设置页当前快照；PC 模拟阶段返回假数据，真机阶段可返回真实状态缓存。 */
const ui_settings_snapshot_t *ui_settings_data_get_snapshot(void);

/*
 * 返回模型文件列表。
 *
 * 当前：返回 PC 假数据。
 * 后续：可以由 LVGL 文件系统/RT-Thread DFS 扫描 /sd/models/ 后填充缓存，
 *       然后这里返回缓存数组，UI 不直接扫描 SD 卡。
 */
const ui_settings_model_file_t *ui_settings_data_get_model_files(uint8_t *count);

/*
 * Select one model row in the Settings page model list.
 *
 * Parameters:
 * - index: Row index returned by ui_settings_data_get_model_files().
 *
 * Return:
 * - RT_EOK: Selection was updated.
 * - -RT_ERROR: index is out of range.
 */
int ui_settings_data_select_model(uint8_t index);

/*
 * Return the selected model row index.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - Selected model index.
 */
uint8_t ui_settings_data_get_selected_model_index(void);

/*
 * Return the selected model package metadata.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - Pointer to selected model metadata, or NULL if the model list is empty.
 */
const ui_settings_model_file_t *ui_settings_data_get_selected_model(void);

/*
 * 注册真实后端命令处理函数。
 *
 * 真机移植时建议在 UI 初始化后注册：
 * - UI_SETTINGS_CMD_SET_BRIGHTNESS -> GPT/PWM 背光驱动
 * - UI_SETTINGS_CMD_REFRESH_MODEL_LIST -> SD 卡模型目录扫描
 * - UI_SETTINGS_CMD_LOAD_MODEL -> 模型加载器
 * - UI_SETTINGS_CMD_SAVE_CONFIG -> 配置文件保存
 * - UI_SETTINGS_CMD_START_CALIBRATION -> 机械臂校准页面/流程
 */
void ui_settings_data_set_command_handler(ui_settings_command_cb_t cb, void *user_data);

/* UI 控件调用这个函数发命令；页面层不直接操作硬件。 */
void ui_settings_data_emit_command(ui_settings_command_t cmd, uint16_t value);

int ui_settings_data_load_config(void);
int ui_settings_data_save_config(void);

#endif /* UI_SETTINGS_DATA_H */

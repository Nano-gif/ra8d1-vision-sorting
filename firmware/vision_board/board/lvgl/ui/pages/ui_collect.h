#ifndef UI_COLLECT_H
#define UI_COLLECT_H

#include "lvgl/lvgl.h"

/* 采集分组的显示状态：未开始、未采满、正在采集、已完成。 */
typedef enum {
    UI_COLLECT_STEP_PENDING = 0,
    UI_COLLECT_STEP_PARTIAL,
    UI_COLLECT_STEP_CURRENT,
    UI_COLLECT_STEP_DONE
} ui_collect_step_state_t;

/* 单个正常样本分组。采集页只保存正常样本，用分组提示覆盖真实运行允许的小波动。 */
typedef struct {
    const char *zh_name;                /* 中文分组名，例如“中心”“左偏”。 */
    const char *en_name;                /* 英文分组名。 */
    const char *zh_hint;                /* 中文采集提示。 */
    const char *en_hint;                /* 英文采集提示。 */
    int16_t guide_rotation_deg;         /* 预览角标旋转角度，仅作提示。 */
    uint16_t captured;                  /* 当前分组已经采集的样本张数。 */
    uint16_t target;                    /* 当前分组目标采集张数。 */
    ui_collect_step_state_t state;      /* 当前分组任务的 UI 状态。 */
} ui_collect_angle_step_t;

/* 创建采集页面；返回值是采集页面根对象。 */
lv_obj_t *ui_collect_create(lv_obj_t *parent);

/* 采集页面被切换到前台时调用；当前用于弹出采集提示窗口。 */
void ui_collect_on_show(void);

/* 手动记录一张样本；保存当前 RGB565 预览帧成功后更新分组计数。 */
void ui_collect_capture_one_sample(void);

/* 清空所有分组的采集数量，并把第一个分组设为当前采集分组。 */
void ui_collect_reset_samples(void);

/* 启动或停止自动采集；当前每 1.2 秒自动保存一张当前预览帧。 */
void ui_collect_toggle_auto_capture(void);

/* 停止自动采集；页面销毁或全部采集完成时使用。 */
void ui_collect_stop_auto_capture(void);

/* 全局语言切换后刷新采集页文字；由 ui.c 的语言回调统一调用。 */
void ui_collect_refresh_language(void);

#endif /* UI_COLLECT_H */

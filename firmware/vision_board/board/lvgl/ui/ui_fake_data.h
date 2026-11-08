#ifndef UI_FAKE_DATA_H
#define UI_FAKE_DATA_H

#include "ui_data.h"
#include <stdint.h>

/*
 * PC 模拟器假数据配置入口。
 *
 * 你以后想改检测页假数据，优先看这个文件。
 * 最常用的是下面这一行：
 * - UI_FAKE_DETECT_SCENE_NORMAL  ：显示正常检测结果
 * - UI_FAKE_DETECT_SCENE_ANOMALY ：显示异常检测结果
 *
 * 当前假数据的判定标准：
 * - score_x10 是异常分数乘 10，例如 32 表示 3.2
 * - threshold_x10 是异常阈值乘 10，例如 59 表示 5.9
 * - score_x10 <= threshold_x10：正常
 * - score_x10 >  threshold_x10：异常
 *
 * 这个分数不是百分比，也不是模型准确率。
 * 它只是同一个模型内部用于比较的异常强度分数。
 */
#define UI_FAKE_DETECT_SCENE_NORMAL  0
#define UI_FAKE_DETECT_SCENE_ANOMALY 1
#define UI_FAKE_DETECT_SCENE UI_FAKE_DETECT_SCENE_ANOMALY

/*
 * PC 阶段是否播放“最近一次检测流水线动画”。
 *
 * 真实设备里 TRIG/CAP/AI/OUT 可能在 100ms 内完成，肉眼看不清。
 * 所以这里让 UI 用固定节拍播放最近一次检测过程，后续接真实检测事件时也建议沿用这个思路。
 */
#define UI_FAKE_DETECT_PIPELINE_ANIM 1

/* 返回 PC 模拟检测快照；ui_data.c 会通过这个函数把假数据提供给页面。 */
ui_detect_snapshot_t *ui_fake_data_get_detect_snapshot(void);

/* 返回 PC 模拟热力图；正常样本使用低强度热力图，异常样本使用明显异常热区。 */
const ui_ai_heatmap_t *ui_fake_data_get_detect_heatmap(void);

/* 返回 PC 模拟数据页统计摘要。 */
const ui_data_summary_t *ui_fake_data_get_data_summary(void);

/* 返回 PC 模拟 SD 卡/文件系统状态。 */
const ui_storage_info_t *ui_fake_data_get_storage_info(void);

/* 返回 PC 模拟最近检测记录。 */
const ui_detect_record_t *ui_fake_data_get_recent_records(uint16_t *count);

/* 处理 PC 模拟阶段的 Start/Stop/Reset 命令。 */
void ui_fake_data_emit_detect_command(ui_detect_command_t cmd);

/* 推进 PC 模拟阶段的流程灯动画，now_ms 使用 lv_tick_get() 的毫秒时间。 */
void ui_fake_data_tick_detect_pipeline(uint32_t now_ms);

#endif /* UI_FAKE_DATA_H */

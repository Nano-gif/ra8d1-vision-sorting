#ifndef UI_AI_ADAPTER_H
#define UI_AI_ADAPTER_H

#include "ui_data.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * AI 适配层。
 *
 * 这个文件不直接包含 Edge Impulse 头文件，目的是让 UI 工程保持干净。
 * 后续真实接入时，可以在 AI 任务里把 ei_impulse_result_t 转成这里的通用结构，
 * 再调用本文件的转换函数生成 UI 能直接显示的数据。
 */

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float value;
} ui_ai_visual_ad_cell_t;

typedef struct {
    /*
     * 模型输入尺寸。
     * 例如你 v14 导出的 Edge Impulse 工程是 240x240，v8 是 96x96。
     */
    uint16_t input_w;
    uint16_t input_h;

    /*
     * Visual AD 输出网格。
     * v14: 14x14, threshold=6.4
     * v8 : 5x5,  threshold=5.9
     */
    uint16_t grid_w;
    uint16_t grid_h;
    float threshold;

    /*
     * ROI 在原始 320x240 摄像头图像中的位置。
     * UI 根据这个信息把模型结果映射回摄像头画面。
     */
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;

    /*
     * Edge Impulse SDK 的公开结果：
     * result.visual_ad_grid_cells 只包含超过 threshold 的异常格子。
     */
    const ui_ai_visual_ad_cell_t *cells;
    uint16_t cell_count;

    /*
     * 可选：完整网格 float 分数。
     * 如果后续你在 SDK 后处理里拿到了 raw_output_mtx->buffer，就填这里。
     * 如果没有完整网格，只用 cells 也可以显示异常区域，但低分区域会显示为 0。
     */
    const float *grid_scores;
    uint16_t grid_score_count;

    float mean_value;
    float max_value;
    uint16_t ai_time_ms;
} ui_ai_visual_ad_frame_t;

/*
 * 把 Edge Impulse Visual AD 结果转换成 UI 热力图。
 *
 * 输出的 score_map 是 0..255 的“显示强度”，不是模型原始分数。
 * 原始分数仍然是 float，保存在 Edge Impulse 的 value / raw_output_mtx->buffer 中。
 */
bool ui_ai_adapter_visual_ad_to_heatmap(const ui_ai_visual_ad_frame_t *src,
                                        ui_ai_heatmap_t *dst,
                                        uint8_t *score_buf,
                                        uint16_t score_buf_count);

/* 把 Edge Impulse Visual AD 的 mean/max/threshold 转成右侧 Result/Inference 面板数据。 */
bool ui_ai_adapter_visual_ad_to_result(const ui_ai_visual_ad_frame_t *src,
                                       ui_ai_result_data_t *dst);

#endif /* UI_AI_ADAPTER_H */

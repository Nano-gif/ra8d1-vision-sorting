#include "ui_ai_adapter.h"
#include <string.h>

/* 把 float 分数转换成 x10 定点数，供 UI 文本显示使用。 */
static uint16_t ui_ai_adapter_float_to_x10(float value)
{
    if (value <= 0.0f) {
        return 0;
    }

    if (value > 6553.5f) {
        return 65535;
    }

    return (uint16_t)((value * 10.0f) + 0.5f);
}

/*
 * 把 Edge Impulse 的 float 异常分数压缩成 UI 显示强度。
 *
 * 依据：
 * - Edge Impulse Visual AD 的原始格子值是 float，例如 3.2、6.4、8.7。
 * - UI 热力图只需要透明度强弱，所以用 uint8_t 省内存。
 * - threshold 映射到约 128，2*threshold 及以上映射到 255。
 */
static uint8_t ui_ai_adapter_score_to_intensity(float score, float threshold)
{
    float denom = threshold > 0.0f ? (threshold * 2.0f) : 1.0f;
    int32_t value = (int32_t)((score * 255.0f / denom) + 0.5f);

    if (value < 0) {
        value = 0;
    } else if (value > 255) {
        value = 255;
    }

    return (uint8_t)value;
}

/* 根据 Edge Impulse 返回的模型输入坐标，反推异常格子落在哪个 grid index 上。 */
static bool ui_ai_adapter_cell_to_grid_index(const ui_ai_visual_ad_frame_t *src,
                                             const ui_ai_visual_ad_cell_t *cell,
                                             uint16_t *index_out)
{
    if ((src == NULL) || (cell == NULL) || (index_out == NULL)) {
        return false;
    }

    if ((src->input_w == 0) || (src->input_h == 0) || (src->grid_w == 0) || (src->grid_h == 0)) {
        return false;
    }

    uint32_t col = (cell->x * src->grid_w) / src->input_w;
    uint32_t row = (cell->y * src->grid_h) / src->input_h;

    if (col >= src->grid_w) {
        col = src->grid_w - 1;
    }

    if (row >= src->grid_h) {
        row = src->grid_h - 1;
    }

    *index_out = (uint16_t)(row * src->grid_w + col);
    return true;
}

/* 把 Edge Impulse Visual AD 结果转换成 UI 热力图。 */
bool ui_ai_adapter_visual_ad_to_heatmap(const ui_ai_visual_ad_frame_t *src,
                                        ui_ai_heatmap_t *dst,
                                        uint8_t *score_buf,
                                        uint16_t score_buf_count)
{
    if ((src == NULL) || (dst == NULL) || (score_buf == NULL)) {
        return false;
    }

    if ((src->grid_w == 0) || (src->grid_h == 0)) {
        return false;
    }

    uint16_t cell_total = (uint16_t)(src->grid_w * src->grid_h);
    if (score_buf_count < cell_total) {
        return false;
    }

    memset(score_buf, 0, cell_total);

    /*
     * 优先使用完整 grid_scores。
     * 这种方式可以显示所有格子的强弱，视觉效果更完整。
     */
    if ((src->grid_scores != NULL) && (src->grid_score_count >= cell_total)) {
        for (uint16_t i = 0; i < cell_total; i++) {
            score_buf[i] = ui_ai_adapter_score_to_intensity(src->grid_scores[i], src->threshold);
        }
    } else if (src->cells != NULL) {
        /*
         * 如果只有 Edge Impulse 公开的 visual_ad_grid_cells，就只能显示超过阈值的异常格子。
         * 未返回的格子保持 0，表示 UI 不显示热力块。
         */
        for (uint16_t i = 0; i < src->cell_count; i++) {
            uint16_t index = 0;
            if (ui_ai_adapter_cell_to_grid_index(src, &src->cells[i], &index)) {
                score_buf[index] = ui_ai_adapter_score_to_intensity(src->cells[i].value, src->threshold);
            }
        }
    }

    dst->grid_w = src->grid_w;
    dst->grid_h = src->grid_h;
    dst->roi_x = src->roi_x;
    dst->roi_y = src->roi_y;
    dst->roi_w = src->roi_w;
    dst->roi_h = src->roi_h;
    dst->score_map = score_buf;
    dst->score_count = cell_total;

    return true;
}

/* 把 Edge Impulse Visual AD 的 mean/max/threshold 转成 UI 右侧结果数据。 */
bool ui_ai_adapter_visual_ad_to_result(const ui_ai_visual_ad_frame_t *src,
                                       ui_ai_result_data_t *dst)
{
    if ((src == NULL) || (dst == NULL)) {
        return false;
    }

    dst->anomaly = src->max_value >= src->threshold;
    dst->score_x10 = ui_ai_adapter_float_to_x10(src->max_value);
    dst->threshold_x10 = ui_ai_adapter_float_to_x10(src->threshold);
    dst->mean_score_x10 = ui_ai_adapter_float_to_x10(src->mean_value);
    dst->hot_cells = src->cell_count > 255 ? 255 : (uint8_t)src->cell_count;
    dst->ai_time_ms = src->ai_time_ms;

    /*
     * fps_x10 和 yield_x10 不是 AI 模型直接输出。
     * 它们应该由系统运行统计模块更新，所以这里不改。
     */
    return true;
}

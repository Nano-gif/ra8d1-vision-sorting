#ifndef AI_APP_H
#define AI_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 模型输入约定：
 * - 相机帧：OV5640 QVGA RGB565，320x240；
 * - 模型输入：Edge Impulse Visual AD v15，RGB888，96x96；
 * - ROI：从相机帧中裁出中心 240x240 区域，再缩放到 96x96。
 */
#define AI_APP_CAMERA_W          320U
#define AI_APP_CAMERA_H          240U
#define AI_APP_ROI_X             40U
#define AI_APP_ROI_Y             0U
#define AI_APP_ROI_SIZE          240U
#define AI_APP_INPUT_W           96U
#define AI_APP_INPUT_H           96U
#define AI_APP_INPUT_CHANNELS    3U
#define AI_APP_INPUT_BYTES       (AI_APP_INPUT_W * AI_APP_INPUT_H * AI_APP_INPUT_CHANNELS)
#define AI_APP_GRID_W            5U
#define AI_APP_GRID_H            5U
#define AI_APP_GRID_CELLS        (AI_APP_GRID_W * AI_APP_GRID_H)
#define AI_APP_THRESHOLD_X10     93U
#define AI_APP_ARENA_SIZE        183808U

typedef struct
{
    uint16_t score_x10;
    uint16_t mean_x10;
    uint16_t threshold_x10;
    uint16_t duration_ms;
    uint16_t capture_ms;
    uint16_t dsp_ms;
    uint16_t classification_ms;
    uint16_t anomaly_ms;
    uint16_t postprocessing_ms;
    uint16_t signal_ms;
    uint16_t result_ms;
    uint16_t tflm_setup_ms;
    uint16_t tflm_allocate_ms;
    uint16_t tflm_invoke_ms;
    uint16_t tflm_output_ms;
    uint16_t tflm_graph0_ms;
    uint16_t tflm_graph1_ms;
    uint8_t hot_cells;
    uint8_t grid_w;
    uint8_t grid_h;
    uint8_t heatmap[AI_APP_GRID_CELLS];
    rt_bool_t anomaly;
} ai_app_result_t;

/*
 * 将一帧相机图像转换为 Edge Impulse 模型输入。
 *
 * 参数：
 * - src_rgb565：源图像，320x240 RGB565，每像素一个 uint16_t；
 * - dst_rgb888：输出缓冲区，96x96 RGB888，按 R/G/B 字节顺序；
 * - dst_size：dst_rgb888 字节数，至少为 AI_APP_INPUT_BYTES。
 *
 * 返回：
 * - RT_EOK：转换完成；
 * - -RT_ERROR：指针无效或 dst_size 不足。
 *
 * 说明：
 * - 与 PC 端数据集工具的预处理保持一致：先裁剪 40,0,240,240，再双线性缩放到 96x96。
 */
int ai_app_preprocess_rgb565_to_rgb88896(const uint16_t *src_rgb565,
                                         uint8_t *dst_rgb888,
                                         uint32_t dst_size);

/*
 * 推理占位实现：未启用 APP_USING_EDGE_IMPULSE_V15 时由 detect_app 调用。
 *
 * 参数：
 * - input_rgb888：预处理后的 96x96 RGB888 输入；
 * - input_size：input_rgb888 字节数，必须等于 AI_APP_INPUT_BYTES；
 * - out_result：输出的占位结果，结构与真实推理结果一致。
 *
 * 返回：
 * - RT_EOK：结果已填充；
 * - -RT_ERROR：参数无效。
 */
int ai_app_run_placeholder(const uint8_t *input_rgb888,
                           uint32_t input_size,
                           ai_app_result_t *out_result);

/* 通过 msh 打印当前固定的 AI 输入与预处理参数 */
void ai_app_print_info(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_APP_H */

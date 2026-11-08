#ifndef UI_DETECT_H
#define UI_DETECT_H

#include "lvgl/lvgl.h"
#include "../ui_data.h"
#include <stdint.h>

/*
 * 创建检测页面。
 *
 * 当前只是 UI 假数据版，后续会逐步接入：
 * - 摄像头帧刷新
 * - AI 推理结果
 * - 启动/停止/刷新按钮事件
 */
lv_obj_t *ui_detect_create(lv_obj_t *parent);

/*
 * 更新检测页摄像头画面。
 *
 * 输入必须是 RGB565 连续帧缓冲，当前 UI 只接受 320x240。
 * 注意：这个函数不复制图像数据，buf 指向的内存必须在 LVGL 显示期间保持有效。
 * 真实系统里建议由 UI 线程调用，DMA/摄像头中断不要直接调用 LVGL 接口。
 */
void ui_detect_set_camera_frame_rgb565(const uint8_t *buf, uint16_t width, uint16_t height);

/*
 * 更新检测页的 AI 热力图覆盖层。
 *
 * heatmap 使用 ui_ai_heatmap_t 的通用格式，不绑定 5x5、12x12 或某个 SDK。
 * 真实系统中建议由 AI 适配层完成：
 * TFLite 输出 tensor -> Edge Impulse 后处理 -> 0..255 score_map -> 调用本函数刷新 UI。
 */
void ui_detect_set_heatmap(const ui_ai_heatmap_t *heatmap);

/*
 * Refresh the Detect page from the latest ui_data snapshot.
 *
 * This function is safe to call from the LVGL thread when detect_app publishes
 * a result outside the Detect page timer, for example after a PC-side result
 * is injected from the command service.
 */
void ui_detect_refresh_from_snapshot(void);

/*
 * 刷新检测页的静态语言文本。
 *
 * 设置页切换中英文后，ui.c 会调用这个函数。
 * 这里只刷新标题、按钮、指标名称等 UI 文案，不改变检测数据和硬件状态。
 */
void ui_detect_refresh_language(void);

/*
 * Optional callback used by the UI shell to mirror detect result changes to
 * the GLCDC camera overlay.
 *
 * Parameters:
 * - cb: Function called when Detect page renders a new frame_id. Pass NULL to
 *   disable the callback.
 */
void ui_detect_set_result_changed_callback(void (*cb)(void));

#endif /* UI_DETECT_H */

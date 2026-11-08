#ifndef DETECT_APP_H
#define DETECT_APP_H

#include "ui_data.h"
#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DETECT_APP_MODEL_PROFILE_V8_96 = 0,
    DETECT_APP_MODEL_PROFILE_V14_240,
} detect_app_model_profile_t;

typedef void (*detect_app_result_published_cb_t)(void *user_data);

typedef struct
{
    uint32_t total_count;
    uint32_t ok_count;
    uint32_t ng_count;
    uint16_t capture_ms;
    uint16_t camera_wait_ms;
    uint16_t preprocess_ms;
    uint16_t capture_copy_ms;
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
} detect_app_stats_t;

/*
 * Initialize the detect task framework.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_EOK: Task resources are ready.
 * - -RT_ERROR: Thread creation failed.
 *
 * Note:
 * - The task captures AI input from camera_app first, then runs the configured
 *   inference backend and publishes UI/CSV/heatmap data.
 */
int detect_app_init(void);

/*
 * Handle a command from the LVGL Detect page or msh.
 *
 * Parameters:
 * - cmd: Start, Stop or Refresh command.
 *
 * Return:
 * - None.
 */
void detect_app_handle_command(ui_detect_command_t cmd);

/*
 * Same as detect_app_handle_command(), but prints a source tag for debugging
 * UI/msh command flow.
 */
void detect_app_handle_command_from(ui_detect_command_t cmd, const char *source);

/*
 * Request exactly one detection cycle and wait until the result snapshot is
 * published. This is used by the automatic conveyor/gate flow so the sort
 * decision is based on the same result that the LCD page displays.
 *
 * Parameters:
 * - out_snapshot: Optional output snapshot copied after detection completes.
 * - timeout_ms: Maximum wait time. Use a value that covers capture, optional
 *   PC TCP frame upload, and Edge Impulse inference.
 *
 * Return:
 * - RT_EOK: One detection result was published.
 * - -RT_EBUSY: Detection is already running or another one-shot is pending.
 * - -RT_ETIMEOUT: No new result was published before timeout.
 * - -RT_ERROR: detect_app cannot initialize.
 */
int detect_app_request_once_and_wait(ui_detect_snapshot_t *out_snapshot,
                                     uint32_t timeout_ms);

/*
 * Let an external flow controller, such as the conveyor software-gate flow,
 * expose its active state through the Detect page snapshot without enabling
 * the periodic detect loop.
 */
void detect_app_set_external_run_state(rt_bool_t active);

/*
 * Update the Detect page pipeline stage for an external inspection flow.
 * This keeps the LCD progress indicators synchronized while the conveyor flow
 * is waiting for an off-board inspection result.
 */
void detect_app_set_external_stage(ui_detect_stage_t stage);

/*
 * Register a lightweight notification hook fired after detect_app publishes a
 * new result snapshot. The callback can run from detect, pccmd, or a future
 * flow thread, so it must only set flags or release RT-Thread primitives; do
 * not call LVGL APIs directly from this callback.
 */
void detect_app_set_result_published_callback(detect_app_result_published_cb_t cb,
                                              void *user_data);

/*
 * Select the model profile used by the detect backend.
 *
 * Parameters:
 * - profile: Model metadata profile. This does not load the real Edge Impulse
 *   SDK yet; it only keeps UI, CSV and heatmap metadata aligned with the model
 *   size that will be used later.
 *
 * Return:
 * - RT_EOK: Profile applied.
 * - -RT_ERROR: Invalid profile or detect_app cannot initialize.
 */
int detect_app_set_model_profile(detect_app_model_profile_t profile);

/*
 * Apply runtime metadata from a loaded model package without changing the
 * underlying firmware profile implementation.
 *
 * Parameters:
 * - name: Display/package name.
 * - input_w/input_h: Model input size. First runtime-loading stage supports
 *   the existing 96x96 profile only.
 * - grid_w/grid_h: Heatmap grid size.
 * - threshold_x10: Runtime anomaly threshold, for example 83 means 8.3.
 * - tflite_count: Number of TFLite graphs in the package.
 * - arena_size: Largest graph arena size.
 *
 * Return:
 * - RT_EOK: Metadata applied to UI/statistics.
 * - -RT_ERROR: Invalid parameters or detect_app cannot initialize.
 * - -RT_EBUSY: Detection is running; stop it before switching model.
 */
int detect_app_apply_model_metadata(const char *name,
                                    uint16_t input_w,
                                    uint16_t input_h,
                                    uint16_t grid_w,
                                    uint16_t grid_h,
                                    uint16_t threshold_x10,
                                    uint8_t tflite_count,
                                    uint32_t arena_size);

/*
 * Check whether the detection task is idle enough for a model switch.
 *
 * Return:
 * - RT_EOK: The model can be switched now.
 * - -RT_ERROR: detect_app cannot initialize.
 * - -RT_EBUSY: Detection is running or a single-shot inference is pending.
 */
int detect_app_can_switch_model(void);

/*
 * Return the currently selected model profile.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - Current detect model profile.
 */
detect_app_model_profile_t detect_app_get_model_profile(void);

/*
 * Copy the latest detection snapshot.
 *
 * Parameters:
 * - out_snapshot: Output buffer filled with the current detection state.
 *
 * Return:
 * - RT_EOK: Snapshot copied.
 * - -RT_ERROR: out_snapshot is NULL.
 */
int detect_app_get_snapshot(ui_detect_snapshot_t *out_snapshot);

/*
 * Copy detect counters and timing details.
 *
 * Parameters:
 * - out_stats: Output buffer filled with total/OK/NG counters and latest
 *   timing values.
 *
 * Return:
 * - RT_EOK: Stats copied.
 * - -RT_ERROR: out_stats is NULL or detect_app cannot initialize.
 */
int detect_app_get_stats(detect_app_stats_t *out_stats);

/*
 * Inject one synthetic detection result into the current detect snapshot.
 *
 * Parameters:
 * - score_x10: Anomaly score scaled by 10.
 * - mean_x10: Mean anomaly score scaled by 10.
 * - hot_cells: Number of hot heatmap cells.
 * - duration_ms: Total detect duration.
 * - capture_ms: Capture duration.
 * - dsp_ms: DSP/preprocess duration.
 * - classification_ms: Classification duration.
 * - anomaly_ms: Anomaly stage duration.
 * - postprocessing_ms: Post-processing duration.
 * - signal_ms: Signal stage duration.
 * - result_ms: Final result duration.
 * - tflm_setup_ms: TFLM setup duration.
 * - tflm_allocate_ms: TFLM allocation duration.
 * - tflm_invoke_ms: TFLM invoke duration.
 * - tflm_output_ms: TFLM output duration.
 * - tflm_graph0_ms: TFLM graph0 duration.
 * - tflm_graph1_ms: TFLM graph1 duration.
 * - trigger_sort: If RT_TRUE and anomaly is true, run the existing sort demo.
 *
 * Return:
 * - RT_EOK: Result injected.
 * - -RT_ERROR: detect_app unavailable.
 * - -RT_EBUSY: Detection loop is running.
 */
int detect_app_inject_result(uint16_t score_x10,
                             uint16_t mean_x10,
                             uint8_t hot_cells,
                             uint16_t duration_ms,
                             uint16_t capture_ms,
                             uint16_t dsp_ms,
                             uint16_t classification_ms,
                             uint16_t anomaly_ms,
                             uint16_t postprocessing_ms,
                             uint16_t signal_ms,
                             uint16_t result_ms,
                             uint16_t tflm_setup_ms,
                             uint16_t tflm_allocate_ms,
                             uint16_t tflm_invoke_ms,
                             uint16_t tflm_output_ms,
                             uint16_t tflm_graph0_ms,
                             uint16_t tflm_graph1_ms,
                             rt_bool_t trigger_sort);

/*
 * Inject an external PC-side inspection result into the current detect
 * snapshot. Unlike detect_app_inject_result(), this function accepts a full
 * heatmap so the LCD can show the real defect position.
 *
 * Parameters:
 * - anomaly: Final NG/OK decision from the external inspection pipeline.
 * - score_x10/mean_x10: Display score scaled by 10.
 * - hot_cells: Number of non-zero heatmap cells.
 * - heatmap: Row-major 0..255 heatmap intensities.
 * - grid_w/grid_h: Heatmap shape. PC result path supports up to
 *   16x16 cells; Edge Impulse fallback still uses its native 5x5 map.
 * - duration_ms: Total external inspection duration, if known.
 *
 * Return:
 * - RT_EOK: Result injected.
 * - -RT_ERROR: detect_app unavailable or invalid parameters.
 * - -RT_EBUSY: Detection loop is running.
 */
int detect_app_inject_pc_result(rt_bool_t anomaly,
                                uint16_t score_x10,
                                uint16_t mean_x10,
                                uint8_t hot_cells,
                                const uint8_t *heatmap,
                                uint8_t grid_w,
                                uint8_t grid_h,
                                uint16_t duration_ms);
/*
 * Get the latest heatmap object.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - Pointer to the latest heatmap. The pointer remains valid until the next
 *   detect_app update.
 */
const ui_ai_heatmap_t *detect_app_get_heatmap(void);

/*
 * Return whether detect_app is the owner of Detect page runtime data.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_TRUE: UI should read detect_app snapshot/heatmap.
 * - RT_FALSE: UI should keep its own initial/empty state.
 */
rt_bool_t detect_app_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* DETECT_APP_H */

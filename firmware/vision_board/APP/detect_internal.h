#ifndef DETECT_INTERNAL_H
#define DETECT_INTERNAL_H

#include "detect_app.h"
#include "ai_app.h"
#include "camera_app.h"
#include "sort_demo_app.h"
#include "storage_app.h"
#include "hal_data.h"

#ifdef APP_USING_EDGE_IMPULSE_V15
#include "ei_v15_inference.h"
#endif

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

#define DBG_TAG "detect_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define DETECT_APP_THREAD_STACK_SIZE (16U * 1024U)
#define DETECT_APP_THREAD_PRIORITY   23U
#define DETECT_APP_AI_PRIORITY       18U
#define DETECT_APP_THREAD_TICK       10U
#define DETECT_APP_INTERVAL_MS       1000U
#define DETECT_APP_CAMERA_FRAME_W    320U
#define DETECT_APP_CAMERA_FRAME_H    240U
#define DETECT_APP_ROI_X             40U
#define DETECT_APP_ROI_Y             0U
#define DETECT_APP_ROI_SIZE          240U
#define DETECT_APP_HEATMAP_MAX_W     16U
#define DETECT_APP_HEATMAP_MAX_H     16U
#define DETECT_APP_HEATMAP_CELLS     (DETECT_APP_HEATMAP_MAX_W * DETECT_APP_HEATMAP_MAX_H)
#define DETECT_APP_SUSPEND_PREVIEW_DURING_AI 1U
#define DETECT_APP_RUNTIME_MODEL_NAME_MAX 32U
#define DETECT_APP_PC_FRAME_DEFAULT_PORT 5000U
#define DETECT_APP_PC_FRAME_DEFAULT_CHUNK 2048U
#define DETECT_APP_PC_FRAME_DEFAULT_ACCEPT_MS 30000U

typedef struct
{
    detect_app_model_profile_t profile;
    const char *name;
    uint16_t input_w;
    uint16_t input_h;
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t threshold_x10;
    uint8_t tflite_count;
    uint32_t arena_size;
} detect_app_model_profile_info_t;

/* 模块内部共享接口，仅供本模块的拆分源文件使用。 */
extern rt_mutex_t g_detect_lock;
extern volatile rt_bool_t g_detect_running;
extern volatile rt_bool_t g_detect_single_shot;
extern uint32_t g_detect_frame_id;
extern uint32_t g_detect_total_count;
extern uint32_t g_detect_ok_count;
extern uint16_t g_detect_last_capture_ms;
extern uint16_t g_detect_last_camera_wait_ms;
extern uint16_t g_detect_last_preprocess_ms;
extern uint16_t g_detect_last_capture_copy_ms;
extern uint16_t g_detect_last_dsp_ms;
extern uint16_t g_detect_last_classification_ms;
extern uint16_t g_detect_last_anomaly_ms;
extern uint16_t g_detect_last_postprocessing_ms;
extern uint16_t g_detect_last_signal_ms;
extern uint16_t g_detect_last_result_ms;
extern uint16_t g_detect_last_tflm_setup_ms;
extern uint16_t g_detect_last_tflm_allocate_ms;
extern uint16_t g_detect_last_tflm_invoke_ms;
extern uint16_t g_detect_last_tflm_output_ms;
extern uint16_t g_detect_last_tflm_graph0_ms;
extern uint16_t g_detect_last_tflm_graph1_ms;
extern rt_bool_t g_detect_pc_frame_upload_enabled;
extern uint16_t g_detect_pc_frame_port;
extern uint16_t g_detect_pc_frame_chunk;
extern uint32_t g_detect_pc_frame_accept_ms;
extern const detect_app_model_profile_info_t *g_detect_profile;
extern uint8_t g_detect_heatmap_scores[DETECT_APP_HEATMAP_CELLS];
extern ui_ai_heatmap_t g_detect_heatmap;
extern ui_detect_snapshot_t g_detect_snapshot;
void detect_app_clear_heatmap_locked(void);
void detect_app_notify_snapshot_changed(void);
void detect_app_publish_result(uint16_t score_x10,
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
                               uint16_t tflm_graph1_ms);
int detect_app_apply_result(const ai_app_result_t *result,
                            uint16_t capture_ms,
                            rt_bool_t trigger_sort);

#endif /* DETECT_INTERNAL_H */

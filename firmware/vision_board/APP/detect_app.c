#include "detect_internal.h"

static const detect_app_model_profile_info_t s_detect_model_profiles[] =
{
    {
        DETECT_APP_MODEL_PROFILE_V8_96,
        "unloaded",
        96U,
        96U,
        5U,
        5U,
        AI_APP_THRESHOLD_X10,
        2U,
        AI_APP_ARENA_SIZE,
    },
    {
        DETECT_APP_MODEL_PROFILE_V14_240,
        "cap_v14_240",
        240U,
        240U,
        14U,
        14U,
        64U,
        2U,
        1690086U,
    },
};

static rt_thread_t s_detect_thread = RT_NULL;
static rt_sem_t s_detect_sem = RT_NULL;
rt_mutex_t g_detect_lock = RT_NULL;
static volatile rt_bool_t s_detect_ready = RT_FALSE;
volatile rt_bool_t g_detect_running = RT_FALSE;
volatile rt_bool_t g_detect_single_shot = RT_FALSE;
static volatile rt_bool_t s_detect_external_running = RT_FALSE;
uint32_t g_detect_frame_id = 0U;
static uint32_t s_detect_record_id = 0U;
uint32_t g_detect_total_count = 0U;
uint32_t g_detect_ok_count = 0U;
uint16_t g_detect_last_capture_ms = 0U;
uint16_t g_detect_last_camera_wait_ms = 0U;
uint16_t g_detect_last_preprocess_ms = 0U;
uint16_t g_detect_last_capture_copy_ms = 0U;
uint16_t g_detect_last_dsp_ms = 0U;
uint16_t g_detect_last_classification_ms = 0U;
uint16_t g_detect_last_anomaly_ms = 0U;
uint16_t g_detect_last_postprocessing_ms = 0U;
uint16_t g_detect_last_signal_ms = 0U;
uint16_t g_detect_last_result_ms = 0U;
uint16_t g_detect_last_tflm_setup_ms = 0U;
uint16_t g_detect_last_tflm_allocate_ms = 0U;
uint16_t g_detect_last_tflm_invoke_ms = 0U;
uint16_t g_detect_last_tflm_output_ms = 0U;
uint16_t g_detect_last_tflm_graph0_ms = 0U;
uint16_t g_detect_last_tflm_graph1_ms = 0U;
rt_bool_t g_detect_pc_frame_upload_enabled = RT_FALSE;
uint16_t g_detect_pc_frame_port = DETECT_APP_PC_FRAME_DEFAULT_PORT;
uint16_t g_detect_pc_frame_chunk = DETECT_APP_PC_FRAME_DEFAULT_CHUNK;
uint32_t g_detect_pc_frame_accept_ms = DETECT_APP_PC_FRAME_DEFAULT_ACCEPT_MS;
const detect_app_model_profile_info_t *g_detect_profile = &s_detect_model_profiles[0];
static detect_app_model_profile_info_t s_detect_runtime_profile =
{
    DETECT_APP_MODEL_PROFILE_V8_96,
    "unloaded",
    96U,
    96U,
    5U,
    5U,
    AI_APP_THRESHOLD_X10,
    2U,
    AI_APP_ARENA_SIZE,
};
static char s_detect_runtime_model_name[DETECT_APP_RUNTIME_MODEL_NAME_MAX] = "unloaded";
uint8_t g_detect_heatmap_scores[DETECT_APP_HEATMAP_CELLS];
ui_ai_heatmap_t g_detect_heatmap;
ui_detect_snapshot_t g_detect_snapshot;
static detect_app_result_published_cb_t s_detect_result_cb = RT_NULL;
static void *s_detect_result_cb_user_data = RT_NULL;
static uint8_t s_detect_ai_input[AI_APP_INPUT_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

static void detect_app_thread_entry(void *parameter);
static void detect_app_reset_snapshot(void);
static const detect_app_model_profile_info_t *detect_app_find_profile(detect_app_model_profile_t profile);
static void detect_app_apply_profile_locked(const detect_app_model_profile_info_t *profile);
static void detect_app_set_stage(ui_detect_stage_t stage);
static void detect_app_apply_ai_heatmap(const ai_app_result_t *result);
static int detect_app_run_inference(ai_app_result_t *out_result);
static int detect_app_append_record(const ui_detect_snapshot_t *snapshot);
static void detect_app_make_timestamp(char *buf, uint32_t buf_size);
static uint16_t detect_app_calc_yield_x10(void);

int detect_app_init(void)
{
    if (s_detect_ready == RT_TRUE)
    {
        return RT_EOK;
    }

    g_detect_lock = rt_mutex_create("det_lock", RT_IPC_FLAG_PRIO);
    if (g_detect_lock == RT_NULL)
    {
        LOG_E("create mutex failed");
        return -RT_ERROR;
    }

    s_detect_sem = rt_sem_create("det_sem", 0, RT_IPC_FLAG_PRIO);
    if (s_detect_sem == RT_NULL)
    {
        LOG_E("create sem failed");
        rt_mutex_delete(g_detect_lock);
        g_detect_lock = RT_NULL;
        return -RT_ERROR;
    }

    detect_app_reset_snapshot();

    s_detect_thread = rt_thread_create("detect",
                                       detect_app_thread_entry,
                                       RT_NULL,
                                       DETECT_APP_THREAD_STACK_SIZE,
                                       DETECT_APP_THREAD_PRIORITY,
                                       DETECT_APP_THREAD_TICK);
    if (s_detect_thread == RT_NULL)
    {
        LOG_E("create thread failed");
        rt_sem_delete(s_detect_sem);
        rt_mutex_delete(g_detect_lock);
        s_detect_sem = RT_NULL;
        g_detect_lock = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(s_detect_thread);
    s_detect_ready = RT_TRUE;
    return RT_EOK;
}

void detect_app_handle_command(ui_detect_command_t cmd)
{
    detect_app_handle_command_from(cmd, "app");
}

void detect_app_handle_command_from(ui_detect_command_t cmd, const char *source)
{
    if (detect_app_init() != RT_EOK)
    {
        return;
    }

    if (source == RT_NULL)
    {
        source = "unknown";
    }

    switch (cmd)
    {
    case UI_DETECT_CMD_START:
        rt_kprintf("[detect] cmd source=%s action=start\n", source);
        g_detect_running = RT_TRUE;
        g_detect_single_shot = RT_TRUE;
        detect_app_set_stage(UI_DETECT_STAGE_TRIG);
        rt_sem_release(s_detect_sem);
        break;

    case UI_DETECT_CMD_STOP:
        rt_kprintf("[detect] cmd source=%s action=stop\n", source);
        g_detect_running = RT_FALSE;
        g_detect_single_shot = RT_FALSE;
        detect_app_set_stage(UI_DETECT_STAGE_IDLE);
        break;

    case UI_DETECT_CMD_REFRESH:
        rt_kprintf("[detect] cmd source=%s action=once\n", source);
        g_detect_single_shot = RT_TRUE;
        detect_app_set_stage(UI_DETECT_STAGE_TRIG);
        rt_sem_release(s_detect_sem);
        break;

    default:
        break;
    }
}

int detect_app_set_model_profile(detect_app_model_profile_t profile)
{
    const detect_app_model_profile_info_t *profile_info;
    int result = RT_EOK;

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    profile_info = detect_app_find_profile(profile);
    if (profile_info == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    if ((g_detect_running == RT_TRUE) ||
        (g_detect_single_shot == RT_TRUE) ||
        (s_detect_external_running == RT_TRUE))
    {
        /*
         * 推理过程中热力图/CSV 依赖当前模型参数，切换模型前必须先停止检测。
         */
        result = -RT_EBUSY;
    }
    else
    {
#ifdef APP_USING_EDGE_IMPULSE_V15
        ei_v15_use_builtin_model();
#endif
        g_detect_profile = profile_info;
        detect_app_apply_profile_locked(g_detect_profile);
    }
    rt_mutex_release(g_detect_lock);
    return result;
}

int detect_app_apply_model_metadata(const char *name,
                                    uint16_t input_w,
                                    uint16_t input_h,
                                    uint16_t grid_w,
                                    uint16_t grid_h,
                                    uint16_t threshold_x10,
                                    uint8_t tflite_count,
                                    uint32_t arena_size)
{
    int result = RT_EOK;

    if ((name == RT_NULL) || (name[0] == '\0') ||
        (input_w == 0U) || (input_h == 0U) ||
        (grid_w == 0U) || (grid_h == 0U) ||
        (threshold_x10 == 0U) || (tflite_count == 0U) ||
        (arena_size == 0U))
    {
        return -RT_ERROR;
    }

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    if ((g_detect_running == RT_TRUE) ||
        (g_detect_single_shot == RT_TRUE) ||
        (s_detect_external_running == RT_TRUE))
    {
        result = -RT_EBUSY;
    }
    else
    {
        rt_strncpy(s_detect_runtime_model_name, name, sizeof(s_detect_runtime_model_name) - 1U);
        s_detect_runtime_model_name[sizeof(s_detect_runtime_model_name) - 1U] = '\0';

        s_detect_runtime_profile.profile = DETECT_APP_MODEL_PROFILE_V8_96;
        s_detect_runtime_profile.name = s_detect_runtime_model_name;
        s_detect_runtime_profile.input_w = input_w;
        s_detect_runtime_profile.input_h = input_h;
        s_detect_runtime_profile.grid_w = grid_w;
        s_detect_runtime_profile.grid_h = grid_h;
        s_detect_runtime_profile.threshold_x10 = threshold_x10;
        s_detect_runtime_profile.tflite_count = tflite_count;
        s_detect_runtime_profile.arena_size = arena_size;

        g_detect_profile = &s_detect_runtime_profile;
        detect_app_apply_profile_locked(g_detect_profile);
    }
    rt_mutex_release(g_detect_lock);

    return result;
}

int detect_app_can_switch_model(void)
{
    int result = RT_EOK;

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    if ((g_detect_running == RT_TRUE) ||
        (g_detect_single_shot == RT_TRUE) ||
        (s_detect_external_running == RT_TRUE))
    {
        result = -RT_EBUSY;
    }
    rt_mutex_release(g_detect_lock);

    return result;
}

detect_app_model_profile_t detect_app_get_model_profile(void)
{
    detect_app_model_profile_t profile;

    if (detect_app_init() != RT_EOK)
    {
        return DETECT_APP_MODEL_PROFILE_V8_96;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    profile = g_detect_profile->profile;
    rt_mutex_release(g_detect_lock);
    return profile;
}

int detect_app_get_snapshot(ui_detect_snapshot_t *out_snapshot)
{
    if (out_snapshot == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    *out_snapshot = g_detect_snapshot;
    rt_mutex_release(g_detect_lock);
    return RT_EOK;
}

int detect_app_get_stats(detect_app_stats_t *out_stats)
{
    if (out_stats == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    out_stats->total_count = g_detect_total_count;
    out_stats->ok_count = g_detect_ok_count;
    out_stats->ng_count = g_detect_total_count - g_detect_ok_count;
    out_stats->capture_ms = g_detect_last_capture_ms;
    out_stats->camera_wait_ms = g_detect_last_camera_wait_ms;
    out_stats->preprocess_ms = g_detect_last_preprocess_ms;
    out_stats->capture_copy_ms = g_detect_last_capture_copy_ms;
    out_stats->dsp_ms = g_detect_last_dsp_ms;
    out_stats->classification_ms = g_detect_last_classification_ms;
    out_stats->anomaly_ms = g_detect_last_anomaly_ms;
    out_stats->postprocessing_ms = g_detect_last_postprocessing_ms;
    out_stats->signal_ms = g_detect_last_signal_ms;
    out_stats->result_ms = g_detect_last_result_ms;
    out_stats->tflm_setup_ms = g_detect_last_tflm_setup_ms;
    out_stats->tflm_allocate_ms = g_detect_last_tflm_allocate_ms;
    out_stats->tflm_invoke_ms = g_detect_last_tflm_invoke_ms;
    out_stats->tflm_output_ms = g_detect_last_tflm_output_ms;
    out_stats->tflm_graph0_ms = g_detect_last_tflm_graph0_ms;
    out_stats->tflm_graph1_ms = g_detect_last_tflm_graph1_ms;
    rt_mutex_release(g_detect_lock);

    return RT_EOK;
}

const ui_ai_heatmap_t *detect_app_get_heatmap(void)
{
    if (detect_app_init() != RT_EOK)
    {
        return RT_NULL;
    }

    return &g_detect_heatmap;
}

rt_bool_t detect_app_is_ready(void)
{
    return s_detect_ready;
}

void detect_app_set_result_published_callback(detect_app_result_published_cb_t cb,
                                              void *user_data)
{
    if (detect_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    s_detect_result_cb = cb;
    s_detect_result_cb_user_data = user_data;
    rt_mutex_release(g_detect_lock);
}

void detect_app_set_external_run_state(rt_bool_t active)
{
    detect_app_result_published_cb_t published_cb;
    void *published_user_data;

    if (detect_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    s_detect_external_running = active ? RT_TRUE : RT_FALSE;
    g_detect_snapshot.run_state = ((g_detect_running == RT_TRUE) ||
                                   (s_detect_external_running == RT_TRUE)) ?
                                  UI_DETECT_RUN_RUNNING :
                                  UI_DETECT_RUN_STOPPED;
    if ((active != RT_TRUE) &&
        (g_detect_running != RT_TRUE) &&
        (g_detect_single_shot != RT_TRUE))
    {
        g_detect_snapshot.stage = UI_DETECT_STAGE_IDLE;
    }
    published_cb = s_detect_result_cb;
    published_user_data = s_detect_result_cb_user_data;
    rt_mutex_release(g_detect_lock);

    if (published_cb != RT_NULL)
    {
        published_cb(published_user_data);
    }
}

void detect_app_set_external_stage(ui_detect_stage_t stage)
{
    detect_app_result_published_cb_t published_cb;
    void *published_user_data;

    if (detect_app_init() != RT_EOK)
    {
        return;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    g_detect_snapshot.run_state = ((g_detect_running == RT_TRUE) ||
                                   (s_detect_external_running == RT_TRUE)) ?
                                  UI_DETECT_RUN_RUNNING :
                                  UI_DETECT_RUN_STOPPED;
    g_detect_snapshot.stage = stage;
    published_cb = s_detect_result_cb;
    published_user_data = s_detect_result_cb_user_data;
    rt_mutex_release(g_detect_lock);

    if (published_cb != RT_NULL)
    {
        published_cb(published_user_data);
    }
}

int detect_app_request_once_and_wait(ui_detect_snapshot_t *out_snapshot,
                                     uint32_t timeout_ms)
{
    uint32_t start_tick;
    uint32_t timeout_tick;
    uint32_t start_frame_id;
    rt_bool_t busy;
    ui_detect_snapshot_t snapshot;

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    busy = ((g_detect_running == RT_TRUE) || (g_detect_single_shot == RT_TRUE)) ? RT_TRUE : RT_FALSE;
    start_frame_id = g_detect_snapshot.camera.frame_id;
    if (busy == RT_FALSE)
    {
        g_detect_single_shot = RT_TRUE;
        g_detect_snapshot.stage = UI_DETECT_STAGE_TRIG;
    }
    rt_mutex_release(g_detect_lock);

    if (busy == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    rt_sem_release(s_detect_sem);
    start_tick = rt_tick_get();
    timeout_tick = rt_tick_from_millisecond(timeout_ms);

    while (1)
    {
        rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
        snapshot = g_detect_snapshot;
        busy = g_detect_single_shot;
        rt_mutex_release(g_detect_lock);

        if ((snapshot.camera.frame_id != start_frame_id) &&
            (snapshot.stage == UI_DETECT_STAGE_OUTPUT))
        {
            if (out_snapshot != RT_NULL)
            {
                *out_snapshot = snapshot;
            }
            return RT_EOK;
        }

        if (busy == RT_FALSE)
        {
            return -RT_ERROR;
        }

        if ((uint32_t)(rt_tick_get() - start_tick) >= timeout_tick)
        {
            rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
            g_detect_single_shot = RT_FALSE;
            rt_mutex_release(g_detect_lock);
            return -RT_ETIMEOUT;
        }

        rt_thread_mdelay(20U);
    }
}

static void detect_app_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        if ((g_detect_running == RT_FALSE) && (g_detect_single_shot == RT_FALSE))
        {
            rt_sem_take(s_detect_sem, RT_WAITING_FOREVER);
            continue;
        }

        detect_app_set_stage(UI_DETECT_STAGE_TRIG);
        rt_thread_mdelay(20);
        detect_app_set_stage(UI_DETECT_STAGE_CAPTURE);

        g_detect_frame_id++;
        {
            ai_app_result_t result;

            if (detect_app_run_inference(&result) == RT_EOK)
            {
                (void)detect_app_apply_result(&result, result.capture_ms, RT_FALSE);
            }
            else
            {
                detect_app_set_stage(UI_DETECT_STAGE_IDLE);
                LOG_W("inference skipped");
            }
        }

        if (g_detect_single_shot == RT_TRUE)
        {
            g_detect_single_shot = RT_FALSE;
        }

        if (g_detect_running == RT_TRUE)
        {
            rt_thread_mdelay(DETECT_APP_INTERVAL_MS);
        }
    }
}

static void detect_app_reset_snapshot(void)
{
    memset(g_detect_heatmap_scores, 0, sizeof(g_detect_heatmap_scores));
    memset(&g_detect_snapshot, 0, sizeof(g_detect_snapshot));
    g_detect_total_count = 0U;
    g_detect_ok_count = 0U;
    g_detect_last_capture_ms = 0U;
    g_detect_last_camera_wait_ms = 0U;
    g_detect_last_preprocess_ms = 0U;
    g_detect_last_capture_copy_ms = 0U;
    g_detect_last_dsp_ms = 0U;
    g_detect_last_classification_ms = 0U;
    g_detect_last_anomaly_ms = 0U;
    g_detect_last_postprocessing_ms = 0U;
    g_detect_last_signal_ms = 0U;
    g_detect_last_result_ms = 0U;
    g_detect_last_tflm_setup_ms = 0U;
    g_detect_last_tflm_allocate_ms = 0U;
    g_detect_last_tflm_invoke_ms = 0U;
    g_detect_last_tflm_output_ms = 0U;
    g_detect_last_tflm_graph0_ms = 0U;
    g_detect_last_tflm_graph1_ms = 0U;

    g_detect_heatmap.roi_x = DETECT_APP_ROI_X;
    g_detect_heatmap.roi_y = DETECT_APP_ROI_Y;
    g_detect_heatmap.roi_w = DETECT_APP_ROI_SIZE;
    g_detect_heatmap.roi_h = DETECT_APP_ROI_SIZE;
    g_detect_heatmap.score_map = g_detect_heatmap_scores;

    g_detect_snapshot.run_state = UI_DETECT_RUN_STOPPED;
    g_detect_snapshot.stage = UI_DETECT_STAGE_IDLE;
    g_detect_snapshot.model.postprocess = UI_AI_POSTPROCESS_VISUAL_AD;
    g_detect_snapshot.camera.pixel_format = "RGB565";
    g_detect_snapshot.camera.frame_size_name = "QVGA";
    g_detect_snapshot.camera.frame_w = DETECT_APP_CAMERA_FRAME_W;
    g_detect_snapshot.camera.frame_h = DETECT_APP_CAMERA_FRAME_H;
    g_detect_snapshot.camera.roi_x = DETECT_APP_ROI_X;
    g_detect_snapshot.camera.roi_y = DETECT_APP_ROI_Y;
    g_detect_snapshot.camera.roi_size = DETECT_APP_ROI_SIZE;
    g_detect_snapshot.ai.yield_x10 = 1000U;
    detect_app_apply_profile_locked(g_detect_profile);
}

static const detect_app_model_profile_info_t *detect_app_find_profile(detect_app_model_profile_t profile)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(s_detect_model_profiles) / sizeof(s_detect_model_profiles[0])); i++)
    {
        if (s_detect_model_profiles[i].profile == profile)
        {
            return &s_detect_model_profiles[i];
        }
    }

    return RT_NULL;
}

static void detect_app_apply_profile_locked(const detect_app_model_profile_info_t *profile)
{
    if (profile == RT_NULL)
    {
        return;
    }

    memset(g_detect_heatmap_scores, 0, sizeof(g_detect_heatmap_scores));

    g_detect_heatmap.grid_w = profile->grid_w;
    g_detect_heatmap.grid_h = profile->grid_h;
    g_detect_heatmap.score_count = (uint16_t)(profile->grid_w * profile->grid_h);

    g_detect_snapshot.model.name = profile->name;
    g_detect_snapshot.model.input_w = profile->input_w;
    g_detect_snapshot.model.input_h = profile->input_h;
    g_detect_snapshot.model.grid_w = profile->grid_w;
    g_detect_snapshot.model.grid_h = profile->grid_h;
    g_detect_snapshot.model.threshold_x10 = profile->threshold_x10;
    g_detect_snapshot.model.tflite_count = profile->tflite_count;
    g_detect_snapshot.model.arena_size = profile->arena_size;
    g_detect_snapshot.ai.threshold_x10 = profile->threshold_x10;
}

static void detect_app_set_stage(ui_detect_stage_t stage)
{
    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    g_detect_snapshot.run_state = ((g_detect_running == RT_TRUE) ||
                                  (s_detect_external_running == RT_TRUE)) ?
                                  UI_DETECT_RUN_RUNNING :
                                  UI_DETECT_RUN_STOPPED;
    g_detect_snapshot.stage = stage;
    rt_mutex_release(g_detect_lock);
}

void detect_app_clear_heatmap_locked(void)
{
    memset(g_detect_heatmap_scores, 0, sizeof(g_detect_heatmap_scores));
    g_detect_heatmap.score_count = 0U;
    g_detect_heatmap.score_map = g_detect_heatmap_scores;
}

void detect_app_notify_snapshot_changed(void)
{
    detect_app_result_published_cb_t published_cb;
    void *published_user_data;

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    published_cb = s_detect_result_cb;
    published_user_data = s_detect_result_cb_user_data;
    rt_mutex_release(g_detect_lock);

    if (published_cb != RT_NULL)
    {
        published_cb(published_user_data);
    }
}

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
                                      uint16_t tflm_graph1_ms)
{
    ui_detect_snapshot_t snapshot_copy;
    detect_app_result_published_cb_t published_cb;
    void *published_user_data;

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    g_detect_snapshot.run_state = ((g_detect_running == RT_TRUE) ||
                                  (s_detect_external_running == RT_TRUE)) ?
                                  UI_DETECT_RUN_RUNNING :
                                  UI_DETECT_RUN_STOPPED;
    g_detect_snapshot.stage = UI_DETECT_STAGE_OUTPUT;
    g_detect_snapshot.camera.frame_id = g_detect_frame_id;
    g_detect_snapshot.ai.anomaly = (score_x10 > g_detect_snapshot.ai.threshold_x10);
    g_detect_total_count++;
    if (g_detect_snapshot.ai.anomaly == RT_FALSE)
    {
        g_detect_ok_count++;
    }
    g_detect_snapshot.ai.score_x10 = score_x10;
    g_detect_snapshot.ai.mean_score_x10 = mean_x10;
    g_detect_snapshot.ai.hot_cells = hot_cells;
    g_detect_snapshot.ai.ai_time_ms = duration_ms;
    g_detect_snapshot.ai.yield_x10 = detect_app_calc_yield_x10();
    g_detect_last_capture_ms = capture_ms;
    g_detect_last_dsp_ms = dsp_ms;
    g_detect_last_classification_ms = classification_ms;
    g_detect_last_anomaly_ms = anomaly_ms;
    g_detect_last_postprocessing_ms = postprocessing_ms;
    g_detect_last_signal_ms = signal_ms;
    g_detect_last_result_ms = result_ms;
    g_detect_last_tflm_setup_ms = tflm_setup_ms;
    g_detect_last_tflm_allocate_ms = tflm_allocate_ms;
    g_detect_last_tflm_invoke_ms = tflm_invoke_ms;
    g_detect_last_tflm_output_ms = tflm_output_ms;
    g_detect_last_tflm_graph0_ms = tflm_graph0_ms;
    g_detect_last_tflm_graph1_ms = tflm_graph1_ms;
    snapshot_copy = g_detect_snapshot;
    published_cb = s_detect_result_cb;
    published_user_data = s_detect_result_cb_user_data;
    rt_mutex_release(g_detect_lock);

    detect_app_append_record(&snapshot_copy);
    if (published_cb != RT_NULL)
    {
        published_cb(published_user_data);
    }
}
int detect_app_apply_result(const ai_app_result_t *result,
                                   uint16_t capture_ms,
                                   rt_bool_t trigger_sort)
{
    int ret;

    if (result == RT_NULL)
    {
        return -RT_ERROR;
    }

    detect_app_apply_ai_heatmap(result);
    detect_app_publish_result(result->score_x10,
                              result->mean_x10,
                              result->hot_cells,
                              result->duration_ms,
                              capture_ms,
                              result->dsp_ms,
                              result->classification_ms,
                              result->anomaly_ms,
                              result->postprocessing_ms,
                              result->signal_ms,
                              result->result_ms,
                              result->tflm_setup_ms,
                              result->tflm_allocate_ms,
                              result->tflm_invoke_ms,
                              result->tflm_output_ms,
                              result->tflm_graph0_ms,
                              result->tflm_graph1_ms);

    if ((trigger_sort == RT_TRUE) && (result->anomaly == RT_TRUE))
    {
        ret = sort_demo_run_auto(sort_flow_get_transfer_pulses());
        if (ret != RT_EOK)
        {
            LOG_W("mock sort failed: %d", ret);
            return ret;
        }
    }

    return RT_EOK;
}

static void detect_app_apply_ai_heatmap(const ai_app_result_t *result)
{
    uint32_t cells;
    uint32_t i;

    if (result == RT_NULL)
    {
        return;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    memset(g_detect_heatmap_scores, 0, sizeof(g_detect_heatmap_scores));

    cells = (uint32_t)result->grid_w * (uint32_t)result->grid_h;
    if (cells > DETECT_APP_HEATMAP_CELLS)
    {
        cells = DETECT_APP_HEATMAP_CELLS;
    }
    if (cells > AI_APP_GRID_CELLS)
    {
        cells = AI_APP_GRID_CELLS;
    }

    g_detect_heatmap.grid_w = result->grid_w;
    g_detect_heatmap.grid_h = result->grid_h;
    g_detect_heatmap.score_count = (uint16_t)cells;
    g_detect_snapshot.model.grid_w = result->grid_w;
    g_detect_snapshot.model.grid_h = result->grid_h;
    g_detect_snapshot.ai.threshold_x10 = result->threshold_x10;

    for (i = 0U; i < cells; i++)
    {
        g_detect_heatmap_scores[i] = result->heatmap[i];
    }

    rt_mutex_release(g_detect_lock);
}

static int detect_app_run_inference(ai_app_result_t *out_result)
{
    int ret;
    rt_tick_t capture_start;
    rt_tick_t capture_end;
    uint16_t capture_ms;
    camera_app_ai_capture_metrics_t capture_metrics;
    camera_app_tcp_frame_request_t tcp_request;
    camera_app_tcp_frame_request_t *tcp_request_ptr = RT_NULL;

    if (out_result == RT_NULL)
    {
        return -RT_ERROR;
    }

#if DETECT_APP_SUSPEND_PREVIEW_DURING_AI
    {
        rt_uint8_t ai_priority = DETECT_APP_AI_PRIORITY;
        rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &ai_priority);
    }
#endif

    /*
     * 采集请求只让相机线程预处理下一帧干净图像，
     * 不使用叠加了 ROI/热力图的 LCD 帧。
     */
    capture_start = rt_tick_get();
    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    if (g_detect_pc_frame_upload_enabled == RT_TRUE)
    {
        tcp_request.port = g_detect_pc_frame_port;
        tcp_request.chunk = g_detect_pc_frame_chunk;
        tcp_request.accept_timeout_ms = g_detect_pc_frame_accept_ms;
        tcp_request.frame_id = g_detect_frame_id;
        tcp_request_ptr = &tcp_request;
    }
    rt_mutex_release(g_detect_lock);

    ret = camera_app_capture_ai_input_with_tcp_frame_ex(s_detect_ai_input,
                                                        sizeof(s_detect_ai_input),
                                                        1500U,
                                                        tcp_request_ptr,
                                                        &capture_metrics);
    capture_end = rt_tick_get();
    capture_ms = capture_metrics.total_ms;
    if (capture_ms == 0U)
    {
        capture_ms = (uint16_t)((capture_end - capture_start) * 1000U / RT_TICK_PER_SECOND);
    }
    if (ret != RT_EOK)
    {
#if DETECT_APP_SUSPEND_PREVIEW_DURING_AI
        {
            rt_uint8_t normal_priority = DETECT_APP_THREAD_PRIORITY;
            rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &normal_priority);
        }
#endif
        LOG_W("capture ai input failed: %d", ret);
        return ret;
    }

    detect_app_set_stage(UI_DETECT_STAGE_AI);
#ifdef APP_USING_EDGE_IMPULSE_V15
    ret = ei_v15_run_rgb88896(s_detect_ai_input,
                              sizeof(s_detect_ai_input),
                              out_result);
#else
    ret = ai_app_run_placeholder(s_detect_ai_input,
                                 sizeof(s_detect_ai_input),
                                 out_result);
#endif

#if DETECT_APP_SUSPEND_PREVIEW_DURING_AI
    {
        rt_uint8_t normal_priority = DETECT_APP_THREAD_PRIORITY;
        rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &normal_priority);
        camera_app_set_lcd_preview_suspended(0U);
    }
#endif

    if (ret != RT_EOK)
    {
        LOG_W("run inference failed: %d", ret);
        return ret;
    }

    out_result->capture_ms = capture_ms;
    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    g_detect_last_camera_wait_ms = capture_metrics.wait_ms;
    g_detect_last_preprocess_ms = capture_metrics.preprocess_ms;
    g_detect_last_capture_copy_ms = capture_metrics.copy_ms;
    rt_mutex_release(g_detect_lock);

    if (out_result->threshold_x10 == 0U)
    {
        out_result->threshold_x10 = g_detect_profile->threshold_x10;
    }

    return RT_EOK;
}

static uint16_t detect_app_calc_yield_x10(void)
{
    if (g_detect_total_count == 0U)
    {
        return 1000U;
    }

    return (uint16_t)(((uint64_t)g_detect_ok_count * 1000ULL +
                       (uint64_t)(g_detect_total_count / 2U)) /
                      (uint64_t)g_detect_total_count);
}

static int detect_app_append_record(const ui_detect_snapshot_t *snapshot)
{
    storage_detect_record_t record;
    char timestamp[16];

    if (snapshot == RT_NULL)
    {
        return -RT_ERROR;
    }

    detect_app_make_timestamp(timestamp, sizeof(timestamp));
    record.timestamp = timestamp;
    record.model = snapshot->model.name;
    record.anomaly = snapshot->ai.anomaly ? RT_TRUE : RT_FALSE;
    record.score_x10 = snapshot->ai.score_x10;
    record.threshold_x10 = snapshot->ai.threshold_x10;
    record.duration_ms = snapshot->ai.ai_time_ms;
    record.image_path = "";

    if (storage_app_append_detect_record(&record) != RT_EOK)
    {
        LOG_W("append record failed: %s", storage_app_last_error());
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void detect_app_make_timestamp(char *buf, uint32_t buf_size)
{
    if ((buf == RT_NULL) || (buf_size == 0U))
    {
        return;
    }

    s_detect_record_id++;
    rt_snprintf(buf, buf_size, "D%06u", (unsigned int)s_detect_record_id);
}

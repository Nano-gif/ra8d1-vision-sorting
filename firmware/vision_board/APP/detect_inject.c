#include "detect_internal.h"

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
                             rt_bool_t trigger_sort)
{
    ai_app_result_t result;
    rt_bool_t busy;
    uint32_t cells;
    uint32_t center;
    uint32_t i;

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    memset(&result, 0, sizeof(result));
    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    busy = ((g_detect_running == RT_TRUE) || (g_detect_single_shot == RT_TRUE)) ? RT_TRUE : RT_FALSE;
    result.threshold_x10 = g_detect_snapshot.ai.threshold_x10;
    result.grid_w = g_detect_snapshot.model.grid_w;
    result.grid_h = g_detect_snapshot.model.grid_h;
    rt_mutex_release(g_detect_lock);

    if (busy == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    if (result.threshold_x10 == 0U)
    {
        result.threshold_x10 = AI_APP_THRESHOLD_X10;
    }
    result.score_x10 = score_x10;
    result.mean_x10 = mean_x10;
    result.duration_ms = duration_ms;
    result.capture_ms = capture_ms;
    result.dsp_ms = dsp_ms;
    result.classification_ms = classification_ms;
    result.anomaly_ms = anomaly_ms;
    result.postprocessing_ms = postprocessing_ms;
    result.signal_ms = signal_ms;
    result.result_ms = result_ms;
    result.tflm_setup_ms = tflm_setup_ms;
    result.tflm_allocate_ms = tflm_allocate_ms;
    result.tflm_invoke_ms = tflm_invoke_ms;
    result.tflm_output_ms = tflm_output_ms;
    result.tflm_graph0_ms = tflm_graph0_ms;
    result.tflm_graph1_ms = tflm_graph1_ms;
    result.anomaly = (score_x10 > result.threshold_x10) ? RT_TRUE : RT_FALSE;

    if (result.grid_w == 0U)
    {
        result.grid_w = AI_APP_GRID_W;
    }
    if (result.grid_h == 0U)
    {
        result.grid_h = AI_APP_GRID_H;
    }

    cells = (uint32_t)result.grid_w * (uint32_t)result.grid_h;
    if (cells > AI_APP_GRID_CELLS)
    {
        cells = AI_APP_GRID_CELLS;
    }
    if (hot_cells > cells)
    {
        hot_cells = (uint8_t)cells;
    }
    result.hot_cells = hot_cells;
    if ((hot_cells > 0U) && (cells > 0U))
    {
        center = ((uint32_t)(result.grid_h / 2U) * (uint32_t)result.grid_w) + (uint32_t)(result.grid_w / 2U);
        if (center >= cells)
        {
            center = 0U;
        }
        result.heatmap[center] = 220U;
        for (i = 1U; (i < hot_cells) && (i < cells); i++)
        {
            result.heatmap[(center + i) % cells] = 160U;
        }
    }

    if (trigger_sort == RT_TRUE)
    {
        rt_kprintf("[detect] mock result score=%u anomaly=%u trigger_sort=1\n",
                   (unsigned int)score_x10,
                   (unsigned int)result.anomaly);
    }
    else
    {
        rt_kprintf("[detect] mock result score=%u anomaly=%u trigger_sort=0\n",
                   (unsigned int)score_x10,
                   (unsigned int)result.anomaly);
    }

    return detect_app_apply_result(&result, capture_ms, trigger_sort);
}

static uint8_t detect_app_count_hot_cells(const uint8_t *heatmap, uint32_t cells)
{
    uint8_t count = 0U;
    uint32_t i;

    if (heatmap == RT_NULL)
    {
        return 0U;
    }

    if (cells > DETECT_APP_HEATMAP_CELLS)
    {
        cells = DETECT_APP_HEATMAP_CELLS;
    }

    for (i = 0U; i < cells; i++)
    {
        if (heatmap[i] > 0U)
        {
            if (count < 255U)
            {
                count++;
            }
        }
    }

    return count;
}

int detect_app_inject_pc_result(rt_bool_t anomaly,
                                uint16_t score_x10,
                                uint16_t mean_x10,
                                uint8_t hot_cells,
                                const uint8_t *heatmap,
                                uint8_t grid_w,
                                uint8_t grid_h,
                                uint16_t duration_ms)
{
    rt_bool_t busy;
    uint16_t threshold_x10;
    uint32_t cells;
    uint32_t i;

    if (detect_app_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if ((heatmap == RT_NULL) || (grid_w == 0U) || (grid_h == 0U))
    {
        return -RT_EINVAL;
    }

    cells = (uint32_t)grid_w * (uint32_t)grid_h;
    if ((cells == 0U) ||
        (grid_w > DETECT_APP_HEATMAP_MAX_W) ||
        (grid_h > DETECT_APP_HEATMAP_MAX_H) ||
        (cells > DETECT_APP_HEATMAP_CELLS))
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    busy = ((g_detect_running == RT_TRUE) || (g_detect_single_shot == RT_TRUE)) ? RT_TRUE : RT_FALSE;
    threshold_x10 = g_detect_snapshot.ai.threshold_x10;
    rt_mutex_release(g_detect_lock);

    if (busy == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    if (threshold_x10 == 0U)
    {
        threshold_x10 = AI_APP_THRESHOLD_X10;
    }

    if ((anomaly == RT_TRUE) && (score_x10 <= threshold_x10))
    {
        score_x10 = (threshold_x10 < 65535U) ? (uint16_t)(threshold_x10 + 1U) : threshold_x10;
    }
    else if ((anomaly == RT_FALSE) && (score_x10 > threshold_x10))
    {
        score_x10 = (threshold_x10 > 0U) ? (uint16_t)(threshold_x10 - 1U) : 0U;
    }

    if (hot_cells == 0U)
    {
        hot_cells = detect_app_count_hot_cells(heatmap, cells);
    }
    if (hot_cells > cells)
    {
        hot_cells = (cells > 255U) ? 255U : (uint8_t)cells;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    detect_app_clear_heatmap_locked();
    for (i = 0U; i < cells; i++)
    {
        g_detect_heatmap_scores[i] = (anomaly == RT_TRUE) ? heatmap[i] : 0U;
    }
    g_detect_heatmap.grid_w = grid_w;
    g_detect_heatmap.grid_h = grid_h;
    g_detect_heatmap.roi_x = DETECT_APP_ROI_X;
    g_detect_heatmap.roi_y = DETECT_APP_ROI_Y;
    g_detect_heatmap.roi_w = DETECT_APP_ROI_SIZE;
    g_detect_heatmap.roi_h = DETECT_APP_ROI_SIZE;
    g_detect_heatmap.score_map = g_detect_heatmap_scores;
    g_detect_heatmap.score_count = (anomaly == RT_TRUE) ? (uint16_t)cells : 0U;
    g_detect_snapshot.model.grid_w = grid_w;
    g_detect_snapshot.model.grid_h = grid_h;
    g_detect_snapshot.ai.threshold_x10 = threshold_x10;
    g_detect_frame_id++;
    rt_mutex_release(g_detect_lock);

    rt_kprintf("[detect] pc result %s score=%u threshold=%u hot=%u grid=%ux%u\n",
               (anomaly == RT_TRUE) ? "ng" : "ok",
               (unsigned int)score_x10,
               (unsigned int)threshold_x10,
               (unsigned int)hot_cells,
               (unsigned int)grid_w,
               (unsigned int)grid_h);

    detect_app_publish_result(score_x10,
                              mean_x10,
                              hot_cells,
                              duration_ms,
                              0U,
                              0U,
                              0U,
                              duration_ms,
                              0U,
                              0U,
                              0U,
                              0U,
                              0U,
                              0U,
                              0U,
                              0U,
                              0U);
    if (anomaly != RT_TRUE)
    {
        camera_app_set_detect_heatmap(RT_NULL);
        detect_app_notify_snapshot_changed();
    }
    return RT_EOK;
}

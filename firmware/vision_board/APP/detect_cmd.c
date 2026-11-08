#include "detect_internal.h"

static void detect_app_print_profile(const detect_app_model_profile_info_t *profile)
{
    if (profile == RT_NULL)
    {
        return;
    }

    rt_kprintf("[detect] model=%s input=%ux%u grid=%ux%u threshold=%u.%u arena=%u\n",
               profile->name,
               (unsigned int)profile->input_w,
               (unsigned int)profile->input_h,
               (unsigned int)profile->grid_w,
               (unsigned int)profile->grid_h,
               (unsigned int)(profile->threshold_x10 / 10U),
               (unsigned int)(profile->threshold_x10 % 10U),
               (unsigned int)profile->arena_size);
}

static int detect_app_init_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (detect_app_init() == RT_EOK)
    {
        rt_kprintf("[detect] init ok\n");
        return 0;
    }

    rt_kprintf("[detect] init failed\n");
    return -1;
}

static int detect_app_start_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    detect_app_handle_command_from(UI_DETECT_CMD_START, "msh");
    return 0;
}

static int detect_app_stop_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    detect_app_handle_command_from(UI_DETECT_CMD_STOP, "msh");
    return 0;
}

static int detect_app_once_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    detect_app_handle_command_from(UI_DETECT_CMD_REFRESH, "msh");
    rt_kprintf("[detect] once requested\n");
    return 0;
}

static int detect_app_stats_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (detect_app_init() != RT_EOK)
    {
        rt_kprintf("[detect] init failed\n");
        return -1;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    rt_kprintf("[detect] total=%u ok=%u ng=%u yield=%u.%u%% score=%u.%u threshold=%u.%u\n",
               (unsigned int)g_detect_total_count,
               (unsigned int)g_detect_ok_count,
               (unsigned int)(g_detect_total_count - g_detect_ok_count),
               (unsigned int)(g_detect_snapshot.ai.yield_x10 / 10U),
               (unsigned int)(g_detect_snapshot.ai.yield_x10 % 10U),
               (unsigned int)(g_detect_snapshot.ai.score_x10 / 10U),
               (unsigned int)(g_detect_snapshot.ai.score_x10 % 10U),
               (unsigned int)(g_detect_snapshot.ai.threshold_x10 / 10U),
               (unsigned int)(g_detect_snapshot.ai.threshold_x10 % 10U));
    rt_kprintf("[detect] frame_id=%lu\n", (unsigned long)g_detect_snapshot.camera.frame_id);
    rt_kprintf("[detect] wall cap=%u ms ai=%u ms dsp=%u ms cls=%u ms anomaly=%u ms post=%u ms\n",
               (unsigned int)g_detect_last_capture_ms,
               (unsigned int)g_detect_snapshot.ai.ai_time_ms,
               (unsigned int)g_detect_last_dsp_ms,
               (unsigned int)g_detect_last_classification_ms,
               (unsigned int)g_detect_last_anomaly_ms,
               (unsigned int)g_detect_last_postprocessing_ms);
    rt_kprintf("[detect] cap_detail wait=%u ms preprocess=%u ms copy=%u ms total=%u ms\n",
               (unsigned int)g_detect_last_camera_wait_ms,
               (unsigned int)g_detect_last_preprocess_ms,
               (unsigned int)g_detect_last_capture_copy_ms,
               (unsigned int)g_detect_last_capture_ms);
    rt_kprintf("[detect] tflm setup=%u ms alloc=%u ms invoke=%u ms out=%u ms sig=%u ms result=%u ms g0=%u ms g1=%u ms\n",
               (unsigned int)g_detect_last_tflm_setup_ms,
               (unsigned int)g_detect_last_tflm_allocate_ms,
               (unsigned int)g_detect_last_tflm_invoke_ms,
               (unsigned int)g_detect_last_tflm_output_ms,
               (unsigned int)g_detect_last_signal_ms,
               (unsigned int)g_detect_last_result_ms,
               (unsigned int)g_detect_last_tflm_graph0_ms,
               (unsigned int)g_detect_last_tflm_graph1_ms);
    rt_kprintf("[detect] pc_frame enabled=%u port=%u chunk=%u accept_ms=%lu\n",
               (unsigned int)g_detect_pc_frame_upload_enabled,
               (unsigned int)g_detect_pc_frame_port,
               (unsigned int)g_detect_pc_frame_chunk,
               (unsigned long)g_detect_pc_frame_accept_ms);
    rt_mutex_release(g_detect_lock);
    return 0;
}

static uint16_t detect_mock_current_threshold(void)
{
    uint16_t threshold_x10 = AI_APP_THRESHOLD_X10;

    if (detect_app_init() == RT_EOK)
    {
        rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
        if (g_detect_snapshot.ai.threshold_x10 > 0U)
        {
            threshold_x10 = g_detect_snapshot.ai.threshold_x10;
        }
        rt_mutex_release(g_detect_lock);
    }

    return threshold_x10;
}

static rt_bool_t detect_mock_parse_u16_arg(const char *text, uint16_t *out_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0') || (out_value == RT_NULL))
    {
        return RT_FALSE;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return RT_FALSE;
    }

    if (value > 0xFFFFUL)
    {
        value = 0xFFFFUL;
    }

    *out_value = (uint16_t)value;
    return RT_TRUE;
}

static rt_bool_t detect_parse_u32_arg(const char *text, uint32_t *out_value)
{
    unsigned long value;
    char *end_ptr;

    if ((text == RT_NULL) || (text[0] == '\0') || (out_value == RT_NULL))
    {
        return RT_FALSE;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return RT_FALSE;
    }

    *out_value = (uint32_t)value;
    return RT_TRUE;
}

static int detect_pc_frame_cmd(int argc, char **argv)
{
    rt_bool_t enabled;
    uint16_t port;
    uint16_t chunk;
    uint32_t accept_ms;

    if (detect_app_init() != RT_EOK)
    {
        rt_kprintf("[detect] init failed\n");
        return -1;
    }

    if (argc > 1)
    {
        if ((strcmp(argv[1], "on") == 0) || (strcmp(argv[1], "1") == 0))
        {
            uint32_t parsed;

            port = DETECT_APP_PC_FRAME_DEFAULT_PORT;
            chunk = DETECT_APP_PC_FRAME_DEFAULT_CHUNK;
            accept_ms = DETECT_APP_PC_FRAME_DEFAULT_ACCEPT_MS;

            if (argc > 2)
            {
                if ((detect_parse_u32_arg(argv[2], &parsed) == RT_FALSE) ||
                    (parsed == 0U) ||
                    (parsed > 65535U))
                {
                    rt_kprintf("[detect] bad pc frame port: %s\n", argv[2]);
                    return -1;
                }
                port = (uint16_t)parsed;
            }
            if (argc > 3)
            {
                if ((detect_parse_u32_arg(argv[3], &parsed) == RT_FALSE) ||
                    (parsed == 0U) ||
                    (parsed > 4096U))
                {
                    rt_kprintf("[detect] bad pc frame chunk: %s\n", argv[3]);
                    return -1;
                }
                chunk = (uint16_t)parsed;
            }
            if (argc > 4)
            {
                if (detect_parse_u32_arg(argv[4], &parsed) == RT_FALSE)
                {
                    rt_kprintf("[detect] bad pc frame accept_ms: %s\n", argv[4]);
                    return -1;
                }
                accept_ms = parsed;
            }

            rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
            g_detect_pc_frame_port = port;
            g_detect_pc_frame_chunk = chunk;
            g_detect_pc_frame_accept_ms = accept_ms;
            g_detect_pc_frame_upload_enabled = RT_TRUE;
            rt_mutex_release(g_detect_lock);
        }
        else if ((strcmp(argv[1], "off") == 0) || (strcmp(argv[1], "0") == 0))
        {
            rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
            g_detect_pc_frame_upload_enabled = RT_FALSE;
            rt_mutex_release(g_detect_lock);
        }
        else
        {
            rt_kprintf("[detect] usage: detect_pc_frame [on|off] [port] [chunk] [accept_ms]\n");
            return -1;
        }
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    enabled = g_detect_pc_frame_upload_enabled;
    port = g_detect_pc_frame_port;
    chunk = g_detect_pc_frame_chunk;
    accept_ms = g_detect_pc_frame_accept_ms;
    rt_mutex_release(g_detect_lock);

    rt_kprintf("[detect] pc_frame enabled=%u port=%u chunk=%u accept_ms=%lu\n",
               (unsigned int)enabled,
               (unsigned int)port,
               (unsigned int)chunk,
               (unsigned long)accept_ms);
    return 0;
}

static int detect_mock_result_cmd(int argc, char **argv)
{
    uint16_t score_x10;
    uint16_t mean_x10;
    uint16_t threshold_x10;
    uint16_t override_score;
    uint8_t hot_cells;
    rt_bool_t trigger_sort = RT_FALSE;
    rt_bool_t desired_ng = RT_FALSE;
    const char *state;
    int arg_index;
    int ret;

    if (argc < 2)
    {
        rt_kprintf("[detect] usage: detect_mock_result <ok|ng> [score_x10] [sort]\n");
        rt_kprintf("[detect] example: detect_mock_result ng sort\n");
        return -RT_EINVAL;
    }

    state = argv[1];
    threshold_x10 = detect_mock_current_threshold();
    if ((strcmp(state, "ok") == 0) || (strcmp(state, "0") == 0))
    {
        score_x10 = (threshold_x10 > 10U) ? (uint16_t)(threshold_x10 - 10U) : 0U;
        mean_x10 = (uint16_t)(score_x10 / 2U);
        hot_cells = 0U;
    }
    else if ((strcmp(state, "ng") == 0) || (strcmp(state, "1") == 0))
    {
        desired_ng = RT_TRUE;
        score_x10 = (threshold_x10 < 0xFFF5U) ? (uint16_t)(threshold_x10 + 10U) : 0xFFFFU;
        mean_x10 = (uint16_t)((score_x10 > 10U) ? (score_x10 - 10U) : score_x10);
        hot_cells = 3U;
    }
    else
    {
        rt_kprintf("[detect] usage: detect_mock_result <ok|ng> [score_x10] [sort]\n");
        return -RT_EINVAL;
    }

    for (arg_index = 2; arg_index < argc; arg_index++)
    {
        if (strcmp(argv[arg_index], "sort") == 0)
        {
            trigger_sort = RT_TRUE;
        }
        else if (detect_mock_parse_u16_arg(argv[arg_index], &override_score) == RT_TRUE)
        {
            score_x10 = override_score;
            mean_x10 = override_score;
        }
        else
        {
            rt_kprintf("[detect] usage: detect_mock_result <ok|ng> [score_x10] [sort]\n");
            return -RT_EINVAL;
        }
    }

    if ((desired_ng == RT_TRUE) && (score_x10 <= threshold_x10))
    {
        score_x10 = (threshold_x10 < 0xFFF5U) ? (uint16_t)(threshold_x10 + 10U) : 0xFFFFU;
        mean_x10 = score_x10;
    }
    else if ((desired_ng == RT_FALSE) && (score_x10 > threshold_x10))
    {
        score_x10 = (threshold_x10 > 10U) ? (uint16_t)(threshold_x10 - 10U) : 0U;
        mean_x10 = score_x10;
    }
    hot_cells = (desired_ng == RT_TRUE) ? 3U : 0U;

    ret = detect_app_inject_result(score_x10,
                                   mean_x10,
                                   hot_cells,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   0U,
                                   trigger_sort);
    rt_kprintf("[detect] mock result ret=%d\n", ret);
    return (ret == RT_EOK) ? 0 : -1;
}

static int detect_ei_profile_once_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef APP_USING_EDGE_IMPULSE_V15
    ei_v15_profile_next_inference();
    rt_kprintf("[detect] EI profiler enabled for next inference\n");
    rt_kprintf("[detect] run: detect_app_once_cmd\n");
    return 0;
#else
    rt_kprintf("[detect] EI v15 is not enabled\n");
    return -1;
#endif
}

static int detect_model_cmd(int argc, char **argv)
{
    const detect_app_model_profile_info_t *profile;
    detect_app_model_profile_t target;
    int result;

    if (detect_app_init() != RT_EOK)
    {
        rt_kprintf("[detect] init failed\n");
        return -1;
    }

    if (argc < 2)
    {
        rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
        profile = g_detect_profile;
        detect_app_print_profile(profile);
        rt_mutex_release(g_detect_lock);
        rt_kprintf("[detect] usage: detect_model_cmd v15|v8|v14\n");
        return 0;
    }

    if ((strcmp(argv[1], "v15") == 0) ||
        (strcmp(argv[1], "v8") == 0) ||
        (strcmp(argv[1], "96") == 0))
    {
        target = DETECT_APP_MODEL_PROFILE_V8_96;
    }
    else if ((strcmp(argv[1], "v14") == 0) || (strcmp(argv[1], "240") == 0))
    {
        target = DETECT_APP_MODEL_PROFILE_V14_240;
    }
    else
    {
        rt_kprintf("[detect] unknown model profile: %s\n", argv[1]);
        rt_kprintf("[detect] usage: detect_model_cmd v15|v8|v14\n");
        return -1;
    }

    result = detect_app_set_model_profile(target);
    if (result == -RT_EBUSY)
    {
        rt_kprintf("[detect] stop detect before switching model profile\n");
        return -1;
    }
    if (result != RT_EOK)
    {
        rt_kprintf("[detect] switch model profile failed\n");
        return -1;
    }

    rt_mutex_take(g_detect_lock, RT_WAITING_FOREVER);
    profile = g_detect_profile;
    detect_app_print_profile(profile);
    rt_mutex_release(g_detect_lock);
    return 0;
}

MSH_CMD_EXPORT(detect_app_init_cmd, initialize detect task framework);
MSH_CMD_EXPORT(detect_app_start_cmd, start detect loop);
MSH_CMD_EXPORT(detect_app_stop_cmd, stop detect loop);
MSH_CMD_EXPORT(detect_app_once_cmd, run one detect cycle);
MSH_CMD_EXPORT(detect_app_stats_cmd, show detect statistics);
MSH_CMD_EXPORT(detect_pc_frame_cmd, configure PC frame upload for detect cycles);
MSH_CMD_EXPORT(detect_mock_result_cmd, inject mock detect result);
MSH_CMD_EXPORT(detect_ei_profile_once_cmd, print EI TFLM per-op profile for next inference);
MSH_CMD_EXPORT(detect_model_cmd, show or switch detect model profile);

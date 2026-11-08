/*
 * 相机采集主线程：传感器初始化、LCD 预览循环、测试命令调度与 msh 命令。
 */
#include "camera_internal.h"

static struct rt_thread s_camera_thread;
static struct rt_semaphore s_camera_sem;
struct rt_semaphore g_camera_collect_save_sem;
struct rt_semaphore g_camera_ai_preprocess_sem;
struct rt_semaphore g_camera_tcp_service_sem;
rt_bool_t g_camera_thread_started = RT_FALSE;
volatile rt_bool_t g_camera_busy = RT_FALSE;
static volatile rt_bool_t s_camera_preview_stop = RT_FALSE;
static volatile rt_bool_t s_camera_lcd_preview_visible = RT_FALSE;
volatile rt_bool_t g_camera_lcd_preview_suspended = RT_FALSE;
static volatile rt_bool_t s_camera_lcd_layer_cfg_dirty = RT_TRUE;
static volatile uint8_t s_camera_lcd_preview_fps = CAMERA_APP_LCD_PREVIEW_DEFAULT_FPS;
static volatile uint16_t s_camera_lcd_preview_actual_fps_x10 = 0U;
static volatile int16_t s_camera_lcd_preview_x = CAMERA_APP_DEFAULT_PREVIEW_X;
static volatile int16_t s_camera_lcd_preview_y = CAMERA_APP_DEFAULT_PREVIEW_Y;
volatile rt_bool_t g_camera_collect_save_requested = RT_FALSE;
volatile rt_bool_t g_camera_collect_save_inited = RT_FALSE;
volatile rt_bool_t g_camera_ai_preprocess_requested = RT_FALSE;
volatile rt_bool_t g_camera_ai_preprocess_inited = RT_FALSE;
volatile rt_bool_t g_camera_ai_preprocess_verbose = RT_FALSE;
volatile rt_bool_t g_camera_tcp_frame_requested = RT_FALSE;
volatile rt_bool_t g_camera_tcp_frame_in_progress = RT_FALSE;
volatile int g_camera_tcp_frame_result = -RT_ERROR;
volatile uint16_t g_camera_tcp_server_port = CAMERA_APP_TCP_DEFAULT_PORT;
volatile uint16_t g_camera_tcp_server_chunk = CAMERA_APP_TCP_DEFAULT_CHUNK;
volatile uint32_t g_camera_tcp_accept_ms = CAMERA_APP_TCP_DEFAULT_ACCEPT_MS;
volatile uint32_t g_camera_tcp_frame_id = 0U;
static volatile camera_app_cmd_t s_camera_pending_cmd = CAMERA_APP_CMD_NONE;
static rt_bool_t s_camera_sensor_ready = RT_FALSE;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static rt_uint8_t s_camera_thread_stack[CAMERA_APP_THREAD_STACK];

static uint8_t s_camera_frame[CAMERA_APP_FRAME_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

uint8_t g_camera_ai_input[AI_APP_INPUT_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

/* tick 转毫秒，饱和到 16 位。 */
uint16_t camera_app_ticks_to_ms(rt_tick_t ticks)
{
    uint32_t ms = (uint32_t)((uint64_t)ticks * 1000ULL / RT_TICK_PER_SECOND);

    return (ms > 0xFFFFU) ? 0xFFFFU : (uint16_t)ms;
}

/* 解析无符号整数参数，非法时返回默认值。 */
static uint32_t camera_app_parse_u32(const char *text, uint32_t default_value)
{
    char *end_ptr;
    unsigned long value;

    if (text == RT_NULL)
    {
        return default_value;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return default_value;
    }

    return (uint32_t)value;
}

/* 按 32 字节对齐清理 D-Cache（写回内存）。 */
void camera_app_clean_dcache(const void *addr, uint32_t size)
{
#if defined(RENESAS_CORTEX_M85) && defined(BSP_CFG_DCACHE_ENABLED) && BSP_CFG_DCACHE_ENABLED
    uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)addr + size + 31U) & ~(uintptr_t)31U;

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)size;
#endif
}

/* 按 32 字节对齐失效 D-Cache（读取 DMA 新数据前）。 */
static void camera_app_invalidate_dcache(const void *addr, uint32_t size)
{
#if defined(RENESAS_CORTEX_M85) && defined(BSP_CFG_DCACHE_ENABLED) && BSP_CFG_DCACHE_ENABLED
    uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)addr + size + 31U) & ~(uintptr_t)31U;

    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)size;
#endif
}

/* 仅清理叠加层脏区对应的缓存行。 */
static void camera_app_clean_dcache_area(const void *framebuffer, const camera_app_dirty_area_t *dirty)
{
    uintptr_t start_addr;
    uintptr_t end_addr;
    uint32_t size;

    if ((framebuffer == RT_NULL) || (dirty == RT_NULL) || (dirty->valid == RT_FALSE))
    {
        return;
    }

    start_addr = (uintptr_t)framebuffer +
                 (((uint32_t)dirty->y1 * CAMERA_APP_WIDTH) + (uint32_t)dirty->x1) * CAMERA_APP_BYTES_PER_PIXEL;
    end_addr = (uintptr_t)framebuffer +
               (((uint32_t)dirty->y2 * CAMERA_APP_WIDTH) + (uint32_t)dirty->x2 + 1U) * CAMERA_APP_BYTES_PER_PIXEL;
    size = (uint32_t)(end_addr - start_addr);

    camera_app_clean_dcache((const void *)start_addr, size);
}

/* 隐藏 GLCDC 图层 2。 */
static int camera_app_lcd_layer_hide(void)
{
#if GLCDC_CFG_LAYER_2_ENABLE
    return (int)R_GLCDC_BufferChange(&g_display0_ctrl, RT_NULL, DISPLAY_FRAME_LAYER_2);
#else
    return RT_EOK;
#endif
}

/* 设置图层 2 的显示位置与帧缓冲。 */
static int camera_app_lcd_layer_set_window(int16_t x, int16_t y, uint8_t *framebuffer)
{
#if GLCDC_CFG_LAYER_2_ENABLE
    display_runtime_cfg_t cfg;

    cfg.input = g_display0_cfg.input[DISPLAY_FRAME_LAYER_2];
    cfg.layer = g_display0_cfg.layer[DISPLAY_FRAME_LAYER_2];
    cfg.input.p_base = (uint32_t *)framebuffer;
    cfg.layer.coordinate.x = x;
    cfg.layer.coordinate.y = y;

    return (int)R_GLCDC_LayerChange(&g_display0_ctrl, &cfg, DISPLAY_FRAME_LAYER_2);
#else
    (void)x;
    (void)y;
    (void)framebuffer;
    return RT_EOK;
#endif
}

/* 按目标帧率计算每帧周期。 */
static rt_tick_t camera_app_lcd_frame_period_tick(void)
{
    uint8_t fps = s_camera_lcd_preview_fps;

    if (fps < CAMERA_APP_LCD_PREVIEW_MIN_FPS)
    {
        fps = CAMERA_APP_LCD_PREVIEW_MIN_FPS;
    }
    else if (fps > CAMERA_APP_LCD_PREVIEW_MAX_FPS)
    {
        fps = CAMERA_APP_LCD_PREVIEW_MAX_FPS;
    }

    return rt_tick_from_millisecond(1000U / fps);
}

/* 更新实际预览帧率（x10）。 */
static void camera_app_set_lcd_actual_fps(uint32_t frames, rt_tick_t elapsed_ticks)
{
    uint32_t fps_x10;

    if ((frames == 0U) || (elapsed_ticks == 0U))
    {
        s_camera_lcd_preview_actual_fps_x10 = 0U;
        return;
    }

    fps_x10 = (frames * (uint32_t)RT_TICK_PER_SECOND * 10U + ((uint32_t)elapsed_ticks / 2U)) /
              (uint32_t)elapsed_ticks;
    if (fps_x10 > 9999U)
    {
        fps_x10 = 9999U;
    }

    s_camera_lcd_preview_actual_fps_x10 = (uint16_t)fps_x10;
}

/* 清零图层 2 双缓冲。 */
static void camera_app_lcd_clear_buffers(void)
{
#if GLCDC_CFG_LAYER_2_ENABLE
    rt_memset(fb_foreground[0], 0, sizeof(fb_foreground[0]));
    rt_memset(fb_foreground[1], 0, sizeof(fb_foreground[1]));
    camera_app_clean_dcache(fb_foreground[0], sizeof(fb_foreground[0]));
    camera_app_clean_dcache(fb_foreground[1], sizeof(fb_foreground[1]));
#endif
}

/* 打印步骤结果，非 0 视为失败。 */
static int camera_app_check_result(const char *step, int ret)
{
    if (ret != 0)
    {
        rt_kprintf("[camera] %s failed: %d\r\n", step, ret);
        return -RT_ERROR;
    }

    rt_kprintf("[camera] %s ok\r\n", step);
    return RT_EOK;
}

/* 打印帧缓冲非零字节数与前 16 字节，用于采集自检。 */
static void camera_app_print_frame_probe(void)
{
    uint32_t i;
    uint32_t non_zero_count = 0;

    for (i = 0; i < sizeof(s_camera_frame); i++)
    {
        if (s_camera_frame[i] != 0U)
        {
            non_zero_count++;
        }
    }

    rt_kprintf("[camera] frame addr=0x%08x size=%u nonzero=%u\r\n",
               (unsigned int)s_camera_frame,
               (unsigned int)sizeof(s_camera_frame),
               (unsigned int)non_zero_count);
    rt_kprintf("[camera] first bytes: %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
               s_camera_frame[0], s_camera_frame[1], s_camera_frame[2], s_camera_frame[3],
               s_camera_frame[4], s_camera_frame[5], s_camera_frame[6], s_camera_frame[7],
               s_camera_frame[8], s_camera_frame[9], s_camera_frame[10], s_camera_frame[11],
               s_camera_frame[12], s_camera_frame[13], s_camera_frame[14], s_camera_frame[15]);
}

/* 初始化传感器为 RGB565 QVGA（仅首次）。 */
static int camera_app_prepare_sensor(void)
{
    int ret;

    if (s_camera_sensor_ready == RT_TRUE)
    {
        return RT_EOK;
    }

    ret = sensor_init();
    if (camera_app_check_result("sensor_init", ret) != RT_EOK)
    {
        return -RT_ERROR;
    }

    ret = sensor_reset();
    if (camera_app_check_result("sensor_reset", ret) != RT_EOK)
    {
        return -RT_ERROR;
    }

    ret = sensor_set_pixformat(PIXFORMAT_RGB565);
    if (camera_app_check_result("set RGB565", ret) != RT_EOK)
    {
        return -RT_ERROR;
    }

    ret = sensor_set_framesize(FRAMESIZE_QVGA);
    if (camera_app_check_result("set QVGA", ret) != RT_EOK)
    {
        return -RT_ERROR;
    }

    s_camera_sensor_ready = RT_TRUE;
    return RT_EOK;
}

/* 单帧采集测试。 */
static void camera_app_run_once(void)
{
    int ret;

    rt_kprintf("[camera] test start\r\n");

    if (camera_app_prepare_sensor() != RT_EOK)
    {
        goto exit;
    }

    rt_memset(s_camera_frame, 0, sizeof(s_camera_frame));

    ret = sensor_snapshot(&sensor, s_camera_frame, 0);
    if (camera_app_check_result("snapshot", ret) != RT_EOK)
    {
        goto exit;
    }

    camera_app_print_frame_probe();

exit:
    rt_kprintf("[camera] test end\r\n");
}

/*
 * 抓拍一帧 QVGA RGB565 并保存为 BMP（测试命令）。
 * 通过 storage_app 的采集会话保存到 /vision/collect_photos/session_xxx/NNNN.bmp。
 */
static void camera_app_run_save_bmp_test(void)
{
    int ret;
    char saved_path[STORAGE_PATH_MAX];

    rt_kprintf("[camera] save bmp test start\r\n");

    if (camera_app_prepare_sensor() != RT_EOK)
    {
        goto exit;
    }

    rt_memset(s_camera_frame, 0, sizeof(s_camera_frame));

    ret = sensor_snapshot(&sensor, s_camera_frame, 0);
    if (camera_app_check_result("snapshot", ret) != RT_EOK)
    {
        goto exit;
    }

    camera_app_invalidate_dcache(s_camera_frame, CAMERA_APP_FRAME_BYTES);

    ret = storage_app_collect_save_rgb565_bmp((const uint16_t *)s_camera_frame,
                                              CAMERA_APP_WIDTH,
                                              CAMERA_APP_HEIGHT,
                                              saved_path,
                                              sizeof(saved_path));
    if (ret == RT_EOK)
    {
        rt_kprintf("[camera] save bmp ok: %s\r\n", saved_path);
    }
    else
    {
        rt_kprintf("[camera] save bmp failed: %s\r\n", storage_app_last_error());
    }

exit:
    rt_kprintf("[camera] save bmp test end\r\n");
}

/* 采集一帧并通过 TCP 服务端发送（独占相机线程）。 */
static void camera_app_run_tcp_frame_server(void)
{
    int ret;
    uint16_t port = g_camera_tcp_server_port;
    uint16_t chunk = g_camera_tcp_server_chunk;
    uint32_t accept_ms = g_camera_tcp_accept_ms;

    rt_kprintf("[camera] tcp frame server start port=%u chunk=%u accept_ms=%lu\r\n",
               (unsigned int)port,
               (unsigned int)chunk,
               (unsigned long)accept_ms);

    if (camera_app_prepare_sensor() != RT_EOK)
    {
        goto exit;
    }

    rt_memset(s_camera_frame, 0, sizeof(s_camera_frame));

    ret = sensor_snapshot(&sensor, s_camera_frame, 0);
    if (camera_app_check_result("snapshot", ret) != RT_EOK)
    {
        goto exit;
    }

    camera_app_invalidate_dcache(s_camera_frame, CAMERA_APP_FRAME_BYTES);
    g_camera_tcp_frame_in_progress = RT_TRUE;
    ret = camera_app_send_tcp_frame_payload(s_camera_frame, port, chunk, accept_ms, 0U);
    g_camera_tcp_frame_result = ret;
    g_camera_tcp_frame_in_progress = RT_FALSE;

exit:
    rt_kprintf("[camera] tcp frame server end\r\n");
}

/* 无 LCD 的采集循环：只服务门限检测与各类帧请求。 */
static void camera_app_run_preview(void)
{
    rt_kprintf("[camera] preview start\r\n");

    if (camera_app_prepare_sensor() != RT_EOK)
    {
        goto exit;
    }

    while (s_camera_preview_stop == RT_FALSE)
    {
        int ret = sensor_snapshot(&sensor, s_camera_frame, 0);
        if (ret != 0)
        {
            rt_kprintf("[camera] preview snapshot failed: %d\r\n", ret);
            break;
        }

        camera_app_invalidate_dcache(s_camera_frame, CAMERA_APP_FRAME_BYTES);
        software_gate_app_process_rgb565((const uint16_t *)s_camera_frame, CAMERA_APP_WIDTH, CAMERA_APP_HEIGHT);
        camera_app_handle_ai_preprocess_request(s_camera_frame);
        camera_app_handle_tcp_frame_request(s_camera_frame);
        camera_app_handle_tcp_service_request(s_camera_frame);

        rt_thread_yield();
    }

exit:
    s_camera_preview_stop = RT_FALSE;
    rt_kprintf("[camera] preview stop\r\n");
}

/* LCD 预览主循环：采集、处理帧请求、绘制叠加层并双缓冲切换显示。 */
static void camera_app_run_lcd_preview(void)
{
#if GLCDC_CFG_LAYER_2_ENABLE
    uint32_t frames = 0;
    uint32_t buffer_index = 0;
    rt_bool_t layer_visible = RT_FALSE;
    int16_t active_x = CAMERA_APP_DEFAULT_PREVIEW_X;
    int16_t active_y = CAMERA_APP_DEFAULT_PREVIEW_Y;
    rt_tick_t last_tick;

    rt_kprintf("[camera] lcd preview start\r\n");

    if (camera_app_prepare_sensor() != RT_EOK)
    {
        goto exit;
    }

    if ((sizeof(fb_foreground[0]) < CAMERA_APP_FRAME_BYTES) ||
        (DISPLAY_HSIZE_INPUT1 != CAMERA_APP_WIDTH) ||
        (DISPLAY_VSIZE_INPUT1 != CAMERA_APP_HEIGHT))
    {
        rt_kprintf("[camera] lcd layer size mismatch, layer=%ux%u size=%u, camera=%ux%u size=%u\r\n",
                   (unsigned int)DISPLAY_HSIZE_INPUT1,
                   (unsigned int)DISPLAY_VSIZE_INPUT1,
                   (unsigned int)sizeof(fb_foreground[0]),
                   (unsigned int)CAMERA_APP_WIDTH,
                   (unsigned int)CAMERA_APP_HEIGHT,
                   (unsigned int)CAMERA_APP_FRAME_BYTES);
        goto exit;
    }

    camera_app_lcd_clear_buffers();
    (void)camera_app_lcd_layer_hide();
    last_tick = rt_tick_get();

    while (s_camera_preview_stop == RT_FALSE)
    {
        rt_tick_t frame_start_tick = rt_tick_get();
        rt_tick_t frame_period_tick;
        uint8_t *framebuffer = fb_foreground[buffer_index];
        camera_app_dirty_area_t dirty;
        int16_t target_x = s_camera_lcd_preview_x;
        int16_t target_y = s_camera_lcd_preview_y;
        int ret;

        if (s_camera_lcd_preview_visible == RT_FALSE)
        {
            if (layer_visible == RT_TRUE)
            {
                ret = camera_app_lcd_layer_hide();
                if (ret == FSP_SUCCESS)
                {
                    layer_visible = RT_FALSE;
                }
                else if (ret != FSP_ERR_INVALID_UPDATE_TIMING)
                {
                    rt_kprintf("[camera] lcd layer hide failed: %d\r\n", ret);
                }
            }

            frames = 0;
            last_tick = rt_tick_get();
            s_camera_lcd_preview_actual_fps_x10 = 0U;
            /* 预览隐藏时仍持续采集，保证 TCP 服务与 AI 预处理能拿到实时帧。 */
            ret = sensor_snapshot(&sensor, framebuffer, 0);
            if (ret != 0)
            {
                rt_kprintf("[camera] lcd hidden snapshot failed: %d\r\n", ret);
                rt_thread_delay(rt_tick_from_millisecond(CAMERA_APP_HIDDEN_DELAY_MS));
                continue;
            }

            camera_app_invalidate_dcache(framebuffer, CAMERA_APP_FRAME_BYTES);
            camera_app_handle_collect_save_request(framebuffer);
            camera_app_handle_ai_preprocess_request(framebuffer);
            camera_app_handle_tcp_frame_request(framebuffer);
            camera_app_handle_tcp_service_request(framebuffer);
            rt_thread_delay(rt_tick_from_millisecond(CAMERA_APP_HIDDEN_DELAY_MS));
            continue;
        }

        if (g_camera_lcd_preview_suspended == RT_TRUE)
        {
            /* AI 推理期间冻结当前图层；不能隐藏图层，否则下层 LVGL 占位图露出导致闪烁。 */
            frames = 0;
            last_tick = rt_tick_get();
            s_camera_lcd_preview_actual_fps_x10 = 0U;
            ret = sensor_snapshot(&sensor, framebuffer, 0);
            if (ret != 0)
            {
                rt_kprintf("[camera] lcd suspended snapshot failed: %d\r\n", ret);
                rt_thread_delay(rt_tick_from_millisecond(CAMERA_APP_HIDDEN_DELAY_MS));
                continue;
            }

            camera_app_invalidate_dcache(framebuffer, CAMERA_APP_FRAME_BYTES);
            camera_app_handle_collect_save_request(framebuffer);
            camera_app_handle_ai_preprocess_request(framebuffer);
            camera_app_handle_tcp_frame_request(framebuffer);
            camera_app_handle_tcp_service_request(framebuffer);
            rt_thread_delay(rt_tick_from_millisecond(CAMERA_APP_HIDDEN_DELAY_MS));
            continue;
        }

        frame_period_tick = camera_app_lcd_frame_period_tick();

        if ((s_camera_lcd_layer_cfg_dirty == RT_TRUE) ||
            (active_x != target_x) ||
            (active_y != target_y))
        {
            ret = camera_app_lcd_layer_set_window(target_x, target_y, framebuffer);
            if (ret == FSP_SUCCESS)
            {
                active_x = target_x;
                active_y = target_y;
                layer_visible = RT_TRUE;
                s_camera_lcd_layer_cfg_dirty = RT_FALSE;
            }
            else if (ret != FSP_ERR_INVALID_UPDATE_TIMING)
            {
                rt_kprintf("[camera] lcd layer window failed: %d\r\n", ret);
                break;
            }

            if (ret != FSP_SUCCESS)
            {
                rt_thread_yield();
                continue;
            }
        }

        ret = sensor_snapshot(&sensor, framebuffer, 0);

        if (ret != 0)
        {
            rt_kprintf("[camera] lcd preview snapshot failed: %d\r\n", ret);
            break;
        }

        camera_app_invalidate_dcache(framebuffer, CAMERA_APP_FRAME_BYTES);
        software_gate_app_process_rgb565((const uint16_t *)framebuffer, CAMERA_APP_WIDTH, CAMERA_APP_HEIGHT);
        camera_app_handle_collect_save_request(framebuffer);
        camera_app_handle_ai_preprocess_request(framebuffer);
        camera_app_handle_tcp_frame_request(framebuffer);
        camera_app_handle_tcp_service_request(framebuffer);
        if (g_camera_lcd_preview_suspended == RT_TRUE)
        {
            /* AI 已取走本帧输入：保持画面冻结，跳过叠加/缓存/显示，避免与推理争抢资源。 */
            continue;
        }
        camera_app_dirty_area_reset(&dirty);
        camera_app_draw_overlay(framebuffer, &dirty);
        camera_app_clean_dcache_area(framebuffer, &dirty);

        ret = R_GLCDC_BufferChange(&g_display0_ctrl, framebuffer, DISPLAY_FRAME_LAYER_2);
        if (ret == FSP_SUCCESS)
        {
            buffer_index ^= 1U;
            layer_visible = RT_TRUE;
        }
        else if (ret != FSP_ERR_INVALID_UPDATE_TIMING)
        {
            rt_kprintf("[camera] lcd buffer change failed: %d\r\n", ret);
            break;
        }

        frames++;

        if ((rt_tick_get() - last_tick) >= rt_tick_from_millisecond(CAMERA_APP_FPS_PERIOD_MS))
        {
            camera_app_set_lcd_actual_fps(frames, rt_tick_get() - last_tick);
            frames = 0;
            last_tick = rt_tick_get();
        }

        if (frame_period_tick > 0)
        {
            rt_tick_t elapsed_tick = rt_tick_get() - frame_start_tick;
            if (elapsed_tick < frame_period_tick)
            {
                rt_thread_delay(frame_period_tick - elapsed_tick);
            }
            else
            {
                rt_thread_yield();
            }
        }
        else
        {
            rt_thread_yield();
        }
    }

exit:
    (void)camera_app_lcd_layer_hide();
    s_camera_lcd_preview_actual_fps_x10 = 0U;
    s_camera_preview_stop = RT_FALSE;
    rt_kprintf("[camera] lcd preview stop\r\n");
#else
    rt_kprintf("[camera] GLCDC layer 2 is not enabled\r\n");
    s_camera_lcd_preview_actual_fps_x10 = 0U;
    s_camera_preview_stop = RT_FALSE;
#endif
}

/* 相机线程：等待命令并执行对应采集任务。 */
static void camera_app_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        camera_app_cmd_t cmd;

        rt_sem_take(&s_camera_sem, RT_WAITING_FOREVER);

        cmd = s_camera_pending_cmd;
        s_camera_pending_cmd = CAMERA_APP_CMD_NONE;

        switch (cmd)
        {
        case CAMERA_APP_CMD_TEST_ONCE:
            camera_app_run_once();
            break;
        case CAMERA_APP_CMD_PREVIEW:
            camera_app_run_preview();
            break;
        case CAMERA_APP_CMD_LCD_PREVIEW:
            camera_app_run_lcd_preview();
            break;
        case CAMERA_APP_CMD_SAVE_BMP_TEST:
            camera_app_run_save_bmp_test();
            break;
        case CAMERA_APP_CMD_TCP_FRAME_SERVER:
            camera_app_run_tcp_frame_server();
            break;
        default:
            break;
        }

        g_camera_busy = RT_FALSE;
    }
}

/* 初始化相机线程及信号量（仅首次）。 */
static int camera_app_thread_init_once(void)
{
    rt_err_t err;

    if (g_camera_thread_started == RT_TRUE)
    {
        return RT_EOK;
    }

    err = rt_sem_init(&s_camera_sem, "camsem", 0, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] sem init failed: %d\r\n", err);
        return err;
    }

    err = rt_sem_init(&g_camera_collect_save_sem, "camsave", 0, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] save sem init failed: %d\r\n", err);
        rt_sem_detach(&s_camera_sem);
        return err;
    }
    g_camera_collect_save_inited = RT_TRUE;

    err = rt_sem_init(&g_camera_ai_preprocess_sem, "camaipp", 0, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] ai sem init failed: %d\r\n", err);
        rt_sem_detach(&g_camera_collect_save_sem);
        g_camera_collect_save_inited = RT_FALSE;
        rt_sem_detach(&s_camera_sem);
        return err;
    }
    g_camera_ai_preprocess_inited = RT_TRUE;

    err = rt_sem_init(&g_camera_tcp_service_sem, "camtcps", 0, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] tcp service sem init failed: %d\r\n", err);
        rt_sem_detach(&g_camera_ai_preprocess_sem);
        g_camera_ai_preprocess_inited = RT_FALSE;
        rt_sem_detach(&g_camera_collect_save_sem);
        g_camera_collect_save_inited = RT_FALSE;
        rt_sem_detach(&s_camera_sem);
        return err;
    }

    err = rt_thread_init(&s_camera_thread,
                         "cam_app",
                         camera_app_thread_entry,
                         RT_NULL,
                         &s_camera_thread_stack[0],
                         sizeof(s_camera_thread_stack),
                         CAMERA_APP_THREAD_PRIO,
                         CAMERA_APP_THREAD_TICK);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] thread init failed: %d\r\n", err);
        rt_sem_detach(&g_camera_tcp_service_sem);
        rt_sem_detach(&g_camera_ai_preprocess_sem);
        g_camera_ai_preprocess_inited = RT_FALSE;
        rt_sem_detach(&g_camera_collect_save_sem);
        g_camera_collect_save_inited = RT_FALSE;
        rt_sem_detach(&s_camera_sem);
        return err;
    }

    err = rt_thread_startup(&s_camera_thread);
    if (err != RT_EOK)
    {
        rt_kprintf("[camera] thread startup failed: %d\r\n", err);
        rt_thread_detach(&s_camera_thread);
        rt_sem_detach(&g_camera_tcp_service_sem);
        rt_sem_detach(&g_camera_ai_preprocess_sem);
        g_camera_ai_preprocess_inited = RT_FALSE;
        rt_sem_detach(&g_camera_collect_save_sem);
        g_camera_collect_save_inited = RT_FALSE;
        rt_sem_detach(&s_camera_sem);
        return err;
    }

    g_camera_thread_started = RT_TRUE;
    return RT_EOK;
}

/* 提交单帧采集测试。 */
void camera_app_start_test(void)
{
    rt_base_t level;

    if (camera_app_thread_init_once() != RT_EOK)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (g_camera_busy == RT_TRUE)
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] test already running\r\n");
        return;
    }
    g_camera_busy = RT_TRUE;
    s_camera_preview_stop = RT_TRUE;
    s_camera_pending_cmd = CAMERA_APP_CMD_TEST_ONCE;
    rt_hw_interrupt_enable(level);

    rt_sem_release(&s_camera_sem);
}

/* 提交单帧 BMP 保存测试。 */
void camera_app_save_bmp_test(void)
{
    rt_base_t level;

    if (camera_app_thread_init_once() != RT_EOK)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (g_camera_busy == RT_TRUE)
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] camera task already running\r\n");
        return;
    }
    g_camera_busy = RT_TRUE;
    s_camera_preview_stop = RT_TRUE;
    s_camera_pending_cmd = CAMERA_APP_CMD_SAVE_BMP_TEST;
    rt_hw_interrupt_enable(level);

    rt_sem_release(&s_camera_sem);
}

/* 请求发送一帧 TCP 图像：相机空闲时独占执行，预览中则排到下一帧。 */
void camera_app_tcp_frame_server(uint16_t port, uint16_t chunk, uint32_t accept_timeout_ms)
{
    rt_base_t level;
    rt_bool_t camera_busy;

    if (camera_app_thread_init_once() != RT_EOK)
    {
        return;
    }

    if (port == 0U)
    {
        port = CAMERA_APP_TCP_DEFAULT_PORT;
    }
    if (chunk == 0U)
    {
        chunk = CAMERA_APP_TCP_DEFAULT_CHUNK;
    }
    if (accept_timeout_ms == 0U)
    {
        accept_timeout_ms = CAMERA_APP_TCP_DEFAULT_ACCEPT_MS;
    }

    level = rt_hw_interrupt_disable();
    if ((g_camera_tcp_frame_requested == RT_TRUE) || (g_camera_tcp_frame_in_progress == RT_TRUE))
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] tcp frame already pending or sending\r\n");
        return;
    }

    g_camera_tcp_server_port = port;
    g_camera_tcp_server_chunk = chunk;
    g_camera_tcp_accept_ms = accept_timeout_ms;
    g_camera_tcp_frame_id = 0U;
    g_camera_tcp_frame_result = -RT_ERROR;
    camera_busy = g_camera_busy;
    if (camera_busy == RT_TRUE)
    {
        g_camera_tcp_frame_requested = RT_TRUE;
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] tcp frame queued for next preview frame\r\n");
        return;
    }

    g_camera_busy = RT_TRUE;
    s_camera_preview_stop = RT_TRUE;
    s_camera_pending_cmd = CAMERA_APP_CMD_TCP_FRAME_SERVER;
    rt_hw_interrupt_enable(level);

    rt_sem_release(&s_camera_sem);
}

/* 启动无 LCD 采集循环。 */
void camera_app_start_preview(void)
{
    rt_base_t level;

    if (camera_app_thread_init_once() != RT_EOK)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (g_camera_busy == RT_TRUE)
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] camera task already running\r\n");
        return;
    }

    g_camera_busy = RT_TRUE;
    s_camera_preview_stop = RT_FALSE;
    s_camera_pending_cmd = CAMERA_APP_CMD_PREVIEW;
    rt_hw_interrupt_enable(level);

    rt_sem_release(&s_camera_sem);
}

/* 启动 LCD 预览循环。 */
void camera_app_start_lcd_preview(void)
{
    rt_base_t level;

    if (camera_app_thread_init_once() != RT_EOK)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (g_camera_busy == RT_TRUE)
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] camera task already running\r\n");
        return;
    }

    g_camera_busy = RT_TRUE;
    s_camera_preview_stop = RT_FALSE;
    s_camera_pending_cmd = CAMERA_APP_CMD_LCD_PREVIEW;
    rt_hw_interrupt_enable(level);

    rt_sem_release(&s_camera_sem);
}

/* 请求停止预览循环。 */
void camera_app_stop_preview(void)
{
    s_camera_preview_stop = RT_TRUE;
    rt_kprintf("[camera] preview stop requested\r\n");
}

/* msh：预处理下一帧并打印校验信息。 */
static int camera_app_ai_preprocess_test(int argc, char **argv)
{
    rt_base_t level;
    int ret;

    (void)argc;
    (void)argv;

    level = rt_hw_interrupt_disable();
    g_camera_ai_preprocess_verbose = RT_TRUE;
    rt_hw_interrupt_enable(level);

    ret = camera_app_capture_ai_input(g_camera_ai_input, sizeof(g_camera_ai_input), 2000U);
    if (ret != RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        g_camera_ai_preprocess_verbose = RT_FALSE;
        rt_hw_interrupt_enable(level);
        rt_kprintf("[camera] ai preprocess failed: %d\n", ret);
        return -1;
    }

    return 0;
}

/* msh：camera_tcp_server_send [port] [chunk] [accept_ms]。 */
static int camera_app_tcp_frame_server_cmd(int argc, char **argv)
{
    uint32_t port = CAMERA_APP_TCP_DEFAULT_PORT;
    uint32_t chunk = CAMERA_APP_TCP_DEFAULT_CHUNK;
    uint32_t accept_ms = CAMERA_APP_TCP_DEFAULT_ACCEPT_MS;

    if (argc > 1)
    {
        port = camera_app_parse_u32(argv[1], CAMERA_APP_TCP_DEFAULT_PORT);
        if ((port == 0U) || (port > 65535U))
        {
            rt_kprintf("[camera] bad port: %s\r\n", argv[1]);
            return -1;
        }
    }

    if (argc > 2)
    {
        chunk = camera_app_parse_u32(argv[2], CAMERA_APP_TCP_DEFAULT_CHUNK);
        if ((chunk == 0U) || (chunk > 4096U))
        {
            rt_kprintf("[camera] bad chunk: %s\r\n", argv[2]);
            return -1;
        }
    }

    if (argc > 3)
    {
        accept_ms = camera_app_parse_u32(argv[3], CAMERA_APP_TCP_DEFAULT_ACCEPT_MS);
    }

    rt_kprintf("[camera] tcp frame request port=%lu chunk=%lu accept_ms=%lu\r\n",
               (unsigned long)port,
               (unsigned long)chunk,
               (unsigned long)accept_ms);
    camera_app_tcp_frame_server((uint16_t)port, (uint16_t)chunk, accept_ms);
    return 0;
}

/* msh：打印相机任务状态。 */
static int camera_app_stat_cmd(int argc, char **argv)
{
    rt_base_t level;
    rt_bool_t busy;
    rt_bool_t preview_stop;
    rt_bool_t lcd_visible;
    rt_bool_t lcd_suspended;
    rt_bool_t collect_pending;
    rt_bool_t ai_pending;
    rt_bool_t tcp_pending;
    rt_bool_t tcp_sending;
    uint16_t tcp_port;
    uint16_t tcp_chunk;
    uint32_t tcp_accept_ms;
    int tcp_result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    level = rt_hw_interrupt_disable();
    busy = g_camera_busy;
    preview_stop = s_camera_preview_stop;
    lcd_visible = s_camera_lcd_preview_visible;
    lcd_suspended = g_camera_lcd_preview_suspended;
    collect_pending = g_camera_collect_save_requested;
    ai_pending = g_camera_ai_preprocess_requested;
    tcp_pending = g_camera_tcp_frame_requested;
    tcp_sending = g_camera_tcp_frame_in_progress;
    tcp_port = g_camera_tcp_server_port;
    tcp_chunk = g_camera_tcp_server_chunk;
    tcp_accept_ms = g_camera_tcp_accept_ms;
    tcp_result = g_camera_tcp_frame_result;
    rt_hw_interrupt_enable(level);

    rt_kprintf("[camera] thread=%s sensor=%s busy=%u preview_stop=%u lcd_visible=%u suspended=%u fps=%u.%u\n",
               (g_camera_thread_started == RT_TRUE) ? "on" : "off",
               (s_camera_sensor_ready == RT_TRUE) ? "ready" : "not_ready",
               (unsigned int)busy,
               (unsigned int)preview_stop,
               (unsigned int)lcd_visible,
               (unsigned int)lcd_suspended,
               (unsigned int)(s_camera_lcd_preview_actual_fps_x10 / 10U),
               (unsigned int)(s_camera_lcd_preview_actual_fps_x10 % 10U));
    rt_kprintf("[camera] pending collect=%u ai=%u tcp=%u sending=%u tcp_result=%d port=%u chunk=%u accept_ms=%lu\n",
               (unsigned int)collect_pending,
               (unsigned int)ai_pending,
               (unsigned int)tcp_pending,
               (unsigned int)tcp_sending,
               tcp_result,
               (unsigned int)tcp_port,
               (unsigned int)tcp_chunk,
               (unsigned long)tcp_accept_ms);
    return 0;
}

/* 设置预览可见性（保持当前位置）。 */
void camera_app_set_lcd_preview_visible(uint8_t visible)
{
    camera_app_set_lcd_preview_window(visible,
                                      s_camera_lcd_preview_x,
                                      s_camera_lcd_preview_y);
}

/* 设置预览可见性与位置。 */
void camera_app_set_lcd_preview_window(uint8_t visible, int16_t x, int16_t y)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t layer_cfg_changed = ((s_camera_lcd_preview_x != x) || (s_camera_lcd_preview_y != y)) ? RT_TRUE : RT_FALSE;

    s_camera_lcd_preview_x = x;
    s_camera_lcd_preview_y = y;
    s_camera_lcd_preview_visible = (visible != 0U) ? RT_TRUE : RT_FALSE;
    if (layer_cfg_changed == RT_TRUE)
    {
        s_camera_lcd_layer_cfg_dirty = RT_TRUE;
    }

    rt_hw_interrupt_enable(level);
}

/* 设置目标预览帧率（限幅）。 */
void camera_app_set_lcd_preview_fps(uint8_t fps)
{
    if (fps < CAMERA_APP_LCD_PREVIEW_MIN_FPS)
    {
        fps = CAMERA_APP_LCD_PREVIEW_MIN_FPS;
    }
    else if (fps > CAMERA_APP_LCD_PREVIEW_MAX_FPS)
    {
        fps = CAMERA_APP_LCD_PREVIEW_MAX_FPS;
    }

    s_camera_lcd_preview_fps = fps;
}

/* 实际预览帧率 x10。 */
uint16_t camera_app_get_lcd_preview_fps_x10(void)
{
    return s_camera_lcd_preview_actual_fps_x10;
}

/* 冻结/恢复预览显示（采集不停）。 */
void camera_app_set_lcd_preview_suspended(uint8_t suspended)
{
    g_camera_lcd_preview_suspended = (suspended != 0U) ? RT_TRUE : RT_FALSE;
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(camera_app_start_test, camera_test, run camera capture test);
MSH_CMD_EXPORT_ALIAS(camera_app_save_bmp_test, camera_save_bmp_test, capture one frame and save BMP to TF card);
MSH_CMD_EXPORT_ALIAS(camera_app_start_preview, camera_preview_start, start camera capture preview test);
MSH_CMD_EXPORT_ALIAS(camera_app_start_lcd_preview, camera_preview_lcd_start, start camera lcd preview test);
MSH_CMD_EXPORT_ALIAS(camera_app_stop_preview, camera_preview_stop, stop camera capture preview test);
MSH_CMD_EXPORT(camera_app_ai_preprocess_test, preprocess next camera frame for AI input test);
MSH_CMD_EXPORT_ALIAS(camera_app_tcp_frame_server_cmd, camera_tcp_server_send, capture one frame and send it through W5500 TCP server);
MSH_CMD_EXPORT_ALIAS(camera_app_stat_cmd, camera_stat, show camera task state);
#endif

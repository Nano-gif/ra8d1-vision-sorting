/*
 * 相机帧请求：其他线程向相机线程请求 AI 预处理、TCP 发帧与采集保存，由相机线程在采到新帧后完成。
 */
#include "camera_internal.h"

static volatile int s_camera_collect_save_result = -RT_ERROR;
static volatile rt_bool_t s_camera_ai_suspend_after_preprocess = RT_FALSE;
static volatile int s_camera_ai_preprocess_result = -RT_ERROR;
static volatile uint16_t s_camera_ai_preprocess_ms = 0U;
static volatile rt_bool_t s_camera_tcp_service_requested = RT_FALSE;
static volatile rt_bool_t s_camera_tcp_service_payload_ready = RT_FALSE;
static volatile int s_camera_tcp_service_result = -RT_ERROR;
static volatile uint32_t s_camera_tcp_service_frame_id = 0U;
static uint32_t s_camera_tcp_service_payload_seq = 0U;
static uint32_t s_camera_tcp_frame_seq = 0U;
static char s_camera_collect_save_path[STORAGE_PATH_MAX];
static uint8_t s_camera_tcp_payload[CAMERA_APP_TCP_PAYLOAD_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

/* tick 转 32 位毫秒。 */
static uint32_t camera_app_ticks_to_u32_ms(rt_tick_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) / RT_TICK_PER_SECOND);
}

/* 小端写入 16 位。 */
static void camera_app_put_le16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xffU);
    buf[1] = (uint8_t)((value >> 8) & 0xffU);
}

/* 小端写入 32 位。 */
static void camera_app_put_le32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xffU);
    buf[1] = (uint8_t)((value >> 8) & 0xffU);
    buf[2] = (uint8_t)((value >> 16) & 0xffU);
    buf[3] = (uint8_t)((value >> 24) & 0xffU);
}

/* 构建 32 字节 VIMG 帧头（RGB565 小端 320x240）。 */
static void camera_app_build_tcp_frame_header(uint8_t *header, uint32_t seq)
{
    rt_memset(header, 0, CAMERA_APP_TCP_HEADER_BYTES);
    header[0] = (uint8_t)'V';
    header[1] = (uint8_t)'I';
    header[2] = (uint8_t)'M';
    header[3] = (uint8_t)'G';
    camera_app_put_le16(&header[4], CAMERA_APP_TCP_PROTOCOL_VERSION);
    camera_app_put_le16(&header[6], CAMERA_APP_TCP_HEADER_BYTES);
    camera_app_put_le16(&header[8], CAMERA_APP_WIDTH);
    camera_app_put_le16(&header[10], CAMERA_APP_HEIGHT);
    camera_app_put_le16(&header[12], CAMERA_APP_TCP_FORMAT_RGB565_LE);
    camera_app_put_le16(&header[14], 0U);
    camera_app_put_le32(&header[16], CAMERA_APP_FRAME_BYTES);
    camera_app_put_le32(&header[20], seq);
    camera_app_put_le32(&header[24], camera_app_ticks_to_u32_ms(rt_tick_get()));
    camera_app_put_le32(&header[28], 0U);
}

/* 生成帧序号：指定 frame_id 时沿用并推进自增序号。 */
static uint32_t camera_app_next_tcp_frame_seq(uint32_t frame_id)
{
    uint32_t seq;

    if (frame_id != 0U)
    {
        seq = frame_id;
        if (s_camera_tcp_frame_seq <= frame_id)
        {
            s_camera_tcp_frame_seq = frame_id + 1U;
        }
    }
    else
    {
        seq = s_camera_tcp_frame_seq++;
    }

    return seq;
}

/* 把帧头与图像拷入发送缓冲并清理缓存。 */
static int camera_app_build_tcp_frame_payload(const uint8_t *framebuffer, uint32_t frame_id, uint32_t *out_seq)
{
    uint32_t seq;

    if (framebuffer == RT_NULL)
    {
        return -RT_ERROR;
    }

    seq = camera_app_next_tcp_frame_seq(frame_id);
    camera_app_build_tcp_frame_header(s_camera_tcp_payload, seq);
    rt_memcpy(&s_camera_tcp_payload[CAMERA_APP_TCP_HEADER_BYTES], framebuffer, CAMERA_APP_FRAME_BYTES);
    camera_app_clean_dcache(s_camera_tcp_payload, CAMERA_APP_TCP_PAYLOAD_BYTES);

    if (out_seq != RT_NULL)
    {
        *out_seq = seq;
    }

    return RT_EOK;
}

/*
 * 通过 w5500_eth_tcp_server_send 发送一帧 VIMG（camera_tcp_server_send 诊断命令用）。
 * 常驻的 camera_tcp_service 不走这里，而是复用已建立的连接发送预先构建的载荷。
 */
int camera_app_send_tcp_frame_payload(const uint8_t *framebuffer,
                                      uint16_t port,
                                      uint16_t chunk,
                                      uint32_t accept_ms,
                                      uint32_t frame_id)
{
    int ret;
    uint32_t seq;

    if (framebuffer == RT_NULL)
    {
        return -RT_ERROR;
    }

    (void)camera_app_build_tcp_frame_payload(framebuffer, frame_id, &seq);

    rt_kprintf("[camera] tcp frame ready seq=%lu size=%lu bytes, connect PC now\r\n",
               (unsigned long)seq,
               (unsigned long)CAMERA_APP_TCP_PAYLOAD_BYTES);

    ret = w5500_eth_tcp_server_send(port,
                                    s_camera_tcp_payload,
                                    CAMERA_APP_TCP_PAYLOAD_BYTES,
                                    chunk,
                                    accept_ms);
    if (ret == 0)
    {
        rt_kprintf("[camera] tcp frame sent seq=%lu\r\n", (unsigned long)seq);
    }
    else
    {
        rt_kprintf("[camera] tcp frame send failed ret=%d\r\n", ret);
    }

    return ret;
}

/* 处理采集保存请求：把当前帧保存为 BMP。 */
void camera_app_handle_collect_save_request(const uint8_t *framebuffer)
{
    int ret;

    if (g_camera_collect_save_requested == RT_FALSE)
    {
        return;
    }

    ret = storage_app_collect_save_rgb565_bmp((const uint16_t *)framebuffer,
                                               CAMERA_APP_WIDTH,
                                               CAMERA_APP_HEIGHT,
                                               s_camera_collect_save_path,
                                               sizeof(s_camera_collect_save_path));

    s_camera_collect_save_result = ret;
    g_camera_collect_save_requested = RT_FALSE;
    rt_sem_release(&g_camera_collect_save_sem);
}

/* 处理 AI 预处理请求：当前帧裁剪缩放为 AI 输入。 */
void camera_app_handle_ai_preprocess_request(const uint8_t *framebuffer)
{
    rt_tick_t start_tick;
    rt_tick_t elapsed_tick;
    int ret;
    rt_bool_t verbose;

    if (g_camera_ai_preprocess_requested == RT_FALSE)
    {
        return;
    }

    /* 必须在叠加层绘制之前调用，否则 AI 输入会混入 ROI/热力图像素。预处理期间临时提升线程优先级。 */
    {
        rt_uint8_t ai_priority = CAMERA_APP_AI_PREPROCESS_PRIO;
        rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &ai_priority);
    }

    start_tick = rt_tick_get();
    ret = ai_app_preprocess_rgb565_to_rgb88896((const uint16_t *)framebuffer,
                                               g_camera_ai_input,
                                               sizeof(g_camera_ai_input));
    elapsed_tick = rt_tick_get() - start_tick;

    {
        rt_uint8_t normal_priority = CAMERA_APP_THREAD_PRIO;
        rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &normal_priority);
    }

    s_camera_ai_preprocess_ms = camera_app_ticks_to_ms(elapsed_tick);
    verbose = g_camera_ai_preprocess_verbose;

    if ((ret == RT_EOK) && (verbose == RT_TRUE))
    {
        uint32_t checksum = 0U;
        uint32_t i;

        for (i = 0U; i < sizeof(g_camera_ai_input); i += 97U)
        {
            checksum += g_camera_ai_input[i];
        }

        rt_kprintf("[camera] ai preprocess ok: ticks=%u bytes=%u checksum=%u\n",
                   (unsigned int)elapsed_tick,
                   (unsigned int)sizeof(g_camera_ai_input),
                   (unsigned int)checksum);
        rt_kprintf("[camera] ai first rgb: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   g_camera_ai_input[0],
                   g_camera_ai_input[1],
                   g_camera_ai_input[2],
                   g_camera_ai_input[3],
                   g_camera_ai_input[4],
                   g_camera_ai_input[5],
                   g_camera_ai_input[6],
                   g_camera_ai_input[7],
                   g_camera_ai_input[8],
                   g_camera_ai_input[9],
                   g_camera_ai_input[10],
                   g_camera_ai_input[11]);
    }
    else if (ret != RT_EOK)
    {
        rt_kprintf("[camera] ai preprocess failed: %d\n", ret);
    }

    s_camera_ai_preprocess_result = ret;
    g_camera_ai_preprocess_requested = RT_FALSE;
    g_camera_ai_preprocess_verbose = RT_FALSE;
    if ((ret == RT_EOK) && (s_camera_ai_suspend_after_preprocess == RT_TRUE))
    {
        s_camera_ai_suspend_after_preprocess = RT_FALSE;
        g_camera_lcd_preview_suspended = RT_TRUE;
    }
    else
    {
        s_camera_ai_suspend_after_preprocess = RT_FALSE;
    }
    rt_sem_release(&g_camera_ai_preprocess_sem);
}

/* 处理排队的诊断 TCP 发帧请求。 */
void camera_app_handle_tcp_frame_request(const uint8_t *framebuffer)
{
    uint16_t port;
    uint16_t chunk;
    uint32_t accept_ms;
    uint32_t frame_id;
    int ret;

    if (g_camera_tcp_frame_requested == RT_FALSE)
    {
        return;
    }

    port = g_camera_tcp_server_port;
    chunk = g_camera_tcp_server_chunk;
    accept_ms = g_camera_tcp_accept_ms;
    frame_id = g_camera_tcp_frame_id;

    g_camera_tcp_frame_requested = RT_FALSE;
    g_camera_tcp_frame_id = 0U;
    g_camera_tcp_frame_in_progress = RT_TRUE;
    ret = camera_app_send_tcp_frame_payload(framebuffer, port, chunk, accept_ms, frame_id);
    g_camera_tcp_frame_result = ret;
    g_camera_tcp_frame_in_progress = RT_FALSE;
}

/*
 * 为常驻 TCP 服务构建 VIMG 载荷。
 * 相机线程是 sensor_snapshot() 的唯一调用者，这里复用主循环刚采到的帧，不再访问传感器。
 */
void camera_app_handle_tcp_service_request(const uint8_t *framebuffer)
{
    uint32_t seq;
    int ret;

    if (s_camera_tcp_service_requested == RT_FALSE)
    {
        return;
    }

    if (framebuffer == RT_NULL)
    {
        s_camera_tcp_service_result = -RT_ERROR;
        s_camera_tcp_service_payload_ready = RT_TRUE;
        s_camera_tcp_service_requested = RT_FALSE;
        rt_sem_release(&g_camera_tcp_service_sem);
        return;
    }

    ret = camera_app_build_tcp_frame_payload(framebuffer,
                                             s_camera_tcp_service_frame_id,
                                             &seq);
    if (ret == RT_EOK)
    {
        s_camera_tcp_service_payload_seq = seq;
        s_camera_tcp_service_result = RT_EOK;
    }
    else
    {
        s_camera_tcp_service_result = ret;
    }

    s_camera_tcp_service_payload_ready = RT_TRUE;
    s_camera_tcp_service_requested = RT_FALSE;
    rt_sem_release(&g_camera_tcp_service_sem);
}

/*
 * 常驻 TCP 服务请求一帧：置请求标志并等待相机线程用下一帧构建 VIMG 载荷。
 * frame_id 为 0 时自动编号；timeout_ms 为 0 表示一直等待。
 * 返回 RT_EOK 表示载荷就绪；-RT_EBUSY 已有请求未完成；-RT_ETIMEOUT 超时；
 * -RT_ERROR 相机线程未启动。
 */
int camera_app_request_tcp_service_frame(uint32_t frame_id, uint32_t timeout_ms)
{
    rt_tick_t timeout_tick;

    if (g_camera_thread_started == RT_FALSE)
    {
        return -RT_ERROR;
    }

    if (s_camera_tcp_service_requested == RT_TRUE)
    {
        return -RT_EBUSY;
    }

    /* 丢弃上次超时后相机线程迟到释放的完成信号，避免本次请求被误唤醒。 */
    while (rt_sem_take(&g_camera_tcp_service_sem, RT_WAITING_NO) == RT_EOK)
    {
    }

    s_camera_tcp_service_frame_id = frame_id;
    s_camera_tcp_service_payload_ready = RT_FALSE;
    s_camera_tcp_service_result = -RT_ERROR;
    s_camera_tcp_service_requested = RT_TRUE;

    timeout_tick = (timeout_ms == 0U) ? RT_WAITING_FOREVER : rt_tick_from_millisecond(timeout_ms);
    if (rt_sem_take(&g_camera_tcp_service_sem, timeout_tick) != RT_EOK)
    {
        s_camera_tcp_service_requested = RT_FALSE;
        return -RT_ETIMEOUT;
    }

    return RT_EOK;
}

/*
 * 取出已构建的 VIMG 载荷描述（data 指向内部缓冲，len 为帧头+图像总长）。
 * 返回构建结果；参数为空或载荷未就绪时返回 -RT_ERROR。
 */
int camera_app_get_tcp_service_payload(camera_app_tcp_frame_payload_t *payload)
{
    if (payload == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (s_camera_tcp_service_payload_ready != RT_TRUE)
    {
        return -RT_ERROR;
    }

    payload->data = s_camera_tcp_payload;
    payload->len = CAMERA_APP_TCP_PAYLOAD_BYTES;
    payload->seq = s_camera_tcp_service_payload_seq;
    return s_camera_tcp_service_result;
}

/* 发送完成（或失败）后复位载荷状态，防止旧载荷被重复发送。 */
void camera_app_finish_tcp_service_payload(void)
{
    s_camera_tcp_service_payload_ready = RT_FALSE;
    s_camera_tcp_service_result = -RT_ERROR;
    s_camera_tcp_service_frame_id = 0U;
}

/* 保存预览中的下一帧为 BMP，成功时返回保存路径。 */
int camera_app_collect_save_current_frame(char *out_path, uint32_t out_path_size, uint32_t timeout_ms)
{
    rt_base_t level;
    rt_err_t wait_ret;
    rt_tick_t timeout_tick;

    if ((out_path == RT_NULL) || (out_path_size == 0U))
    {
        return -RT_ERROR;
    }

    if ((g_camera_thread_started == RT_FALSE) ||
        (g_camera_collect_save_inited == RT_FALSE) ||
        (g_camera_busy == RT_FALSE))
    {
        rt_kprintf("[camera] collect save ignored: preview is not running\r\n");
        return -RT_ERROR;
    }

    if (rt_sem_take(&g_camera_collect_save_sem, RT_WAITING_NO) == RT_EOK)
    {
        /* 丢弃旧请求遗留的完成信号。 */
    }

    level = rt_hw_interrupt_disable();
    if (g_camera_collect_save_requested == RT_TRUE)
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;
    }

    s_camera_collect_save_result = -RT_ERROR;
    s_camera_collect_save_path[0] = '\0';
    g_camera_collect_save_requested = RT_TRUE;
    rt_hw_interrupt_enable(level);

    timeout_tick = rt_tick_from_millisecond(timeout_ms);
    if (timeout_tick == 0U)
    {
        timeout_tick = RT_WAITING_FOREVER;
    }

    wait_ret = rt_sem_take(&g_camera_collect_save_sem, timeout_tick);
    if (wait_ret != RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        g_camera_collect_save_requested = RT_FALSE;
        rt_hw_interrupt_enable(level);
        return -RT_ETIMEOUT;
    }

    if (s_camera_collect_save_result == RT_EOK)
    {
        rt_snprintf(out_path, out_path_size, "%s", s_camera_collect_save_path);
    }

    return s_camera_collect_save_result;
}

/*
 * 请求相机线程把下一帧预处理为 AI 输入（96x96 RGB888），可选同时发送一帧 TCP 图像。
 * 预处理在相机线程中、叠加层绘制之前完成，保证输入为原始图像。
 * 返回 RT_EOK 成功；-RT_ERROR 参数非法或相机未运行；-RT_EBUSY 已有请求；-RT_ETIMEOUT 超时。
 */
static int camera_app_capture_ai_input_internal(uint8_t *out_rgb888,
                                                uint32_t out_size,
                                                uint32_t timeout_ms,
                                                const camera_app_tcp_frame_request_t *tcp_request,
                                                camera_app_ai_capture_metrics_t *metrics)
{
    rt_base_t level;
    rt_err_t wait_ret;
    rt_tick_t timeout_tick;
    rt_tick_t total_start_tick;
    rt_tick_t wait_start_tick;
    rt_tick_t wait_end_tick;
    rt_tick_t copy_start_tick;
    uint16_t sem_wait_ms;
    uint16_t preprocess_ms;
    uint16_t copy_ms;
    uint16_t total_ms;

    if (metrics != RT_NULL)
    {
        rt_memset(metrics, 0, sizeof(*metrics));
    }

    if ((out_rgb888 == RT_NULL) || (out_size < AI_APP_INPUT_BYTES))
    {
        return -RT_ERROR;
    }

    total_start_tick = rt_tick_get();

    if ((g_camera_thread_started == RT_FALSE) ||
        (g_camera_ai_preprocess_inited == RT_FALSE) ||
        (g_camera_busy == RT_FALSE))
    {
        return -RT_ERROR;
    }

    if (rt_sem_take(&g_camera_ai_preprocess_sem, RT_WAITING_NO) == RT_EOK)
    {
        /* 丢弃旧请求遗留的完成信号。 */
    }

    level = rt_hw_interrupt_disable();
    if ((g_camera_ai_preprocess_requested == RT_TRUE) ||
        ((tcp_request != RT_NULL) &&
         ((g_camera_tcp_frame_requested == RT_TRUE) || (g_camera_tcp_frame_in_progress == RT_TRUE))))
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;
    }

    s_camera_ai_preprocess_result = -RT_ERROR;
    s_camera_ai_preprocess_ms = 0U;
    s_camera_ai_suspend_after_preprocess = (metrics != RT_NULL) ? RT_TRUE : RT_FALSE;
    if (tcp_request != RT_NULL)
    {
        uint16_t port = tcp_request->port;
        uint16_t chunk = tcp_request->chunk;
        uint32_t accept_ms = tcp_request->accept_timeout_ms;

        if (port == 0U)
        {
            port = CAMERA_APP_TCP_DEFAULT_PORT;
        }
        if (chunk == 0U)
        {
            chunk = CAMERA_APP_TCP_DEFAULT_CHUNK;
        }
        if (accept_ms == 0U)
        {
            accept_ms = CAMERA_APP_TCP_DEFAULT_ACCEPT_MS;
        }

        g_camera_tcp_server_port = port;
        g_camera_tcp_server_chunk = chunk;
        g_camera_tcp_accept_ms = accept_ms;
        g_camera_tcp_frame_id = tcp_request->frame_id;
        g_camera_tcp_frame_result = -RT_ERROR;
        g_camera_tcp_frame_requested = RT_TRUE;
    }
    g_camera_ai_preprocess_requested = RT_TRUE;
    wait_start_tick = rt_tick_get();
    rt_hw_interrupt_enable(level);

    timeout_tick = rt_tick_from_millisecond(timeout_ms);
    if (timeout_tick == 0U)
    {
        timeout_tick = RT_WAITING_FOREVER;
    }

    wait_ret = rt_sem_take(&g_camera_ai_preprocess_sem, timeout_tick);
    wait_end_tick = rt_tick_get();
    if (wait_ret != RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        g_camera_ai_preprocess_requested = RT_FALSE;
        g_camera_ai_preprocess_verbose = RT_FALSE;
        s_camera_ai_suspend_after_preprocess = RT_FALSE;
        if (tcp_request != RT_NULL)
        {
            g_camera_tcp_frame_requested = RT_FALSE;
            g_camera_tcp_frame_id = 0U;
        }
        rt_hw_interrupt_enable(level);
        if (metrics != RT_NULL)
        {
            metrics->wait_ms = camera_app_ticks_to_ms(wait_end_tick - wait_start_tick);
            metrics->total_ms = camera_app_ticks_to_ms(wait_end_tick - total_start_tick);
        }
        return -RT_ETIMEOUT;
    }

    if (s_camera_ai_preprocess_result != RT_EOK)
    {
        if (metrics != RT_NULL)
        {
            metrics->preprocess_ms = s_camera_ai_preprocess_ms;
            metrics->total_ms = camera_app_ticks_to_ms(wait_end_tick - total_start_tick);
        }
        return s_camera_ai_preprocess_result;
    }

    copy_start_tick = rt_tick_get();
    if (out_rgb888 != g_camera_ai_input)
    {
        rt_memcpy(out_rgb888, g_camera_ai_input, AI_APP_INPUT_BYTES);
    }
    copy_ms = camera_app_ticks_to_ms(rt_tick_get() - copy_start_tick);
    preprocess_ms = s_camera_ai_preprocess_ms;
    sem_wait_ms = camera_app_ticks_to_ms(wait_end_tick - wait_start_tick);
    total_ms = camera_app_ticks_to_ms(rt_tick_get() - total_start_tick);

    if (metrics != RT_NULL)
    {
        metrics->preprocess_ms = preprocess_ms;
        metrics->wait_ms = (sem_wait_ms > preprocess_ms) ? (uint16_t)(sem_wait_ms - preprocess_ms) : 0U;
        metrics->copy_ms = copy_ms;
        metrics->total_ms = total_ms;
    }

    return RT_EOK;
}

/* 获取 AI 输入并返回耗时统计。 */
int camera_app_capture_ai_input_ex(uint8_t *out_rgb888,
                                   uint32_t out_size,
                                   uint32_t timeout_ms,
                                   camera_app_ai_capture_metrics_t *metrics)
{
    return camera_app_capture_ai_input_internal(out_rgb888,
                                                out_size,
                                                timeout_ms,
                                                RT_NULL,
                                                metrics);
}

/* 获取 AI 输入，同时用同一帧发送 TCP 图像。 */
int camera_app_capture_ai_input_with_tcp_frame_ex(uint8_t *out_rgb888,
                                                  uint32_t out_size,
                                                  uint32_t timeout_ms,
                                                  const camera_app_tcp_frame_request_t *tcp_request,
                                                  camera_app_ai_capture_metrics_t *metrics)
{
    return camera_app_capture_ai_input_internal(out_rgb888,
                                                out_size,
                                                timeout_ms,
                                                tcp_request,
                                                metrics);
}

/* 获取 AI 输入。 */
int camera_app_capture_ai_input(uint8_t *out_rgb888, uint32_t out_size, uint32_t timeout_ms)
{
    return camera_app_capture_ai_input_ex(out_rgb888, out_size, timeout_ms, RT_NULL);
}

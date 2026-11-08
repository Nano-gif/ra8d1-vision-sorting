#include "ai_app.h"

#include <rtthread.h>
#include <string.h>

typedef struct
{
    uint16_t x0;
    uint16_t x1;
    uint16_t fx;
} ai_app_resize_x_t;

typedef struct
{
    uint32_t row0;
    uint32_t row1;
    uint16_t fy;
} ai_app_resize_y_t;

static ai_app_resize_x_t s_ai_app_resize_x[AI_APP_INPUT_W];
static ai_app_resize_y_t s_ai_app_resize_y[AI_APP_INPUT_H];
static rt_bool_t s_ai_app_resize_map_ready = RT_FALSE;

static void ai_app_init_resize_map(void);
static uint8_t ai_app_rgb565_r(uint16_t pixel);
static uint8_t ai_app_rgb565_g(uint16_t pixel);
static uint8_t ai_app_rgb565_b(uint16_t pixel);
static uint8_t ai_app_lerp256_u8(uint8_t a, uint8_t b, uint16_t t);

int ai_app_preprocess_rgb565_to_rgb88896(const uint16_t *src_rgb565,
                                         uint8_t *dst_rgb888,
                                         uint32_t dst_size)
{
    uint32_t dst_x;
    uint32_t dst_y;

    if ((src_rgb565 == RT_NULL) ||
        (dst_rgb888 == RT_NULL) ||
        (dst_size < AI_APP_INPUT_BYTES))
    {
        return -RT_ERROR;
    }

    ai_app_init_resize_map();

    for (dst_y = 0U; dst_y < AI_APP_INPUT_H; dst_y++)
    {
        const ai_app_resize_y_t *ymap = &s_ai_app_resize_y[dst_y];
        const uint16_t *row0 = src_rgb565 + ymap->row0;
        const uint16_t *row1 = src_rgb565 + ymap->row1;
        uint16_t fy = ymap->fy;

        for (dst_x = 0U; dst_x < AI_APP_INPUT_W; dst_x++)
        {
            const ai_app_resize_x_t *xmap = &s_ai_app_resize_x[dst_x];
            uint16_t p00;
            uint16_t p10;
            uint16_t p01;
            uint16_t p11;
            uint16_t fx;
            uint8_t r_top;
            uint8_t r_bottom;
            uint8_t g_top;
            uint8_t g_bottom;
            uint8_t b_top;
            uint8_t b_bottom;
            uint8_t *dst = dst_rgb888 + ((dst_y * AI_APP_INPUT_W + dst_x) * AI_APP_INPUT_CHANNELS);

            fx = xmap->fx;
            p00 = row0[xmap->x0];
            p10 = row0[xmap->x1];
            p01 = row1[xmap->x0];
            p11 = row1[xmap->x1];

            r_top = ai_app_lerp256_u8(ai_app_rgb565_r(p00), ai_app_rgb565_r(p10), fx);
            r_bottom = ai_app_lerp256_u8(ai_app_rgb565_r(p01), ai_app_rgb565_r(p11), fx);
            g_top = ai_app_lerp256_u8(ai_app_rgb565_g(p00), ai_app_rgb565_g(p10), fx);
            g_bottom = ai_app_lerp256_u8(ai_app_rgb565_g(p01), ai_app_rgb565_g(p11), fx);
            b_top = ai_app_lerp256_u8(ai_app_rgb565_b(p00), ai_app_rgb565_b(p10), fx);
            b_bottom = ai_app_lerp256_u8(ai_app_rgb565_b(p01), ai_app_rgb565_b(p11), fx);

            dst[0] = ai_app_lerp256_u8(r_top, r_bottom, fy);
            dst[1] = ai_app_lerp256_u8(g_top, g_bottom, fy);
            dst[2] = ai_app_lerp256_u8(b_top, b_bottom, fy);
        }
    }

    return RT_EOK;
}

int ai_app_run_placeholder(const uint8_t *input_rgb888,
                           uint32_t input_size,
                           ai_app_result_t *out_result)
{
    uint32_t sum = 0U;
    uint32_t i;
    uint8_t avg;

    if ((input_rgb888 == RT_NULL) ||
        (input_size != AI_APP_INPUT_BYTES) ||
        (out_result == RT_NULL))
    {
        return -RT_ERROR;
    }

    /*
     * 这不是真正的 AI 推理：只在未接入 Edge Impulse 推理库时
     * 为 detect_app 提供结构一致的占位结果。
     */
    for (i = 0U; i < input_size; i += 64U)
    {
        sum += input_rgb888[i];
    }
    avg = (uint8_t)(sum / (input_size / 64U));

    memset(out_result, 0, sizeof(*out_result));
    out_result->grid_w = AI_APP_GRID_W;
    out_result->grid_h = AI_APP_GRID_H;
    out_result->threshold_x10 = AI_APP_THRESHOLD_X10;
    out_result->mean_x10 = (uint16_t)((avg % 40U) + 10U);
    out_result->score_x10 = (uint16_t)((avg % 80U) + 20U);
    out_result->duration_ms = 0U;
    out_result->anomaly = (out_result->score_x10 > out_result->threshold_x10) ? RT_TRUE : RT_FALSE;

    if (out_result->anomaly == RT_TRUE)
    {
        out_result->hot_cells = 1U;
        out_result->heatmap[(AI_APP_GRID_H / 2U) * AI_APP_GRID_W + (AI_APP_GRID_W / 2U)] = 180U;
    }

    return RT_EOK;
}

void ai_app_print_info(void)
{
    rt_kprintf("[ai] camera=%ux%u RGB565 roi=%u,%u,%u,%u input=%ux%u RGB888 bytes=%u\n",
               (unsigned int)AI_APP_CAMERA_W,
               (unsigned int)AI_APP_CAMERA_H,
               (unsigned int)AI_APP_ROI_X,
               (unsigned int)AI_APP_ROI_Y,
               (unsigned int)AI_APP_ROI_SIZE,
               (unsigned int)AI_APP_ROI_SIZE,
               (unsigned int)AI_APP_INPUT_W,
               (unsigned int)AI_APP_INPUT_H,
               (unsigned int)AI_APP_INPUT_BYTES);
    rt_kprintf("[ai] visual_ad grid=%ux%u threshold=%u.%u arena=%u\n",
               (unsigned int)AI_APP_GRID_W,
               (unsigned int)AI_APP_GRID_H,
               (unsigned int)(AI_APP_THRESHOLD_X10 / 10U),
               (unsigned int)(AI_APP_THRESHOLD_X10 % 10U),
               (unsigned int)AI_APP_ARENA_SIZE);
}

static void ai_app_init_resize_map(void)
{
    uint32_t i;

    if (s_ai_app_resize_map_ready == RT_TRUE)
    {
        return;
    }

    /*
     * 定点双线性缩放映射表：src_fp 含 8 位小数。
     * 采用像素中心对齐映射，预先计算以省去每帧的除法与边界判断。
     */
    for (i = 0U; i < AI_APP_INPUT_W; i++)
    {
        uint32_t src_x_fp = (((i * 2U + 1U) * AI_APP_ROI_SIZE) << 7) / AI_APP_INPUT_W;
        uint32_t src_x_int;
        uint32_t x0;

        if (src_x_fp >= 128U)
        {
            src_x_fp -= 128U;
        }
        else
        {
            src_x_fp = 0U;
        }

        src_x_int = src_x_fp >> 8;
        s_ai_app_resize_x[i].fx = (uint16_t)(src_x_fp & 0xFFU);
        if (src_x_int >= (AI_APP_ROI_SIZE - 1U))
        {
            src_x_int = AI_APP_ROI_SIZE - 1U;
            s_ai_app_resize_x[i].fx = 0U;
        }

        x0 = AI_APP_ROI_X + src_x_int;
        s_ai_app_resize_x[i].x0 = (uint16_t)x0;
        s_ai_app_resize_x[i].x1 = (uint16_t)((x0 + 1U < AI_APP_CAMERA_W) ? (x0 + 1U) : x0);
    }

    for (i = 0U; i < AI_APP_INPUT_H; i++)
    {
        uint32_t src_y_fp = (((i * 2U + 1U) * AI_APP_ROI_SIZE) << 7) / AI_APP_INPUT_H;
        uint32_t src_y_int;
        uint32_t y0;
        uint32_t y1;

        if (src_y_fp >= 128U)
        {
            src_y_fp -= 128U;
        }
        else
        {
            src_y_fp = 0U;
        }

        src_y_int = src_y_fp >> 8;
        s_ai_app_resize_y[i].fy = (uint16_t)(src_y_fp & 0xFFU);
        if (src_y_int >= (AI_APP_ROI_SIZE - 1U))
        {
            src_y_int = AI_APP_ROI_SIZE - 1U;
            s_ai_app_resize_y[i].fy = 0U;
        }

        y0 = AI_APP_ROI_Y + src_y_int;
        y1 = (y0 + 1U < AI_APP_CAMERA_H) ? (y0 + 1U) : y0;
        s_ai_app_resize_y[i].row0 = y0 * AI_APP_CAMERA_W;
        s_ai_app_resize_y[i].row1 = y1 * AI_APP_CAMERA_W;
    }

    s_ai_app_resize_map_ready = RT_TRUE;
}

static uint8_t ai_app_rgb565_r(uint16_t pixel)
{
    uint8_t value = (uint8_t)((pixel >> 11) & 0x1FU);
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t ai_app_rgb565_g(uint16_t pixel)
{
    uint8_t value = (uint8_t)((pixel >> 5) & 0x3FU);
    return (uint8_t)((value << 2) | (value >> 4));
}

static uint8_t ai_app_rgb565_b(uint16_t pixel)
{
    uint8_t value = (uint8_t)(pixel & 0x1FU);
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t ai_app_lerp256_u8(uint8_t a, uint8_t b, uint16_t t)
{
    uint32_t left = (uint32_t)a * (uint32_t)(256U - t);
    uint32_t right = (uint32_t)b * (uint32_t)t;
    return (uint8_t)((left + right + 128U) >> 8);
}

static int ai_app_info_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ai_app_print_info();
    return 0;
}

MSH_CMD_EXPORT(ai_app_info_cmd, show AI preprocessing and model parameters);

/*
 * LCD 预览叠加层：检测 ROI、采集引导框、AI 热力图与软件门限包围框的绘制。
 */
#include "camera_internal.h"

static volatile camera_app_overlay_mode_t s_camera_overlay_mode = CAMERA_APP_OVERLAY_NONE;
static uint16_t s_detect_roi_x = CAMERA_APP_COLLECT_GUIDE_X;
static uint16_t s_detect_roi_y = 0U;
static uint16_t s_detect_roi_size = CAMERA_APP_DEFAULT_ROI_SIZE;
static camera_app_heatmap_t s_detect_heatmap = {0};
static uint8_t s_detect_heatmap_scores[CAMERA_APP_HEATMAP_MAX_CELLS];
static rt_tick_t s_detect_heatmap_expire_tick = 0;

/* RGB888 打包为 RGB565。 */
static uint16_t camera_app_pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((uint16_t)r & 0xF8U) << 8) |
                      (((uint16_t)g & 0xFCU) << 3) |
                      (((uint16_t)b) >> 3));
}

/* RGB565 按 alpha(0..255) 混合。 */
static uint16_t camera_app_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    uint32_t inv = 255U - alpha;
    uint32_t br = (bg >> 11) & 0x1FU;
    uint32_t bgc = (bg >> 5) & 0x3FU;
    uint32_t bb = bg & 0x1FU;
    uint32_t fr = (fg >> 11) & 0x1FU;
    uint32_t fgc = (fg >> 5) & 0x3FU;
    uint32_t fb = fg & 0x1FU;

    br = (br * inv + fr * alpha) / 255U;
    bgc = (bgc * inv + fgc * alpha) / 255U;
    bb = (bb * inv + fb * alpha) / 255U;

    return (uint16_t)((br << 11) | (bgc << 5) | bb);
}

/* 清空脏区。 */
void camera_app_dirty_area_reset(camera_app_dirty_area_t *dirty)
{
    if (dirty == RT_NULL)
    {
        return;
    }

    dirty->x1 = 0;
    dirty->y1 = 0;
    dirty->x2 = 0;
    dirty->y2 = 0;
    dirty->valid = RT_FALSE;
}

/* 把矩形并入脏区（裁剪到画面内）。 */
static void camera_app_dirty_area_add(camera_app_dirty_area_t *dirty,
                                      int16_t x,
                                      int16_t y,
                                      int16_t width,
                                      int16_t height)
{
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;

    if ((dirty == RT_NULL) || (width <= 0) || (height <= 0))
    {
        return;
    }

    x1 = x;
    y1 = y;
    x2 = (int16_t)(x + width - 1);
    y2 = (int16_t)(y + height - 1);

    if ((x2 < 0) || (y2 < 0) || (x1 >= (int16_t)CAMERA_APP_WIDTH) || (y1 >= (int16_t)CAMERA_APP_HEIGHT))
    {
        return;
    }

    if (x1 < 0)
    {
        x1 = 0;
    }
    if (y1 < 0)
    {
        y1 = 0;
    }
    if (x2 >= (int16_t)CAMERA_APP_WIDTH)
    {
        x2 = (int16_t)CAMERA_APP_WIDTH - 1;
    }
    if (y2 >= (int16_t)CAMERA_APP_HEIGHT)
    {
        y2 = (int16_t)CAMERA_APP_HEIGHT - 1;
    }

    if (dirty->valid == RT_FALSE)
    {
        dirty->x1 = x1;
        dirty->y1 = y1;
        dirty->x2 = x2;
        dirty->y2 = y2;
        dirty->valid = RT_TRUE;
        return;
    }

    if (x1 < dirty->x1)
    {
        dirty->x1 = x1;
    }
    if (y1 < dirty->y1)
    {
        dirty->y1 = y1;
    }
    if (x2 > dirty->x2)
    {
        dirty->x2 = x2;
    }
    if (y2 > dirty->y2)
    {
        dirty->y2 = y2;
    }
}

/* 半透明水平线。 */
static void camera_app_draw_hline(uint8_t *framebuffer,
                                  int16_t x,
                                  int16_t y,
                                  int16_t width,
                                  uint16_t color,
                                  uint8_t alpha,
                                  camera_app_dirty_area_t *dirty)
{
    int16_t start_x;
    int16_t end_x;
    uint16_t *row;
    int16_t draw_w;

    if ((framebuffer == RT_NULL) ||
        (width <= 0) ||
        (y < 0) ||
        (y >= (int16_t)CAMERA_APP_HEIGHT) ||
        (alpha == 0U))
    {
        return;
    }

    start_x = x;
    end_x = (int16_t)(x + width);

    if (start_x < 0)
    {
        start_x = 0;
    }
    if (end_x > (int16_t)CAMERA_APP_WIDTH)
    {
        end_x = (int16_t)CAMERA_APP_WIDTH;
    }
    if (end_x <= start_x)
    {
        return;
    }

    row = ((uint16_t *)framebuffer) + ((uint32_t)y * CAMERA_APP_WIDTH) + (uint32_t)start_x;
    draw_w = (int16_t)(end_x - start_x);

    if (alpha >= 255U)
    {
        for (int16_t i = 0; i < draw_w; i++)
        {
            row[i] = color;
        }
    }
    else
    {
        for (int16_t i = 0; i < draw_w; i++)
        {
            row[i] = camera_app_blend_rgb565(row[i], color, alpha);
        }
    }

    camera_app_dirty_area_add(dirty, start_x, y, draw_w, 1);
}

/* 半透明竖直线。 */
static void camera_app_draw_vline(uint8_t *framebuffer,
                                  int16_t x,
                                  int16_t y,
                                  int16_t height,
                                  uint16_t color,
                                  uint8_t alpha,
                                  camera_app_dirty_area_t *dirty)
{
    int16_t start_y;
    int16_t end_y;
    uint16_t *pixels;
    int16_t draw_h;

    if ((framebuffer == RT_NULL) ||
        (height <= 0) ||
        (x < 0) ||
        (x >= (int16_t)CAMERA_APP_WIDTH) ||
        (alpha == 0U))
    {
        return;
    }

    start_y = y;
    end_y = (int16_t)(y + height);

    if (start_y < 0)
    {
        start_y = 0;
    }
    if (end_y > (int16_t)CAMERA_APP_HEIGHT)
    {
        end_y = (int16_t)CAMERA_APP_HEIGHT;
    }
    if (end_y <= start_y)
    {
        return;
    }

    pixels = (uint16_t *)framebuffer;
    draw_h = (int16_t)(end_y - start_y);

    if (alpha >= 255U)
    {
        for (int16_t i = 0; i < draw_h; i++)
        {
            pixels[((uint32_t)(start_y + i) * CAMERA_APP_WIDTH) + (uint32_t)x] = color;
        }
    }
    else
    {
        for (int16_t i = 0; i < draw_h; i++)
        {
            uint32_t index = ((uint32_t)(start_y + i) * CAMERA_APP_WIDTH) + (uint32_t)x;
            pixels[index] = camera_app_blend_rgb565(pixels[index], color, alpha);
        }
    }

    camera_app_dirty_area_add(dirty, x, start_y, 1, draw_h);
}

/* 半透明矩形边框。 */
static void camera_app_draw_rect_border(uint8_t *framebuffer,
                                        int16_t x,
                                        int16_t y,
                                        int16_t width,
                                        int16_t height,
                                        uint8_t thickness,
                                        uint16_t color,
                                        uint8_t alpha,
                                        camera_app_dirty_area_t *dirty)
{
    uint8_t i;

    if ((width <= 0) || (height <= 0) || (thickness == 0U))
    {
        return;
    }

    for (i = 0; i < thickness; i++)
    {
        camera_app_draw_hline(framebuffer, x, (int16_t)(y + i), width, color, alpha, dirty);
        camera_app_draw_hline(framebuffer, x, (int16_t)(y + height - 1 - i), width, color, alpha, dirty);
        camera_app_draw_vline(framebuffer, (int16_t)(x + i), y, height, color, alpha, dirty);
        camera_app_draw_vline(framebuffer, (int16_t)(x + width - 1 - i), y, height, color, alpha, dirty);
    }
}

/* 矩形四角短线（取景框风格）。 */
static void camera_app_draw_corner_segments(uint8_t *framebuffer,
                                            int16_t x,
                                            int16_t y,
                                            int16_t width,
                                            int16_t height,
                                            int16_t len,
                                            uint8_t thickness,
                                            uint16_t color,
                                            uint8_t alpha,
                                            camera_app_dirty_area_t *dirty)
{
    uint8_t i;

    if ((width <= 0) || (height <= 0) || (len <= 0) || (thickness == 0U))
    {
        return;
    }

    for (i = 0; i < thickness; i++)
    {
        camera_app_draw_hline(framebuffer, x, (int16_t)(y + i), len, color, alpha, dirty);
        camera_app_draw_vline(framebuffer, (int16_t)(x + i), y, len, color, alpha, dirty);

        camera_app_draw_hline(framebuffer, (int16_t)(x + width - len), (int16_t)(y + i), len, color, alpha, dirty);
        camera_app_draw_vline(framebuffer, (int16_t)(x + width - 1 - i), y, len, color, alpha, dirty);

        camera_app_draw_hline(framebuffer, x, (int16_t)(y + height - 1 - i), len, color, alpha, dirty);
        camera_app_draw_vline(framebuffer, (int16_t)(x + i), (int16_t)(y + height - len), len, color, alpha, dirty);

        camera_app_draw_hline(framebuffer,
                              (int16_t)(x + width - len),
                              (int16_t)(y + height - 1 - i),
                              len,
                              color,
                              alpha,
                              dirty);
        camera_app_draw_vline(framebuffer,
                              (int16_t)(x + width - 1 - i),
                              (int16_t)(y + height - len),
                              len,
                              color,
                              alpha,
                              dirty);
    }
}

/* 半透明矩形填充。 */
static void camera_app_fill_rect(uint8_t *framebuffer,
                                 int16_t x,
                                 int16_t y,
                                 int16_t width,
                                 int16_t height,
                                 uint16_t color,
                                 uint8_t alpha,
                                 camera_app_dirty_area_t *dirty)
{
    int16_t start_x;
    int16_t start_y;
    int16_t end_x;
    int16_t end_y;
    int16_t draw_w;
    int16_t draw_h;

    if ((framebuffer == RT_NULL) || (width <= 0) || (height <= 0) || (alpha == 0U))
    {
        return;
    }

    start_x = x;
    start_y = y;
    end_x = (int16_t)(x + width);
    end_y = (int16_t)(y + height);

    if (start_x < 0)
    {
        start_x = 0;
    }
    if (start_y < 0)
    {
        start_y = 0;
    }
    if (end_x > (int16_t)CAMERA_APP_WIDTH)
    {
        end_x = (int16_t)CAMERA_APP_WIDTH;
    }
    if (end_y > (int16_t)CAMERA_APP_HEIGHT)
    {
        end_y = (int16_t)CAMERA_APP_HEIGHT;
    }
    if ((end_x <= start_x) || (end_y <= start_y))
    {
        return;
    }

    draw_w = (int16_t)(end_x - start_x);
    draw_h = (int16_t)(end_y - start_y);

    for (int16_t yy = 0; yy < draw_h; yy++)
    {
        uint16_t *row = ((uint16_t *)framebuffer) +
                        ((uint32_t)(start_y + yy) * CAMERA_APP_WIDTH) +
                        (uint32_t)start_x;

        if (alpha >= 255U)
        {
            for (int16_t xx = 0; xx < draw_w; xx++)
            {
                row[xx] = color;
            }
        }
        else
        {
            for (int16_t xx = 0; xx < draw_w; xx++)
            {
                row[xx] = camera_app_blend_rgb565(row[xx], color, alpha);
            }
        }
    }

    camera_app_dirty_area_add(dirty, start_x, start_y, draw_w, draw_h);
}

/* ROI x 限制在画面内。 */
static uint16_t camera_app_clamp_roi_x(uint16_t roi_x, uint16_t roi_size)
{
    uint16_t max_x = (roi_size >= CAMERA_APP_WIDTH) ? 0U : (uint16_t)(CAMERA_APP_WIDTH - roi_size);

    return (roi_x > max_x) ? max_x : roi_x;
}

/* ROI y 限制在画面内。 */
static uint16_t camera_app_clamp_roi_y(uint16_t roi_y, uint16_t roi_size)
{
    uint16_t max_y = (roi_size >= CAMERA_APP_HEIGHT) ? 0U : (uint16_t)(CAMERA_APP_HEIGHT - roi_size);

    return (roi_y > max_y) ? max_y : roi_y;
}

/* ROI 边长限幅，0 取默认值。 */
static uint16_t camera_app_clamp_roi_size(uint16_t roi_size)
{
    if (roi_size == 0U)
    {
        roi_size = CAMERA_APP_DEFAULT_ROI_SIZE;
    }
    else if (roi_size < 8U)
    {
        roi_size = 8U;
    }

    if (roi_size > CAMERA_APP_HEIGHT)
    {
        roi_size = CAMERA_APP_HEIGHT;
    }

    if (roi_size > CAMERA_APP_WIDTH)
    {
        roi_size = CAMERA_APP_WIDTH;
    }

    return roi_size;
}

/* 绘制 AI 异常热力图（超过保持时间自动清除）。 */
static void camera_app_draw_detect_heatmap(uint8_t *framebuffer, camera_app_dirty_area_t *dirty)
{
    camera_app_heatmap_t heatmap;
    uint8_t scores[CAMERA_APP_HEATMAP_MAX_CELLS];
    rt_base_t level;
    rt_tick_t expire_tick;
    uint16_t grid_w;
    uint16_t grid_h;
    uint32_t cell_count;
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;
    const uint16_t heat_color = camera_app_pack_rgb565(255U, 32U, 32U);

    level = rt_hw_interrupt_disable();
    expire_tick = s_detect_heatmap_expire_tick;
    if ((expire_tick != 0) && ((rt_tick_get() - expire_tick) < (RT_TICK_MAX / 2)))
    {
        s_detect_heatmap.grid_w = 0U;
        s_detect_heatmap.grid_h = 0U;
        s_detect_heatmap.score_count = 0U;
        s_detect_heatmap_expire_tick = 0;
        rt_hw_interrupt_enable(level);
        return;
    }
    heatmap = s_detect_heatmap;
    if (heatmap.score_count > CAMERA_APP_HEATMAP_MAX_CELLS)
    {
        rt_hw_interrupt_enable(level);
        return;
    }
    rt_memcpy(scores, s_detect_heatmap_scores, heatmap.score_count);
    rt_hw_interrupt_enable(level);

    grid_w = heatmap.grid_w;
    grid_h = heatmap.grid_h;
    if ((grid_w == 0U) ||
        (grid_h == 0U) ||
        (grid_w > CAMERA_APP_HEATMAP_MAX_GRID_W) ||
        (grid_h > CAMERA_APP_HEATMAP_MAX_GRID_H))
    {
        return;
    }

    cell_count = (uint32_t)grid_w * grid_h;
    if ((cell_count == 0U) ||
        (cell_count > CAMERA_APP_HEATMAP_MAX_CELLS) ||
        (heatmap.score_count < cell_count))
    {
        return;
    }

    roi_x = heatmap.roi_x;
    roi_y = heatmap.roi_y;
    roi_w = heatmap.roi_w;
    roi_h = heatmap.roi_h;

    if (roi_x >= CAMERA_APP_WIDTH)
    {
        roi_x = CAMERA_APP_WIDTH - 1U;
    }
    if (roi_y >= CAMERA_APP_HEIGHT)
    {
        roi_y = CAMERA_APP_HEIGHT - 1U;
    }
    if ((roi_w == 0U) || ((uint32_t)roi_x + roi_w > CAMERA_APP_WIDTH))
    {
        roi_w = (uint16_t)(CAMERA_APP_WIDTH - roi_x);
    }
    if ((roi_h == 0U) || ((uint32_t)roi_y + roi_h > CAMERA_APP_HEIGHT))
    {
        roi_h = (uint16_t)(CAMERA_APP_HEIGHT - roi_y);
    }

    for (uint16_t gy = 0; gy < grid_h; gy++)
    {
        for (uint16_t gx = 0; gx < grid_w; gx++)
        {
            uint32_t index = (uint32_t)gy * grid_w + gx;
            uint8_t intensity = scores[index];
            uint16_t cell_x0;
            uint16_t cell_y0;
            uint16_t cell_x1;
            uint16_t cell_y1;
            int16_t draw_x;
            int16_t draw_y;
            int16_t draw_w;
            int16_t draw_h;

            if (intensity < CAMERA_APP_HEATMAP_MIN_INTENSITY)
            {
                continue;
            }

            cell_x0 = (uint16_t)(roi_x + ((uint32_t)gx * roi_w) / grid_w);
            cell_y0 = (uint16_t)(roi_y + ((uint32_t)gy * roi_h) / grid_h);
            cell_x1 = (uint16_t)(roi_x + ((uint32_t)(gx + 1U) * roi_w) / grid_w);
            cell_y1 = (uint16_t)(roi_y + ((uint32_t)(gy + 1U) * roi_h) / grid_h);
            draw_x = (int16_t)cell_x0;
            draw_y = (int16_t)cell_y0;
            draw_w = (int16_t)(cell_x1 - cell_x0);
            draw_h = (int16_t)(cell_y1 - cell_y0);

            if ((draw_w > 0) && (draw_h > 0))
            {
                camera_app_fill_rect(framebuffer,
                                     draw_x,
                                     draw_y,
                                     draw_w,
                                     draw_h,
                                     heat_color,
                                     CAMERA_APP_HEATMAP_ALPHA,
                                     dirty);
            }
        }
    }
}

/* 检测页叠加：热力图 + ROI 框。 */
static void camera_app_draw_detect_overlay(uint8_t *framebuffer, camera_app_dirty_area_t *dirty)
{
    uint16_t roi_size;
    uint16_t roi_x;
    uint16_t roi_y;
    const uint16_t roi_color = camera_app_pack_rgb565(58U, 132U, 170U);
    const uint16_t corner_color = camera_app_pack_rgb565(73U, 183U, 255U);

    rt_base_t level = rt_hw_interrupt_disable();
    roi_size = s_detect_roi_size;
    roi_x = s_detect_roi_x;
    roi_y = s_detect_roi_y;
    rt_hw_interrupt_enable(level);

    roi_size = camera_app_clamp_roi_size(roi_size);
    roi_x = camera_app_clamp_roi_x(roi_x, roi_size);
    roi_y = camera_app_clamp_roi_y(roi_y, roi_size);

    camera_app_draw_detect_heatmap(framebuffer, dirty);
    camera_app_draw_rect_border(framebuffer,
                                (int16_t)(roi_x + 2U),
                                (int16_t)(roi_y + 2U),
                                (int16_t)(roi_size - 4U),
                                (int16_t)(roi_size - 4U),
                                1U,
                                roi_color,
                                110U,
                                dirty);
    camera_app_draw_corner_segments(framebuffer,
                                    (int16_t)(roi_x + 2U),
                                    (int16_t)(roi_y + 2U),
                                    (int16_t)(roi_size - 4U),
                                    (int16_t)(roi_size - 4U),
                                    28,
                                    2U,
                                    corner_color,
                                    150U,
                                    dirty);
}

/* 采集页叠加：居中引导框。 */
static void camera_app_draw_collect_overlay(uint8_t *framebuffer, camera_app_dirty_area_t *dirty)
{
    const uint16_t guide_color = camera_app_pack_rgb565(191U, 215U, 232U);
    const uint16_t corner_color = camera_app_pack_rgb565(73U, 183U, 255U);
    const int16_t x = (int16_t)CAMERA_APP_COLLECT_GUIDE_X;
    const int16_t y = (int16_t)CAMERA_APP_COLLECT_GUIDE_Y;
    const int16_t size = (int16_t)CAMERA_APP_DEFAULT_ROI_SIZE;

    camera_app_draw_rect_border(framebuffer, x, y, size, size, 1U, guide_color, 70U, dirty);
    camera_app_draw_corner_segments(framebuffer, x, y, size, size, 24, 2U, corner_color, 170U, dirty);
}

/* 软件门限包围框，颜色随门限状态变化。 */
static void camera_app_draw_gate_overlay(uint8_t *framebuffer, camera_app_dirty_area_t *dirty)
{
    software_gate_overlay_t overlay;
    uint16_t bbox_color = camera_app_pack_rgb565(244U, 180U, 0U);

    software_gate_app_get_overlay(&overlay);
    if (overlay.visible != RT_TRUE)
    {
        return;
    }

    switch (overlay.state)
    {
    case SOFTWARE_GATE_STATE_TRIGGERED:
        bbox_color = camera_app_pack_rgb565(224U, 70U, 70U);
        break;
    case SOFTWARE_GATE_STATE_COOLDOWN:
        bbox_color = camera_app_pack_rgb565(60U, 150U, 150U);
        break;
    case SOFTWARE_GATE_STATE_SEEN:
        bbox_color = camera_app_pack_rgb565(244U, 180U, 0U);
        break;
    default:
        break;
    }

    if (overlay.bbox_valid == RT_TRUE)
    {
        camera_app_draw_rect_border(framebuffer,
                                    (int16_t)overlay.bbox_x0,
                                    (int16_t)overlay.bbox_y0,
                                    (int16_t)(overlay.bbox_x1 - overlay.bbox_x0 + 1U),
                                    (int16_t)(overlay.bbox_y1 - overlay.bbox_y0 + 1U),
                                    2U,
                                    bbox_color,
                                    190U,
                                    dirty);
    }
}

/* 按当前叠加模式绘制。 */
void camera_app_draw_overlay(uint8_t *framebuffer, camera_app_dirty_area_t *dirty)
{
    camera_app_overlay_mode_t mode = s_camera_overlay_mode;

    switch (mode)
    {
    case CAMERA_APP_OVERLAY_DETECT:
        camera_app_draw_detect_overlay(framebuffer, dirty);
        break;
    case CAMERA_APP_OVERLAY_COLLECT:
        camera_app_draw_collect_overlay(framebuffer, dirty);
        break;
    default:
        break;
    }

    camera_app_draw_gate_overlay(framebuffer, dirty);
}

/* 设置叠加层模式。 */
void camera_app_set_lcd_preview_overlay_mode(camera_app_overlay_mode_t mode)
{
    if (mode > CAMERA_APP_OVERLAY_COLLECT)
    {
        mode = CAMERA_APP_OVERLAY_NONE;
    }

    s_camera_overlay_mode = mode;
}

/* 设置检测 ROI，并同步到软件门限。 */
void camera_app_set_detect_roi(uint16_t roi_x, uint16_t roi_y, uint16_t roi_size)
{
    rt_base_t level;

    roi_size = camera_app_clamp_roi_size(roi_size);
    roi_x = camera_app_clamp_roi_x(roi_x, roi_size);
    roi_y = camera_app_clamp_roi_y(roi_y, roi_size);

    level = rt_hw_interrupt_disable();
    s_detect_roi_x = roi_x;
    s_detect_roi_y = roi_y;
    s_detect_roi_size = roi_size;
    rt_hw_interrupt_enable(level);

    software_gate_app_set_roi(roi_x, roi_y, roi_size, roi_size);
}

/* 更新热力图数据，保持 CAMERA_APP_HEATMAP_HOLD_MS 后自动清除。 */
void camera_app_set_detect_heatmap(const camera_app_heatmap_t *heatmap)
{
    uint32_t cell_count;
    rt_base_t level;

    level = rt_hw_interrupt_disable();

    if ((heatmap == RT_NULL) ||
        (heatmap->score_map == RT_NULL) ||
        (heatmap->grid_w == 0U) ||
        (heatmap->grid_h == 0U) ||
        (heatmap->grid_w > CAMERA_APP_HEATMAP_MAX_GRID_W) ||
        (heatmap->grid_h > CAMERA_APP_HEATMAP_MAX_GRID_H))
    {
        s_detect_heatmap.grid_w = 0U;
        s_detect_heatmap.grid_h = 0U;
        s_detect_heatmap.score_count = 0U;
        s_detect_heatmap_expire_tick = 0;
        rt_hw_interrupt_enable(level);
        return;
    }

    cell_count = (uint32_t)heatmap->grid_w * heatmap->grid_h;
    if ((cell_count == 0U) ||
        (cell_count > CAMERA_APP_HEATMAP_MAX_CELLS) ||
        (heatmap->score_count < cell_count))
    {
        s_detect_heatmap.grid_w = 0U;
        s_detect_heatmap.grid_h = 0U;
        s_detect_heatmap.score_count = 0U;
        s_detect_heatmap_expire_tick = 0;
        rt_hw_interrupt_enable(level);
        return;
    }

    rt_memcpy(s_detect_heatmap_scores, heatmap->score_map, cell_count);
    s_detect_heatmap.grid_w = heatmap->grid_w;
    s_detect_heatmap.grid_h = heatmap->grid_h;
    s_detect_heatmap.roi_x = heatmap->roi_x;
    s_detect_heatmap.roi_y = heatmap->roi_y;
    s_detect_heatmap.roi_w = heatmap->roi_w;
    s_detect_heatmap.roi_h = heatmap->roi_h;
    s_detect_heatmap.score_map = s_detect_heatmap_scores;
    s_detect_heatmap.score_count = (uint16_t)cell_count;
    s_detect_heatmap_expire_tick = rt_tick_get() + rt_tick_from_millisecond(CAMERA_APP_HEATMAP_HOLD_MS);

    rt_hw_interrupt_enable(level);
}

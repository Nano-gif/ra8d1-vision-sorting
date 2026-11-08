/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-11-24     Rbb666       The first version
 */
#include <lvgl.h>
#include "ra8/lcd_config.h"
#include "hal_data.h"
#include <stdint.h>

static rt_sem_t _SemaphoreVsync = RT_NULL;
static uint8_t lvgl_init_flag = 0;

#define LVGL_FB_BYTES_PER_PIXEL    (DISPLAY_BITS_PER_PIXEL_INPUT0 / 8U)
#define LVGL_DCACHE_LINE_SIZE      (32U)

void DisplayVsyncCallback(display_callback_args_t *p_args)
{
    rt_interrupt_enter();
    if (DISPLAY_EVENT_LINE_DETECTION == p_args->event)
    {
        if (lvgl_init_flag != 0)
            rt_sem_release(_SemaphoreVsync);
    }
    rt_interrupt_leave();
}

static void vsync_wait_cb(lv_display_t *display)
{
    if (!lv_display_flush_is_last(display)) return;

    //
    // If Vsync semaphore has already been set, clear it then wait to avoid tearing
    //
    rt_sem_take(_SemaphoreVsync, RT_WAITING_FOREVER);
}

static void disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    if (!lv_display_flush_is_last(display)) return;

#if defined(RENESAS_CORTEX_M85)
#if (BSP_CFG_DCACHE_ENABLED)
    /*
     * LVGL writes only the dirty rectangle described by area.
     * The framebuffer is linear memory with a stride per line, so clean the
     * dirty bytes line by line instead of cleaning the whole 800x480 buffer.
     */
    const uintptr_t fb_base = (uintptr_t) px_map;
    const uint32_t stride_bytes = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0;
    const uint32_t x1_bytes = (uint32_t) area->x1 * LVGL_FB_BYTES_PER_PIXEL;
    const uint32_t width_bytes = (uint32_t) (area->x2 - area->x1 + 1) * LVGL_FB_BYTES_PER_PIXEL;

    for (int32_t y = area->y1; y <= area->y2; y++)
    {
        uintptr_t line_addr = fb_base + ((uint32_t) y * stride_bytes) + x1_bytes;
        uintptr_t clean_addr = line_addr & ~(uintptr_t)(LVGL_DCACHE_LINE_SIZE - 1U);
        uintptr_t clean_end = (line_addr + width_bytes + LVGL_DCACHE_LINE_SIZE - 1U) &
                              ~(uintptr_t)(LVGL_DCACHE_LINE_SIZE - 1U);

        SCB_CleanInvalidateDCache_by_Addr((uint32_t *) clean_addr, (int32_t) (clean_end - clean_addr));
    }
#endif
#endif

    R_GLCDC_BufferChange(&g_display0_ctrl,
                         (uint8_t *) px_map,
                         (display_frame_layer_t) 0);
}

void lv_port_disp_init(void)
{
    static rt_device_t device;
    /* LCD Device Init */
    device = rt_device_find("lcd");
    RT_ASSERT(device != RT_NULL);

    _SemaphoreVsync = rt_sem_create("lvgl_sem", 1, RT_IPC_FLAG_PRIO);

    if (RT_NULL == _SemaphoreVsync)
    {
        rt_kprintf("lvgl semaphore create failed\r\n");
        RT_ASSERT(0);
    }

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    lv_display_t *disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_flush_wait_cb(disp, vsync_wait_cb);
    lv_display_set_buffers(disp, &fb_background[0][0], &fb_background[1][0], sizeof(fb_background[0]), LV_DISPLAY_RENDER_MODE_DIRECT);

    lvgl_init_flag = 1;
}

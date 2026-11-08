#ifndef CAMERA_INTERNAL_H
#define CAMERA_INTERNAL_H

#include <rtthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "hal_data.h"
#include "../board/ports/camera/sensor.h"
#include "camera_app.h"
#include "ai_app.h"
#include "storage_app.h"
#include "w5500_eth_app.h"
#include "software_gate_app.h"

/*
 * 相机模块私有头：常量、内部类型、共享状态与跨文件内部函数声明。
 * 仅供 camera_app.c / camera_overlay.c / camera_request.c 使用，对外接口见 camera_app.h。
 */

#define CAMERA_APP_WIDTH         320U
#define CAMERA_APP_HEIGHT        240U
#define CAMERA_APP_BYTES_PER_PIXEL 2U
#define CAMERA_APP_FRAME_BYTES   (CAMERA_APP_WIDTH * CAMERA_APP_HEIGHT * CAMERA_APP_BYTES_PER_PIXEL)
#define CAMERA_APP_TCP_HEADER_BYTES 32U
#define CAMERA_APP_TCP_PAYLOAD_BYTES (CAMERA_APP_TCP_HEADER_BYTES + CAMERA_APP_FRAME_BYTES)
#define CAMERA_APP_TCP_DEFAULT_PORT 5000U
#define CAMERA_APP_TCP_DEFAULT_CHUNK 2048U
#define CAMERA_APP_TCP_DEFAULT_ACCEPT_MS 30000U
#define CAMERA_APP_TCP_PROTOCOL_VERSION 1U
#define CAMERA_APP_TCP_FORMAT_RGB565_LE 1U
#define CAMERA_APP_THREAD_STACK  4096U
#define CAMERA_APP_THREAD_PRIO   22U
#define CAMERA_APP_AI_PREPROCESS_PRIO 18U
#define CAMERA_APP_THREAD_TICK   10U
#define CAMERA_APP_FPS_PERIOD_MS 1000U
#define CAMERA_APP_LCD_PREVIEW_DEFAULT_FPS 15U
#define CAMERA_APP_LCD_PREVIEW_MIN_FPS 1U
#define CAMERA_APP_LCD_PREVIEW_MAX_FPS 30U
#define CAMERA_APP_HIDDEN_DELAY_MS 50U
#define CAMERA_APP_DEFAULT_PREVIEW_X 116
#define CAMERA_APP_DEFAULT_PREVIEW_Y 124
#define CAMERA_APP_DEFAULT_ROI_SIZE 240U
#define CAMERA_APP_HEATMAP_MIN_INTENSITY 64U
#define CAMERA_APP_HEATMAP_ALPHA 110U
#define CAMERA_APP_HEATMAP_HOLD_MS       2000U
#define CAMERA_APP_COLLECT_GUIDE_X ((CAMERA_APP_WIDTH - CAMERA_APP_DEFAULT_ROI_SIZE) / 2U)
#define CAMERA_APP_COLLECT_GUIDE_Y ((CAMERA_APP_HEIGHT - CAMERA_APP_DEFAULT_ROI_SIZE) / 2U)

typedef enum
{
    CAMERA_APP_CMD_NONE = 0,
    CAMERA_APP_CMD_TEST_ONCE,
    CAMERA_APP_CMD_PREVIEW,
    CAMERA_APP_CMD_LCD_PREVIEW,
    CAMERA_APP_CMD_SAVE_BMP_TEST,
    CAMERA_APP_CMD_TCP_FRAME_SERVER,
} camera_app_cmd_t;

typedef struct
{
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    rt_bool_t valid;
} camera_app_dirty_area_t;

/* 模块内共享状态（定义见各 .c） */
extern struct rt_semaphore g_camera_collect_save_sem;
extern struct rt_semaphore g_camera_ai_preprocess_sem;
extern struct rt_semaphore g_camera_tcp_service_sem;
extern rt_bool_t g_camera_thread_started;
extern volatile rt_bool_t g_camera_busy;
extern volatile rt_bool_t g_camera_lcd_preview_suspended;
extern volatile rt_bool_t g_camera_collect_save_requested;
extern volatile rt_bool_t g_camera_collect_save_inited;
extern volatile rt_bool_t g_camera_ai_preprocess_requested;
extern volatile rt_bool_t g_camera_ai_preprocess_inited;
extern volatile rt_bool_t g_camera_ai_preprocess_verbose;
extern volatile rt_bool_t g_camera_tcp_frame_requested;
extern volatile rt_bool_t g_camera_tcp_frame_in_progress;
extern volatile int g_camera_tcp_frame_result;
extern volatile uint16_t g_camera_tcp_server_port;
extern volatile uint16_t g_camera_tcp_server_chunk;
extern volatile uint32_t g_camera_tcp_accept_ms;
extern volatile uint32_t g_camera_tcp_frame_id;
extern uint8_t g_camera_ai_input[AI_APP_INPUT_BYTES];

/* 模块内共享函数 */
uint16_t camera_app_ticks_to_ms(rt_tick_t ticks);
void camera_app_dirty_area_reset(camera_app_dirty_area_t *dirty);
void camera_app_draw_overlay(uint8_t *framebuffer, camera_app_dirty_area_t *dirty);
void camera_app_clean_dcache(const void *addr, uint32_t size);
int camera_app_send_tcp_frame_payload(const uint8_t *framebuffer,
                                      uint16_t port,
                                      uint16_t chunk,
                                      uint32_t accept_ms,
                                      uint32_t frame_id);
void camera_app_handle_collect_save_request(const uint8_t *framebuffer);
void camera_app_handle_ai_preprocess_request(const uint8_t *framebuffer);
void camera_app_handle_tcp_frame_request(const uint8_t *framebuffer);
void camera_app_handle_tcp_service_request(const uint8_t *framebuffer);

#endif /* CAMERA_INTERNAL_H */

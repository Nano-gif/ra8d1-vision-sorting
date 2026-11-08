#ifndef CAMERA_APP_H
#define CAMERA_APP_H

#include <stdint.h>

#define CAMERA_APP_HEATMAP_MAX_GRID_W 16U
#define CAMERA_APP_HEATMAP_MAX_GRID_H 16U
#define CAMERA_APP_HEATMAP_MAX_CELLS  (CAMERA_APP_HEATMAP_MAX_GRID_W * CAMERA_APP_HEATMAP_MAX_GRID_H)

typedef enum
{
    CAMERA_APP_OVERLAY_NONE = 0,
    CAMERA_APP_OVERLAY_DETECT,
    CAMERA_APP_OVERLAY_COLLECT,
} camera_app_overlay_mode_t;

typedef struct
{
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;
    const uint8_t *score_map;
    uint16_t score_count;
} camera_app_heatmap_t;

typedef struct
{
    uint16_t wait_ms;
    uint16_t preprocess_ms;
    uint16_t copy_ms;
    uint16_t total_ms;
} camera_app_ai_capture_metrics_t;

typedef struct
{
    uint16_t port;
    uint16_t chunk;
    uint32_t accept_timeout_ms;
    uint32_t frame_id;
} camera_app_tcp_frame_request_t;

typedef struct
{
    const uint8_t *data;
    uint32_t len;
    uint32_t seq;
} camera_app_tcp_frame_payload_t;

void camera_app_start_test(void);
void camera_app_save_bmp_test(void);
void camera_app_start_preview(void);
void camera_app_start_lcd_preview(void);
void camera_app_stop_preview(void);
void camera_app_tcp_frame_server(uint16_t port, uint16_t chunk, uint32_t accept_timeout_ms);
int camera_app_request_tcp_service_frame(uint32_t frame_id, uint32_t timeout_ms);
int camera_app_get_tcp_service_payload(camera_app_tcp_frame_payload_t *payload);
void camera_app_finish_tcp_service_payload(void);
int camera_app_collect_save_current_frame(char *out_path, uint32_t out_path_size, uint32_t timeout_ms);
void camera_app_set_lcd_preview_visible(uint8_t visible);
void camera_app_set_lcd_preview_window(uint8_t visible, int16_t x, int16_t y);
void camera_app_set_lcd_preview_fps(uint8_t fps);
uint16_t camera_app_get_lcd_preview_fps_x10(void);
void camera_app_set_lcd_preview_suspended(uint8_t suspended);
void camera_app_set_lcd_preview_overlay_mode(camera_app_overlay_mode_t mode);
void camera_app_set_detect_roi(uint16_t roi_x, uint16_t roi_y, uint16_t roi_size);
void camera_app_set_detect_heatmap(const camera_app_heatmap_t *heatmap);
int camera_app_capture_ai_input(uint8_t *out_rgb888, uint32_t out_size, uint32_t timeout_ms);
int camera_app_capture_ai_input_ex(uint8_t *out_rgb888,
                                   uint32_t out_size,
                                   uint32_t timeout_ms,
                                   camera_app_ai_capture_metrics_t *metrics);
int camera_app_capture_ai_input_with_tcp_frame_ex(uint8_t *out_rgb888,
                                                  uint32_t out_size,
                                                  uint32_t timeout_ms,
                                                  const camera_app_tcp_frame_request_t *tcp_request,
                                                  camera_app_ai_capture_metrics_t *metrics);

#endif /* CAMERA_APP_H */

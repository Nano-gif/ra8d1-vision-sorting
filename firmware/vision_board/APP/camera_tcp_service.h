#ifndef CAMERA_TCP_SERVICE_H
#define CAMERA_TCP_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t enabled;
    uint8_t running;
    uint8_t connected;
    uint8_t socket_open;
    uint16_t port;
    uint16_t peer_port;
    uint8_t peer_ip[4];
    uint32_t listen_ok;
    uint32_t accepted;
    uint32_t ping_rx;
    uint32_t pong_tx;
    uint32_t frame_sent;
    uint32_t reconnects;
    uint32_t recv_errors;
    uint32_t send_errors;
    uint8_t service_ready;
    uint32_t last_active_age_ms;
    uint32_t last_ping_age_ms;
    uint32_t last_frame_age_ms;
} camera_tcp_service_snapshot_t;

int camera_tcp_service_start(void);
void camera_tcp_service_stop(void);
int camera_tcp_service_is_enabled(void);
int camera_tcp_service_get_snapshot(camera_tcp_service_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_TCP_SERVICE_H */

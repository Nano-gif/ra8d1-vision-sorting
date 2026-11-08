#ifndef PC_STATUS_APP_H
#define PC_STATUS_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_STATUS_DEFAULT_PORT        16000U
#define PC_STATUS_DEFAULT_PERIOD_MS   1000U

typedef struct
{
    uint8_t enabled;
    uint8_t dest_ip[4];
    uint16_t dest_port;
    uint32_t period_ms;
    uint32_t seq;
    uint32_t sent_packets;
    uint32_t send_errors;
    int last_error;
} pc_status_snapshot_t;

int pc_status_app_init(void);
int pc_status_app_start(void);
void pc_status_app_stop(void);
int pc_status_app_send_once(void);
int pc_status_app_set_target(const uint8_t ip[4], uint16_t port);
int pc_status_app_set_period(uint32_t period_ms);
int pc_status_app_get_snapshot(pc_status_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* PC_STATUS_APP_H */

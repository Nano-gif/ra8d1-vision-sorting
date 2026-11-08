#ifndef PC_CMD_APP_H
#define PC_CMD_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_CMD_DEFAULT_PORT 51010U

typedef struct
{
    uint8_t running;
    uint16_t listen_port;
    uint32_t received_packets;
    uint32_t ack_packets;
    uint32_t errors;
    uint32_t last_seq;
    uint32_t last_rx_age_ms;
    int last_result;
} pc_cmd_snapshot_t;

int pc_cmd_app_init(void);
int pc_cmd_app_start(void);
void pc_cmd_app_stop(void);
int pc_cmd_app_get_snapshot(pc_cmd_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* PC_CMD_APP_H */

#ifndef APP_W5500_ETH_APP_H_
#define APP_W5500_ETH_APP_H_

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define W5500_ETH_LINK_OFF 0U
#define W5500_ETH_LINK_ON  1U

typedef struct
{
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
    uint8_t dhcp;
} w5500_eth_net_config_t;

typedef struct
{
    uint32_t init_attempts;
    uint32_t init_ok;
    uint32_t init_fail;
    uint32_t link_checks;
    uint32_t link_changes;
    uint32_t link_errors;
    uint32_t tcp_sessions;
    uint32_t tcp_bytes;
    uint32_t tcp_errors;
    uint32_t udp_packets;
    uint32_t udp_bytes;
    uint32_t udp_errors;
    uint8_t ready;
    uint8_t service_running;
    uint8_t link_state;
    uint8_t phycfgr;
    int int_level;
} w5500_eth_stats_t;

int w5500_eth_init(void);
int w5500_eth_is_ready(void);
int w5500_eth_get_link(void);
int w5500_eth_service_start(void);
void w5500_eth_service_stop(void);
void w5500_eth_get_stats(w5500_eth_stats_t *stats);
int w5500_eth_get_net_config(w5500_eth_net_config_t *config);
int w5500_eth_set_net_config(const w5500_eth_net_config_t *config);
int w5500_eth_udp_send_to(const uint8_t dest_ip[4], uint16_t dest_port, const void *data, rt_size_t len);
int w5500_eth_udp_recv_cmd(uint16_t listen_port,
                           void *buffer,
                           rt_size_t buffer_size,
                           uint8_t source_ip[4],
                           uint16_t *source_port);
int w5500_eth_tcp_send_to(const uint8_t dest_ip[4], uint16_t dest_port, const void *data, rt_size_t len, uint16_t chunk);
int w5500_eth_tcp_server_send(uint16_t listen_port,
                              const void *data,
                              rt_size_t len,
                              uint16_t chunk,
                              uint32_t accept_timeout_ms);
int w5500_eth_tcp_service_lock(uint32_t timeout_ms);
void w5500_eth_tcp_service_unlock(void);
int w5500_eth_tcp_service_prepare_locked(void);
int w5500_eth_tcp_service_listen_locked(uint16_t listen_port);
uint8_t w5500_eth_tcp_service_status_locked(void);
int w5500_eth_tcp_service_recv_locked(void *buffer, rt_size_t buffer_size);
int w5500_eth_tcp_service_send_locked(const void *data, rt_size_t len, uint16_t chunk);
void w5500_eth_tcp_service_close_locked(void);
void w5500_eth_tcp_service_get_peer_locked(uint8_t ip[4], uint16_t *port);

#ifdef __cplusplus
}
#endif

#endif /* APP_W5500_ETH_APP_H_ */

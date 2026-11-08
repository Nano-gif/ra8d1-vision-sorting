/*
 * W5500 以太网模块内部头文件。
 *
 * 仅供 w5500_eth_app.c（芯片初始化/链路/后台服务）、w5500_eth_socket.c
 * （UDP/TCP 收发）和 w5500_eth_cmd.c（msh 调试命令）之间共享，
 * 其他模块请只包含 w5500_eth_app.h。
 */
#ifndef APP_W5500_ETH_INTERNAL_H_
#define APP_W5500_ETH_INTERNAL_H_

#include <rtthread.h>
#include <stdint.h>

#include "w5500_eth_app.h"

#define W5500_ETH_VERSION           0x04U
#define W5500_ETH_TCP_SOCKET        0U
#define W5500_ETH_TCP_LOCAL_PORT    50000U
#define W5500_ETH_TCP_PORT          5000U
#define W5500_ETH_TCP_BYTES         115200UL
#define W5500_ETH_TCP_CHUNK         2048U
#define W5500_ETH_TCP_TX_BUF        4096U
#define W5500_ETH_TCP_BUSY_MS       5000U
#define W5500_ETH_TCP_ACCEPT_MS     30000U
#define W5500_ETH_TCP_LOG_MIN_BYTES 1024UL
#define W5500_ETH_UDP_SOCKET        1U
#define W5500_ETH_UDP_LOCAL_PORT    51000U
#define W5500_ETH_UDP_TEST_PORT     6000U
#define W5500_ETH_CMD_SOCKET        2U
#define W5500_ETH_SERVICE_STACK     2048U
#define W5500_ETH_SERVICE_PRIO      24U
#define W5500_ETH_SERVICE_TICK      5U
#define W5500_ETH_SERVICE_MS        1000U

/* 模块共享状态，定义见 w5500_eth_app.c / w5500_eth_socket.c */
extern rt_bool_t g_w5500_eth_ready;
extern w5500_eth_stats_t g_w5500_eth_stats;
extern rt_bool_t g_w5500_eth_tcp_verbose_log;

/* w5500_eth_app.c：互斥锁、底层初始化与链路检查 */
uint32_t w5500_eth_ticks_to_ms(rt_tick_t ticks);
int w5500_eth_lock(void);
int w5500_eth_lock_timeout(rt_int32_t timeout_tick);
void w5500_eth_unlock(void);
int w5500_eth_port_init(void);
void w5500_eth_register_callbacks(void);
int w5500_eth_init_stack(void);
int w5500_eth_check_ready_link_locked(void);

/* w5500_eth_socket.c：TCP 测试图样发送（供 msh 命令使用） */
int w5500_eth_tcp_test_send_to(const uint8_t dest_ip[4],
                               uint16_t dest_port,
                               uint32_t total_bytes,
                               uint16_t chunk);
int w5500_eth_tcp_test_server_send(uint16_t listen_port,
                                   uint32_t total_bytes,
                                   uint16_t chunk);

#endif /* APP_W5500_ETH_INTERNAL_H_ */

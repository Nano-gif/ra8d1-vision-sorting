/*
 * W5500 以太网 UDP/TCP 收发：状态上报 UDP、命令接收 UDP、
 * 图像 TCP 服务端/客户端，以及 TCP 吞吐测试图样发送。
 * 所有 *_locked 接口要求调用方已持有模块互斥锁。
 */
#include <string.h>

#include <rtthread.h>

#include "w5500_eth_internal.h"
/* socket.h 内含若干未使用的 static 辅助函数（第三方头），仅在此处屏蔽该告警 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "Components/Ethernet/socket.h"
#pragma GCC diagnostic pop
#include "Components/Ethernet/W5500/w5500.h"

/* 发送中转缓冲区，位于片内 SRAM */
static uint8_t s_w5500_eth_tx_buf[W5500_ETH_TCP_TX_BUF];

rt_bool_t g_w5500_eth_tcp_verbose_log = RT_FALSE;

static int w5500_eth_send_payload(uint8_t socket_id, const uint8_t *data, uint32_t total_bytes, uint16_t chunk)
{
    uint32_t sent_total = 0;
    uint32_t elapsed_ms;
    uint32_t bytes_per_sec;
    rt_tick_t start_tick = rt_tick_get();
    rt_tick_t last_progress_tick = start_tick;

    while (sent_total < total_bytes)
    {
        uint32_t remain = total_bytes - sent_total;
        uint16_t send_len = (remain > (uint32_t)chunk) ? chunk : (uint16_t)remain;
        int32_t send_ret;

        /*
         * 负载可能位于 SDRAM（例如相机帧），先拷贝到片内 SRAM 中转缓冲区，
         * 使 W5500 SPI/DTC 通路始终从同一类内存取数，与 TCP 吞吐测试一致。
         */
        rt_memcpy(s_w5500_eth_tx_buf, &data[sent_total], send_len);
        send_ret = wiz_send(socket_id, s_w5500_eth_tx_buf, send_len);

        if (send_ret == SOCK_BUSY)
        {
            if (w5500_eth_ticks_to_ms(rt_tick_get() - last_progress_tick) > W5500_ETH_TCP_BUSY_MS)
            {
                rt_kprintf("[w5500_eth] send busy timeout at %lu/%lu\n",
                           (unsigned long)sent_total,
                           (unsigned long)total_bytes);
                g_w5500_eth_stats.tcp_errors++;
                return -1;
            }
            rt_thread_yield();
            continue;
        }

        if (send_ret < 0)
        {
            rt_kprintf("[w5500_eth] send failed: %d at %lu/%lu\n",
                   (int)send_ret,
                   (unsigned long)sent_total,
                   (unsigned long)total_bytes);
            g_w5500_eth_stats.tcp_errors++;
            return -1;
        }

        sent_total += (uint32_t)send_ret;
        last_progress_tick = rt_tick_get();
    }

    elapsed_ms = w5500_eth_ticks_to_ms(rt_tick_get() - start_tick);
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    bytes_per_sec = (uint32_t)(((uint64_t)sent_total * 1000ULL) / elapsed_ms);
    if ((g_w5500_eth_tcp_verbose_log == RT_TRUE) ||
        (sent_total >= W5500_ETH_TCP_LOG_MIN_BYTES))
    {
        rt_kprintf("[w5500_eth] tcp sent=%lu elapsed=%lu ms approx=%lu byte/s\n",
                   (unsigned long)sent_total,
                   (unsigned long)elapsed_ms,
                   (unsigned long)bytes_per_sec);
    }

    g_w5500_eth_stats.tcp_bytes += sent_total;
    return 0;
}

static int w5500_eth_send_test_pattern_locked(uint32_t total_bytes, uint16_t chunk)
{
    uint32_t sent_total = 0U;
    uint32_t pattern_base = 0U;
    uint32_t elapsed_ms;
    uint32_t bytes_per_sec;
    rt_tick_t start_tick = rt_tick_get();
    rt_tick_t last_progress_tick = start_tick;

    while (sent_total < total_bytes)
    {
        uint32_t i;
        uint32_t remain = total_bytes - sent_total;
        uint16_t fill_len = (remain > sizeof(s_w5500_eth_tx_buf)) ?
                            (uint16_t)sizeof(s_w5500_eth_tx_buf) :
                            (uint16_t)remain;
        uint16_t send_offset = 0U;

        for (i = 0U; i < fill_len; i++)
        {
            s_w5500_eth_tx_buf[i] = (uint8_t)((pattern_base + i) & 0xffU);
        }
        pattern_base += fill_len;

        while (send_offset < fill_len)
        {
            uint16_t send_len = (uint16_t)(fill_len - send_offset);
            int32_t send_ret;

            if (send_len > chunk)
            {
                send_len = chunk;
            }

            send_ret = wiz_send(W5500_ETH_TCP_SOCKET, &s_w5500_eth_tx_buf[send_offset], send_len);
            if (send_ret == SOCK_BUSY)
            {
                if (w5500_eth_ticks_to_ms(rt_tick_get() - last_progress_tick) > W5500_ETH_TCP_BUSY_MS)
                {
                    rt_kprintf("[w5500_eth] send busy timeout at %lu/%lu\n",
                               (unsigned long)sent_total,
                               (unsigned long)total_bytes);
                    g_w5500_eth_stats.tcp_errors++;
                    return -1;
                }
                rt_thread_yield();
                continue;
            }

            if (send_ret < 0)
            {
                rt_kprintf("[w5500_eth] send failed: %d at %lu/%lu\n",
                           (int)send_ret,
                           (unsigned long)sent_total,
                           (unsigned long)total_bytes);
                g_w5500_eth_stats.tcp_errors++;
                return -1;
            }

            send_offset = (uint16_t)(send_offset + (uint16_t)send_ret);
            sent_total += (uint32_t)send_ret;
            last_progress_tick = rt_tick_get();
        }
    }

    elapsed_ms = w5500_eth_ticks_to_ms(rt_tick_get() - start_tick);
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    bytes_per_sec = (uint32_t)(((uint64_t)sent_total * 1000ULL) / elapsed_ms);
    rt_kprintf("[w5500_eth] tcp sent=%lu elapsed=%lu ms approx=%lu byte/s\n",
               (unsigned long)sent_total,
               (unsigned long)elapsed_ms,
               (unsigned long)bytes_per_sec);

    g_w5500_eth_stats.tcp_bytes += sent_total;
    return 0;
}

static int w5500_eth_prepare_tcp_payload(const void *data, rt_size_t len, uint16_t *chunk)
{
    if ((data == RT_NULL) || (len == 0U) || (len > 0xFFFFFFFFU) || (chunk == RT_NULL))
    {
        return -1;
    }

    if ((*chunk == 0U) || (*chunk > W5500_ETH_TCP_TX_BUF))
    {
        *chunk = W5500_ETH_TCP_CHUNK;
    }

    return w5500_eth_check_ready_link_locked();
}

int w5500_eth_udp_send_to(const uint8_t dest_ip[4], uint16_t dest_port, const void *data, rt_size_t len)
{
    int8_t sock_ret;
    int32_t send_ret;
    int ret = -1;

    if ((dest_ip == RT_NULL) || (data == RT_NULL) || (len == 0U) || (len > 1472U) || (dest_port == 0U))
    {
        return -1;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_check_ready_link_locked() != 0)
    {
        g_w5500_eth_stats.udp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    if (getSn_SR(W5500_ETH_UDP_SOCKET) != SOCK_UDP)
    {
        wiz_close(W5500_ETH_UDP_SOCKET);
        sock_ret = wiz_socket(W5500_ETH_UDP_SOCKET, Sn_MR_UDP, W5500_ETH_UDP_LOCAL_PORT, 0);
        if (sock_ret < 0)
        {
            rt_kprintf("[w5500_eth] udp socket open failed: %d\n", (int)sock_ret);
            g_w5500_eth_stats.udp_errors++;
            w5500_eth_unlock();
            return -1;
        }
    }

    send_ret = wiz_sendto(W5500_ETH_UDP_SOCKET,
                          (uint8_t *)data,
                          (uint16_t)len,
                          (uint8_t *)dest_ip,
                          dest_port);
    if (send_ret == (int32_t)len)
    {
        g_w5500_eth_stats.udp_packets++;
        g_w5500_eth_stats.udp_bytes += (uint32_t)send_ret;
        ret = 0;
    }
    else
    {
        rt_kprintf("[w5500_eth] udp send failed: %d\n", (int)send_ret);
        g_w5500_eth_stats.udp_errors++;
    }

    w5500_eth_unlock();
    return ret;
}

int w5500_eth_udp_recv_cmd(uint16_t listen_port,
                           void *buffer,
                           rt_size_t buffer_size,
                           uint8_t source_ip[4],
                           uint16_t *source_port)
{
    int8_t sock_ret;
    int32_t recv_ret;
    uint16_t recv_size = 0U;
    uint16_t port = 0U;
    uint8_t ip[4] = {0U, 0U, 0U, 0U};

    if ((listen_port == 0U) || (buffer == RT_NULL) || (buffer_size == 0U) || (buffer_size > 1472U))
    {
        return -1;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_check_ready_link_locked() != 0)
    {
        g_w5500_eth_stats.udp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    if (getSn_SR(W5500_ETH_CMD_SOCKET) != SOCK_UDP)
    {
        wiz_close(W5500_ETH_CMD_SOCKET);
        sock_ret = wiz_socket(W5500_ETH_CMD_SOCKET, Sn_MR_UDP, listen_port, 0);
        if (sock_ret < 0)
        {
            rt_kprintf("[w5500_eth] cmd udp socket open failed: %d\n", (int)sock_ret);
            g_w5500_eth_stats.udp_errors++;
            w5500_eth_unlock();
            return -1;
        }
    }

    recv_size = getSn_RX_RSR(W5500_ETH_CMD_SOCKET);
    if (recv_size == 0U)
    {
        w5500_eth_unlock();
        return 0;
    }
    if (recv_size > buffer_size)
    {
        recv_size = (uint16_t)buffer_size;
    }

    recv_ret = wiz_recvfrom(W5500_ETH_CMD_SOCKET,
                            (uint8_t *)buffer,
                            recv_size,
                            ip,
                            &port);
    if (recv_ret > 0)
    {
        if (source_ip != RT_NULL)
        {
            memcpy(source_ip, ip, 4U);
        }
        if (source_port != RT_NULL)
        {
            *source_port = port;
        }
        g_w5500_eth_stats.udp_packets++;
        g_w5500_eth_stats.udp_bytes += (uint32_t)recv_ret;
        w5500_eth_unlock();
        return (int)recv_ret;
    }

    if (recv_ret < 0)
    {
        g_w5500_eth_stats.udp_errors++;
    }
    w5500_eth_unlock();
    return (int)recv_ret;
}

int w5500_eth_tcp_service_lock(uint32_t timeout_ms)
{
    rt_int32_t timeout_tick;

    timeout_tick = (timeout_ms == 0U) ? RT_WAITING_NO : (rt_int32_t)rt_tick_from_millisecond(timeout_ms);
    return w5500_eth_lock_timeout(timeout_tick);
}

void w5500_eth_tcp_service_unlock(void)
{
    w5500_eth_unlock();
}

int w5500_eth_tcp_service_prepare_locked(void)
{
    return w5500_eth_check_ready_link_locked();
}

int w5500_eth_tcp_service_listen_locked(uint16_t listen_port)
{
    int8_t sock_ret;

    if (listen_port == 0U)
    {
        listen_port = W5500_ETH_TCP_PORT;
    }

    if (w5500_eth_check_ready_link_locked() != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        return -1;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);
    sock_ret = wiz_socket(W5500_ETH_TCP_SOCKET, Sn_MR_TCP, listen_port, 0);
    if (sock_ret < 0)
    {
        rt_kprintf("[w5500_eth] service socket open failed: %d\n", (int)sock_ret);
        g_w5500_eth_stats.tcp_errors++;
        return -1;
    }

    sock_ret = wiz_listen(W5500_ETH_TCP_SOCKET);
    if (sock_ret != SOCK_OK)
    {
        rt_kprintf("[w5500_eth] service listen failed: %d\n", (int)sock_ret);
        wiz_close(W5500_ETH_TCP_SOCKET);
        g_w5500_eth_stats.tcp_errors++;
        return -1;
    }

    return 0;
}

uint8_t w5500_eth_tcp_service_status_locked(void)
{
    return getSn_SR(W5500_ETH_TCP_SOCKET);
}

int w5500_eth_tcp_service_recv_locked(void *buffer, rt_size_t buffer_size)
{
    uint16_t recv_size;
    int32_t recv_ret;

    if ((buffer == RT_NULL) || (buffer_size == 0U) || (buffer_size > 0xFFFFU))
    {
        return -1;
    }

    recv_size = getSn_RX_RSR(W5500_ETH_TCP_SOCKET);
    if (recv_size == 0U)
    {
        return 0;
    }
    if (recv_size > buffer_size)
    {
        recv_size = (uint16_t)buffer_size;
    }

    recv_ret = wiz_recv(W5500_ETH_TCP_SOCKET, (uint8_t *)buffer, recv_size);
    if (recv_ret > 0)
    {
        return (int)recv_ret;
    }

    if (recv_ret < 0)
    {
        g_w5500_eth_stats.tcp_errors++;
    }
    return (int)recv_ret;
}

int w5500_eth_tcp_service_send_locked(const void *data, rt_size_t len, uint16_t chunk)
{
    if (w5500_eth_prepare_tcp_payload(data, len, &chunk) != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        return -1;
    }

    if (w5500_eth_send_payload(W5500_ETH_TCP_SOCKET, (const uint8_t *)data, (uint32_t)len, chunk) == 0)
    {
        g_w5500_eth_stats.tcp_sessions++;
        return 0;
    }

    return -1;
}

void w5500_eth_tcp_service_close_locked(void)
{
    wiz_close(W5500_ETH_TCP_SOCKET);
}

void w5500_eth_tcp_service_get_peer_locked(uint8_t ip[4], uint16_t *port)
{
    if (ip != RT_NULL)
    {
        getSn_DIPR(W5500_ETH_TCP_SOCKET, ip);
    }
    if (port != RT_NULL)
    {
        *port = getSn_DPORTR(W5500_ETH_TCP_SOCKET);
    }
}
int w5500_eth_tcp_send_to(const uint8_t dest_ip[4], uint16_t dest_port, const void *data, rt_size_t len, uint16_t chunk)
{
    int8_t sock_ret;
    int ret = -1;

    if ((dest_ip == RT_NULL) || (dest_port == 0U))
    {
        return -1;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_prepare_tcp_payload(data, len, &chunk) != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);

    sock_ret = wiz_socket(W5500_ETH_TCP_SOCKET, Sn_MR_TCP, W5500_ETH_TCP_LOCAL_PORT, 0);
    if (sock_ret < 0)
    {
        rt_kprintf("[w5500_eth] socket open failed: %d\n", (int)sock_ret);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    sock_ret = wiz_connect(W5500_ETH_TCP_SOCKET, (uint8_t *)dest_ip, dest_port);
    if (sock_ret != SOCK_OK)
    {
        rt_kprintf("[w5500_eth] connect failed: %d\n", (int)sock_ret);
        wiz_close(W5500_ETH_TCP_SOCKET);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    if (w5500_eth_send_payload(W5500_ETH_TCP_SOCKET, (const uint8_t *)data, (uint32_t)len, chunk) == 0)
    {
        g_w5500_eth_stats.tcp_sessions++;
        ret = 0;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);
    w5500_eth_unlock();
    return ret;
}

int w5500_eth_tcp_server_send(uint16_t listen_port,
                              const void *data,
                              rt_size_t len,
                              uint16_t chunk,
                              uint32_t accept_timeout_ms)
{
    rt_tick_t wait_start;
    int8_t sock_ret;
    uint8_t status;
    int ret = -1;

    if (listen_port == 0U)
    {
        listen_port = W5500_ETH_TCP_PORT;
    }
    if (accept_timeout_ms == 0U)
    {
        accept_timeout_ms = W5500_ETH_TCP_ACCEPT_MS;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_prepare_tcp_payload(data, len, &chunk) != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);

    sock_ret = wiz_socket(W5500_ETH_TCP_SOCKET, Sn_MR_TCP, listen_port, 0);
    if (sock_ret < 0)
    {
        rt_kprintf("[w5500_eth] server socket open failed: %d\n", (int)sock_ret);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    sock_ret = wiz_listen(W5500_ETH_TCP_SOCKET);
    if (sock_ret != SOCK_OK)
    {
        rt_kprintf("[w5500_eth] listen failed: %d\n", (int)sock_ret);
        wiz_close(W5500_ETH_TCP_SOCKET);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wait_start = rt_tick_get();
    do
    {
        status = getSn_SR(W5500_ETH_TCP_SOCKET);
        if ((status == SOCK_ESTABLISHED) || (status == SOCK_CLOSE_WAIT))
        {
            if (w5500_eth_send_payload(W5500_ETH_TCP_SOCKET, (const uint8_t *)data, (uint32_t)len, chunk) == 0)
            {
                g_w5500_eth_stats.tcp_sessions++;
                ret = 0;
            }

            wiz_close(W5500_ETH_TCP_SOCKET);
            w5500_eth_unlock();
            return ret;
        }

        if (status == SOCK_CLOSED)
        {
            rt_kprintf("[w5500_eth] socket closed while waiting client\n");
            wiz_close(W5500_ETH_TCP_SOCKET);
            g_w5500_eth_stats.tcp_errors++;
            w5500_eth_unlock();
            return -1;
        }

        rt_thread_yield();
    } while (w5500_eth_ticks_to_ms(rt_tick_get() - wait_start) < accept_timeout_ms);

    rt_kprintf("[w5500_eth] accept timeout, no PC client connected\n");
    wiz_close(W5500_ETH_TCP_SOCKET);
    g_w5500_eth_stats.tcp_errors++;
    w5500_eth_unlock();
    return -1;
}

int w5500_eth_tcp_test_send_to(const uint8_t dest_ip[4],
                                      uint16_t dest_port,
                                      uint32_t total_bytes,
                                      uint16_t chunk)
{
    int8_t sock_ret;
    int ret = -1;

    if (total_bytes == 0U)
    {
        total_bytes = W5500_ETH_TCP_BYTES;
    }
    if ((chunk == 0U) || (chunk > W5500_ETH_TCP_TX_BUF))
    {
        chunk = W5500_ETH_TCP_CHUNK;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_check_ready_link_locked() != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);
    sock_ret = wiz_socket(W5500_ETH_TCP_SOCKET, Sn_MR_TCP, W5500_ETH_TCP_LOCAL_PORT, 0);
    if (sock_ret < 0)
    {
        rt_kprintf("[w5500_eth] socket open failed: %d\n", (int)sock_ret);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    sock_ret = wiz_connect(W5500_ETH_TCP_SOCKET, (uint8_t *)dest_ip, dest_port);
    if (sock_ret != SOCK_OK)
    {
        rt_kprintf("[w5500_eth] connect failed: %d\n", (int)sock_ret);
        wiz_close(W5500_ETH_TCP_SOCKET);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    if (w5500_eth_send_test_pattern_locked(total_bytes, chunk) == 0)
    {
        g_w5500_eth_stats.tcp_sessions++;
        ret = 0;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);
    w5500_eth_unlock();
    return ret;
}

int w5500_eth_tcp_test_server_send(uint16_t listen_port,
                                          uint32_t total_bytes,
                                          uint16_t chunk)
{
    rt_tick_t wait_start;
    int8_t sock_ret;
    uint8_t status;
    int ret = -1;

    if (total_bytes == 0U)
    {
        total_bytes = W5500_ETH_TCP_BYTES;
    }
    if ((chunk == 0U) || (chunk > W5500_ETH_TCP_TX_BUF))
    {
        chunk = W5500_ETH_TCP_CHUNK;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (w5500_eth_check_ready_link_locked() != 0)
    {
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wiz_close(W5500_ETH_TCP_SOCKET);
    sock_ret = wiz_socket(W5500_ETH_TCP_SOCKET, Sn_MR_TCP, listen_port, 0);
    if (sock_ret < 0)
    {
        rt_kprintf("[w5500_eth] server socket open failed: %d\n", (int)sock_ret);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    sock_ret = wiz_listen(W5500_ETH_TCP_SOCKET);
    if (sock_ret != SOCK_OK)
    {
        rt_kprintf("[w5500_eth] listen failed: %d\n", (int)sock_ret);
        wiz_close(W5500_ETH_TCP_SOCKET);
        g_w5500_eth_stats.tcp_errors++;
        w5500_eth_unlock();
        return -1;
    }

    wait_start = rt_tick_get();
    do
    {
        status = getSn_SR(W5500_ETH_TCP_SOCKET);
        if ((status == SOCK_ESTABLISHED) || (status == SOCK_CLOSE_WAIT))
        {
            rt_kprintf("[w5500_eth] tcp client connected, start send\n");
            if (w5500_eth_send_test_pattern_locked(total_bytes, chunk) == 0)
            {
                g_w5500_eth_stats.tcp_sessions++;
                ret = 0;
            }

            wiz_close(W5500_ETH_TCP_SOCKET);
            w5500_eth_unlock();
            return ret;
        }

        if (status == SOCK_CLOSED)
        {
            rt_kprintf("[w5500_eth] socket closed while waiting client\n");
            wiz_close(W5500_ETH_TCP_SOCKET);
            g_w5500_eth_stats.tcp_errors++;
            w5500_eth_unlock();
            return -1;
        }

        rt_thread_yield();
    } while (w5500_eth_ticks_to_ms(rt_tick_get() - wait_start) < W5500_ETH_TCP_ACCEPT_MS);

    rt_kprintf("[w5500_eth] accept timeout, no PC client connected\n");
    wiz_close(W5500_ETH_TCP_SOCKET);
    g_w5500_eth_stats.tcp_errors++;
    w5500_eth_unlock();
    return -1;
}

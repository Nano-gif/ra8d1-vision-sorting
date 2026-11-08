/*
 * W5500 以太网 msh 调试命令：初始化、版本/网络/链路查询、
 * TCP/UDP 发送测试、后台服务启停与统计。
 */
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "w5500_eth_internal.h"
#include "w5500_hwspi_app.h"
#include "Components/Ethernet/wizchip_conf.h"
#include "Components/Ethernet/W5500/w5500.h"

static uint32_t w5500_eth_parse_u32(const char *text, uint32_t default_value)
{
    char *end_ptr;
    unsigned long value;

    if (text == RT_NULL)
    {
        return default_value;
    }

    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return default_value;
    }

    return (uint32_t)value;
}

static int w5500_eth_parse_ipv4(const char *text, uint8_t ip[4])
{
    const char *cursor = text;
    char *end_ptr;
    unsigned long value;
    int i;

    if ((text == RT_NULL) || (ip == RT_NULL))
    {
        return -1;
    }

    for (i = 0; i < 4; i++)
    {
        value = strtoul(cursor, &end_ptr, 10);
        if ((end_ptr == cursor) || (value > 255UL))
        {
            return -1;
        }

        ip[i] = (uint8_t)value;

        if (i < 3)
        {
            if (*end_ptr != '.')
            {
                return -1;
            }
            cursor = end_ptr + 1;
        }
        else if (*end_ptr != '\0')
        {
            return -1;
        }
    }

    return 0;
}

static void w5500_eth_print_ip(const char *name, const uint8_t ip[4])
{
    rt_kprintf("%s=%u.%u.%u.%u",
               name,
               (unsigned int)ip[0],
               (unsigned int)ip[1],
               (unsigned int)ip[2],
               (unsigned int)ip[3]);
}

static void w5500_eth_print_netinfo(const wiz_NetInfo *netinfo)
{
    rt_kprintf("[w5500_eth] mac=%02x:%02x:%02x:%02x:%02x:%02x ",
               (unsigned int)netinfo->mac[0],
               (unsigned int)netinfo->mac[1],
               (unsigned int)netinfo->mac[2],
               (unsigned int)netinfo->mac[3],
               (unsigned int)netinfo->mac[4],
               (unsigned int)netinfo->mac[5]);
    w5500_eth_print_ip("ip", netinfo->ip);
    rt_kprintf(" ");
    w5500_eth_print_ip("sn", netinfo->sn);
    rt_kprintf(" ");
    w5500_eth_print_ip("gw", netinfo->gw);
    rt_kprintf(" ");
    w5500_eth_print_ip("dns", netinfo->dns);
    rt_kprintf(" dhcp=%s\n", (netinfo->dhcp == NETINFO_STATIC) ? "static" : "dhcp");
}

static int w5500_eth_init_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    return w5500_eth_init();
}

static int w5500_eth_ver_cmd(int argc, char **argv)
{
    uint8_t version;
    int ret = -1;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (g_w5500_eth_ready != RT_TRUE)
    {
        if (w5500_eth_port_init() != 0)
        {
            w5500_eth_unlock();
            return -1;
        }
        w5500_eth_register_callbacks();
    }

    version = getVERSIONR();
    rt_kprintf("[w5500_eth] VERSIONR=0x%02x %s\n",
               (unsigned int)version,
               (version == W5500_ETH_VERSION) ? "ok" : "unexpected");

    ret = (version == W5500_ETH_VERSION) ? 0 : -1;
    w5500_eth_unlock();
    return ret;
}

static int w5500_eth_net_cmd(int argc, char **argv)
{
    wiz_NetInfo current;
    int ret = 0;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (g_w5500_eth_ready != RT_TRUE)
    {
        rt_kprintf("[w5500_eth] not initialized, run w5500_eth_init first\n");
        ret = -1;
    }
    else
    {
        ctlnetwork(CN_GET_NETINFO, &current);
        w5500_eth_print_netinfo(&current);
    }

    w5500_eth_unlock();
    return ret;
}

static int w5500_eth_link_cmd(int argc, char **argv)
{
    uint8_t link = PHY_LINK_OFF;
    uint8_t phycfgr;
    int ret = -1;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (g_w5500_eth_ready != RT_TRUE)
    {
        rt_kprintf("[w5500_eth] not initialized, run w5500_eth_init first\n");
        w5500_eth_unlock();
        return -1;
    }

    ctlwizchip(CW_GET_PHYLINK, &link);
    phycfgr = getPHYCFGR();

    rt_kprintf("[w5500_eth] link=%s PHYCFGR=0x%02x INT=%d\n",
               (link == PHY_LINK_ON) ? "on" : "off",
               (unsigned int)phycfgr,
               w5500_hwspi_int_level());

    ret = (link == PHY_LINK_ON) ? 0 : -1;
    w5500_eth_unlock();
    return ret;
}

static int w5500_eth_tcp_send_cmd(int argc, char **argv)
{
    uint8_t peer_ip[4] = {192, 168, 1, 10};
    uint16_t peer_port = W5500_ETH_TCP_PORT;
    uint32_t total_bytes = W5500_ETH_TCP_BYTES;
    uint16_t chunk = W5500_ETH_TCP_CHUNK;

    if ((argc > 1) && (w5500_eth_parse_ipv4(argv[1], peer_ip) != 0))
    {
        rt_kprintf("[w5500_eth] bad ip: %s\n", argv[1]);
        rt_kprintf("[w5500_eth] usage: w5500_eth_tcp_send [ip] [port] [bytes] [chunk]\n");
        return -1;
    }

    if (argc > 2)
    {
        uint32_t port = w5500_eth_parse_u32(argv[2], W5500_ETH_TCP_PORT);
        if ((port == 0U) || (port > 65535U))
        {
            rt_kprintf("[w5500_eth] bad port: %s\n", argv[2]);
            return -1;
        }
        peer_port = (uint16_t)port;
    }

    if (argc > 3)
    {
        total_bytes = w5500_eth_parse_u32(argv[3], W5500_ETH_TCP_BYTES);
    }

    if (argc > 4)
    {
        chunk = (uint16_t)w5500_eth_parse_u32(argv[4], W5500_ETH_TCP_CHUNK);
    }

    rt_kprintf("[w5500_eth] tcp connect %u.%u.%u.%u:%u bytes=%lu chunk=%u\n",
               (unsigned int)peer_ip[0],
               (unsigned int)peer_ip[1],
               (unsigned int)peer_ip[2],
               (unsigned int)peer_ip[3],
               (unsigned int)peer_port,
               (unsigned long)total_bytes,
               (unsigned int)chunk);

    return w5500_eth_tcp_test_send_to(peer_ip, peer_port, total_bytes, chunk);
}

static int w5500_eth_tcp_server_send_cmd(int argc, char **argv)
{
    uint16_t listen_port = W5500_ETH_TCP_PORT;
    uint32_t total_bytes = W5500_ETH_TCP_BYTES;
    uint16_t chunk = W5500_ETH_TCP_CHUNK;

    if (argc > 1)
    {
        uint32_t port = w5500_eth_parse_u32(argv[1], W5500_ETH_TCP_PORT);
        if ((port == 0U) || (port > 65535U))
        {
            rt_kprintf("[w5500_eth] bad port: %s\n", argv[1]);
            return -1;
        }
        listen_port = (uint16_t)port;
    }

    if (argc > 2)
    {
        total_bytes = w5500_eth_parse_u32(argv[2], W5500_ETH_TCP_BYTES);
    }

    if (argc > 3)
    {
        chunk = (uint16_t)w5500_eth_parse_u32(argv[3], W5500_ETH_TCP_CHUNK);
    }

    rt_kprintf("[w5500_eth] tcp server listen 192.168.1.88:%u bytes=%lu chunk=%u\n",
               (unsigned int)listen_port,
               (unsigned long)total_bytes,
               (unsigned int)chunk);

    return w5500_eth_tcp_test_server_send(listen_port, total_bytes, chunk);
}

static int w5500_eth_udp_send_cmd(int argc, char **argv)
{
    static uint32_t seq = 0U;
    uint8_t peer_ip[4] = {192, 168, 1, 10};
    uint16_t peer_port = W5500_ETH_UDP_TEST_PORT;
    char payload[96];
    int len;

    if ((argc > 1) && (w5500_eth_parse_ipv4(argv[1], peer_ip) != 0))
    {
        rt_kprintf("[w5500_eth] bad ip: %s\n", argv[1]);
        rt_kprintf("[w5500_eth] usage: w5500_eth_udp_send [ip] [port] [text]\n");
        return -1;
    }

    if (argc > 2)
    {
        uint32_t port = w5500_eth_parse_u32(argv[2], W5500_ETH_UDP_TEST_PORT);
        if ((port == 0U) || (port > 65535U))
        {
            rt_kprintf("[w5500_eth] bad port: %s\n", argv[2]);
            return -1;
        }
        peer_port = (uint16_t)port;
    }

    if (argc > 3)
    {
        len = rt_snprintf(payload, sizeof(payload), "%s", argv[3]);
    }
    else
    {
        len = rt_snprintf(payload, sizeof(payload), "vision_status seq=%lu", (unsigned long)seq++);
    }

    if ((len <= 0) || (len >= (int)sizeof(payload)))
    {
        return -1;
    }

    rt_kprintf("[w5500_eth] udp send %u.%u.%u.%u:%u len=%d\n",
               (unsigned int)peer_ip[0],
               (unsigned int)peer_ip[1],
               (unsigned int)peer_ip[2],
               (unsigned int)peer_ip[3],
               (unsigned int)peer_port,
               len);

    return w5500_eth_udp_send_to(peer_ip, peer_port, payload, (rt_size_t)len);
}

static int w5500_eth_service_start_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (w5500_eth_service_start() != 0)
    {
        return -1;
    }

    rt_kprintf("[w5500_eth] service running interval=%u ms priority=%u\n",
               (unsigned int)W5500_ETH_SERVICE_MS,
               (unsigned int)W5500_ETH_SERVICE_PRIO);
    return 0;
}

static int w5500_eth_service_stop_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    w5500_eth_service_stop();
    rt_kprintf("[w5500_eth] service stopping\n");
    return 0;
}

static int w5500_eth_stat_cmd(int argc, char **argv)
{
    w5500_eth_stats_t stats;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    w5500_eth_get_stats(&stats);

    rt_kprintf("[w5500_eth] service=%s ready=%s link=%s PHYCFGR=0x%02x INT=%d\n",
               (stats.service_running != 0U) ? "on" : "off",
               (stats.ready != 0U) ? "yes" : "no",
               (stats.link_state == PHY_LINK_ON) ? "on" : "off",
               (unsigned int)stats.phycfgr,
               stats.int_level);
    rt_kprintf("[w5500_eth] init attempts=%lu ok=%lu fail=%lu\n",
               (unsigned long)stats.init_attempts,
               (unsigned long)stats.init_ok,
               (unsigned long)stats.init_fail);
    rt_kprintf("[w5500_eth] link checks=%lu changes=%lu errors=%lu\n",
               (unsigned long)stats.link_checks,
               (unsigned long)stats.link_changes,
               (unsigned long)stats.link_errors);
    rt_kprintf("[w5500_eth] tcp sessions=%lu bytes=%lu errors=%lu\n",
               (unsigned long)stats.tcp_sessions,
               (unsigned long)stats.tcp_bytes,
               (unsigned long)stats.tcp_errors);
    rt_kprintf("[w5500_eth] udp packets=%lu bytes=%lu errors=%lu\n",
               (unsigned long)stats.udp_packets,
               (unsigned long)stats.udp_bytes,
               (unsigned long)stats.udp_errors);
    return 0;
}

static int w5500_eth_tcp_log_cmd(int argc, char **argv)
{
    if (argc > 1)
    {
        if (rt_strcmp(argv[1], "on") == 0)
        {
            g_w5500_eth_tcp_verbose_log = RT_TRUE;
        }
        else if (rt_strcmp(argv[1], "off") == 0)
        {
            g_w5500_eth_tcp_verbose_log = RT_FALSE;
        }
        else if (rt_strcmp(argv[1], "stat") != 0)
        {
            rt_kprintf("[w5500_eth] usage: w5500_eth_tcp_log [on|off|stat]\n");
            return -1;
        }
    }

    rt_kprintf("[w5500_eth] tcp log verbose=%u min_bytes=%lu\n",
               (unsigned int)g_w5500_eth_tcp_verbose_log,
               (unsigned long)W5500_ETH_TCP_LOG_MIN_BYTES);
    return 0;
}

MSH_CMD_EXPORT_ALIAS(w5500_eth_init_cmd, w5500_eth_init, init W5500 ioLibrary driver);
MSH_CMD_EXPORT_ALIAS(w5500_eth_ver_cmd, w5500_eth_ver, read W5500 VERSIONR through ioLibrary);
MSH_CMD_EXPORT_ALIAS(w5500_eth_net_cmd, w5500_eth_net, print W5500 network info);
MSH_CMD_EXPORT_ALIAS(w5500_eth_link_cmd, w5500_eth_link, print W5500 PHY link status);
MSH_CMD_EXPORT_ALIAS(w5500_eth_tcp_send_cmd, w5500_eth_tcp_send, send TCP payload to PC server);
MSH_CMD_EXPORT_ALIAS(w5500_eth_tcp_server_send_cmd, w5500_eth_tcp_server_send, listen TCP client and send payload);
MSH_CMD_EXPORT_ALIAS(w5500_eth_udp_send_cmd, w5500_eth_udp_send, send UDP realtime status packet);
MSH_CMD_EXPORT_ALIAS(w5500_eth_service_start_cmd, w5500_eth_service_start, start W5500 background service);
MSH_CMD_EXPORT_ALIAS(w5500_eth_service_stop_cmd, w5500_eth_service_stop, stop W5500 background service);
MSH_CMD_EXPORT_ALIAS(w5500_eth_stat_cmd, w5500_eth_stat, show W5500 service statistics);
MSH_CMD_EXPORT_ALIAS(w5500_eth_tcp_log_cmd, w5500_eth_tcp_log, configure TCP send log output);

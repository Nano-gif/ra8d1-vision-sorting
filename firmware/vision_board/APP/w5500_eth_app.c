/*
 * W5500 以太网核心：芯片初始化、网络参数、PHY 链路轮询与后台服务线程。
 * UDP/TCP 收发见 w5500_eth_socket.c，msh 调试命令见 w5500_eth_cmd.c。
 */
#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>

#include "w5500_eth_internal.h"
#include "w5500_hwspi_app.h"
#include "Components/Ethernet/wizchip_conf.h"
#include "Components/Ethernet/W5500/w5500.h"

static rt_bool_t s_w5500_eth_port_ready = RT_FALSE;
static volatile rt_bool_t s_w5500_eth_service_running = RT_FALSE;
static rt_thread_t s_w5500_eth_service_thread = RT_NULL;
static rt_mutex_t s_w5500_eth_mutex = RT_NULL;

rt_bool_t g_w5500_eth_ready = RT_FALSE;
w5500_eth_stats_t g_w5500_eth_stats;

static wiz_NetInfo s_w5500_eth_netinfo = {
    .mac = {0x02, 0x08, 0xDC, 0x11, 0x22, 0x33},
    .ip = {192, 168, 1, 88},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 1, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = NETINFO_STATIC
};

uint32_t w5500_eth_ticks_to_ms(rt_tick_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) / RT_TICK_PER_SECOND);
}

/* 获取模块互斥锁（首次调用时创建），timeout_tick 语义同 rt_mutex_take */
int w5500_eth_lock_timeout(rt_int32_t timeout_tick)
{
    if (s_w5500_eth_mutex == RT_NULL)
    {
        s_w5500_eth_mutex = rt_mutex_create("w5500eth", RT_IPC_FLAG_PRIO);
        if (s_w5500_eth_mutex == RT_NULL)
        {
            rt_kprintf("[w5500_eth] mutex create failed\n");
            return -1;
        }
    }

    return (rt_mutex_take(s_w5500_eth_mutex, timeout_tick) == RT_EOK) ? 0 : -1;
}

int w5500_eth_lock(void)
{
    return w5500_eth_lock_timeout(RT_WAITING_FOREVER);
}

void w5500_eth_unlock(void)
{
    if (s_w5500_eth_mutex != RT_NULL)
    {
        rt_mutex_release(s_w5500_eth_mutex);
    }
}

int w5500_eth_port_init(void)
{
    if (s_w5500_eth_port_ready == RT_TRUE)
    {
        return 0;
    }

    if (w5500_hwspi_open() != 0)
    {
        return -1;
    }

    s_w5500_eth_port_ready = RT_TRUE;
    return 0;
}

static void w5500_eth_hw_reset(void)
{
    w5500_hwspi_reset();
}

static void w5500_eth_cs_select(void)
{
    w5500_hwspi_cs_select();
}

static void w5500_eth_cs_deselect(void)
{
    w5500_hwspi_cs_deselect();
}

static uint8_t w5500_eth_spi_read_byte(void)
{
    return w5500_hwspi_transfer_byte(0x00U);
}

static void w5500_eth_spi_write_byte(uint8_t value)
{
    (void)w5500_hwspi_transfer_byte(value);
}

static void w5500_eth_spi_read_burst(uint8_t *buf, uint16_t len)
{
    w5500_hwspi_read_burst(buf, len);
}

static void w5500_eth_spi_write_burst(uint8_t *buf, uint16_t len)
{
    w5500_hwspi_write_burst(buf, len);
}

void w5500_eth_register_callbacks(void)
{
    reg_wizchip_cs_cbfunc(w5500_eth_cs_select, w5500_eth_cs_deselect);
    reg_wizchip_spi_cbfunc(w5500_eth_spi_read_byte, w5500_eth_spi_write_byte);
    reg_wizchip_spiburst_cbfunc(w5500_eth_spi_read_burst, w5500_eth_spi_write_burst);
}

static void w5500_eth_stats_snapshot(w5500_eth_stats_t *stats)
{
    rt_base_t level;

    if (stats == RT_NULL)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    *stats = g_w5500_eth_stats;
    rt_hw_interrupt_enable(level);

    stats->ready = (g_w5500_eth_ready == RT_TRUE) ? 1U : 0U;
    stats->service_running = (s_w5500_eth_service_running == RT_TRUE) ? 1U : 0U;
}

int w5500_eth_init_stack(void)
{
    uint8_t tx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    wiz_NetTimeout timeout = {
        .retry_cnt = 3,
        .time_100us = 2000
    };
    uint8_t version;
    int8_t result;

    g_w5500_eth_stats.init_attempts++;

    if (w5500_eth_port_init() != 0)
    {
        g_w5500_eth_ready = RT_FALSE;
        g_w5500_eth_stats.init_fail++;
        return -1;
    }

    w5500_eth_hw_reset();
    w5500_eth_register_callbacks();

    version = getVERSIONR();
    if (version != W5500_ETH_VERSION)
    {
        rt_kprintf("[w5500_eth] VERSIONR=0x%02x unexpected, expected 0x%02x\n",
                   (unsigned int)version,
                   (unsigned int)W5500_ETH_VERSION);
        g_w5500_eth_ready = RT_FALSE;
        g_w5500_eth_stats.init_fail++;
        return -1;
    }

    result = wizchip_init(tx_size, rx_size);
    if (result != 0)
    {
        rt_kprintf("[w5500_eth] wizchip_init failed: %d\n", (int)result);
        g_w5500_eth_ready = RT_FALSE;
        g_w5500_eth_stats.init_fail++;
        return -1;
    }

    ctlnetwork(CN_SET_NETINFO, &s_w5500_eth_netinfo);
    wizchip_settimeout(&timeout);

    g_w5500_eth_ready = RT_TRUE;
    g_w5500_eth_stats.init_ok++;

    rt_kprintf("[w5500_eth] init ok, VERSIONR=0x%02x\n", (unsigned int)version);
    return 0;
}

static void w5500_eth_poll_link(void)
{
    uint8_t link = PHY_LINK_OFF;
    uint8_t phycfgr = 0U;

    if (g_w5500_eth_ready != RT_TRUE)
    {
        return;
    }

    if (ctlwizchip(CW_GET_PHYLINK, &link) != 0)
    {
        g_w5500_eth_stats.link_errors++;
        return;
    }

    phycfgr = getPHYCFGR();
    g_w5500_eth_stats.link_checks++;
    g_w5500_eth_stats.phycfgr = phycfgr;
    g_w5500_eth_stats.int_level = w5500_hwspi_int_level();

    if (g_w5500_eth_stats.link_state != link)
    {
        g_w5500_eth_stats.link_state = link;
        g_w5500_eth_stats.link_changes++;
        rt_kprintf("[w5500_eth] link changed: %s PHYCFGR=0x%02x\n",
                   (link == PHY_LINK_ON) ? "on" : "off",
                   (unsigned int)phycfgr);
    }
}

static void w5500_eth_service_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (s_w5500_eth_service_running == RT_TRUE)
    {
        if (w5500_eth_lock() == 0)
        {
            if (g_w5500_eth_ready == RT_TRUE)
            {
                w5500_eth_poll_link();
            }
            else
            {
                (void)w5500_eth_init_stack();
                w5500_eth_poll_link();
            }
            w5500_eth_unlock();
        }

        rt_thread_mdelay(W5500_ETH_SERVICE_MS);
    }

    s_w5500_eth_service_thread = RT_NULL;
}

int w5500_eth_init(void)
{
    int ret;

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    ret = w5500_eth_init_stack();
    w5500_eth_unlock();
    return ret;
}

int w5500_eth_is_ready(void)
{
    return (g_w5500_eth_ready == RT_TRUE) ? 1 : 0;
}

int w5500_eth_get_link(void)
{
    uint8_t link = PHY_LINK_OFF;
    int ret = -1;

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (g_w5500_eth_ready == RT_TRUE)
    {
        if (ctlwizchip(CW_GET_PHYLINK, &link) == 0)
        {
            g_w5500_eth_stats.link_state = link;
            g_w5500_eth_stats.phycfgr = getPHYCFGR();
            g_w5500_eth_stats.int_level = w5500_hwspi_int_level();
            ret = (link == PHY_LINK_ON) ? W5500_ETH_LINK_ON : W5500_ETH_LINK_OFF;
        }
        else
        {
            g_w5500_eth_stats.link_errors++;
        }
    }

    w5500_eth_unlock();
    return ret;
}

void w5500_eth_get_stats(w5500_eth_stats_t *stats)
{
    w5500_eth_stats_snapshot(stats);
}

int w5500_eth_get_net_config(w5500_eth_net_config_t *config)
{
    wiz_NetInfo current;

    if (config == RT_NULL)
    {
        return -1;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    if (g_w5500_eth_ready != RT_TRUE)
    {
        w5500_eth_unlock();
        return -1;
    }

    ctlnetwork(CN_GET_NETINFO, &current);
    memcpy(config->mac, current.mac, sizeof(config->mac));
    memcpy(config->ip, current.ip, sizeof(config->ip));
    memcpy(config->sn, current.sn, sizeof(config->sn));
    memcpy(config->gw, current.gw, sizeof(config->gw));
    memcpy(config->dns, current.dns, sizeof(config->dns));
    config->dhcp = current.dhcp;

    w5500_eth_unlock();
    return 0;
}

int w5500_eth_set_net_config(const w5500_eth_net_config_t *config)
{
    if (config == RT_NULL)
    {
        return -1;
    }

    if (w5500_eth_lock() != 0)
    {
        return -1;
    }

    memcpy(s_w5500_eth_netinfo.mac, config->mac, sizeof(s_w5500_eth_netinfo.mac));
    memcpy(s_w5500_eth_netinfo.ip, config->ip, sizeof(s_w5500_eth_netinfo.ip));
    memcpy(s_w5500_eth_netinfo.sn, config->sn, sizeof(s_w5500_eth_netinfo.sn));
    memcpy(s_w5500_eth_netinfo.gw, config->gw, sizeof(s_w5500_eth_netinfo.gw));
    memcpy(s_w5500_eth_netinfo.dns, config->dns, sizeof(s_w5500_eth_netinfo.dns));
    s_w5500_eth_netinfo.dhcp = config->dhcp;

    if (g_w5500_eth_ready == RT_TRUE)
    {
        ctlnetwork(CN_SET_NETINFO, &s_w5500_eth_netinfo);
    }

    w5500_eth_unlock();
    return 0;
}

int w5500_eth_service_start(void)
{
    rt_thread_t thread;

    if (s_w5500_eth_service_running == RT_TRUE)
    {
        return 0;
    }

    /* 刚请求停止的旧线程尚未退出时直接让它继续运行，避免同时存在两个服务线程 */
    if (s_w5500_eth_service_thread != RT_NULL)
    {
        s_w5500_eth_service_running = RT_TRUE;
        return 0;
    }

    s_w5500_eth_service_running = RT_TRUE;
    thread = rt_thread_create("w5500eth",
                              w5500_eth_service_entry,
                              RT_NULL,
                              W5500_ETH_SERVICE_STACK,
                              W5500_ETH_SERVICE_PRIO,
                              W5500_ETH_SERVICE_TICK);
    if (thread == RT_NULL)
    {
        s_w5500_eth_service_running = RT_FALSE;
        rt_kprintf("[w5500_eth] service thread create failed\n");
        return -1;
    }

    s_w5500_eth_service_thread = thread;
    rt_thread_startup(thread);
    return 0;
}

void w5500_eth_service_stop(void)
{
    s_w5500_eth_service_running = RT_FALSE;
}

static int w5500_eth_auto_start(void)
{
    if (w5500_eth_service_start() != 0)
    {
        return -1;
    }

    rt_kprintf("[w5500_eth] service auto started\n");
    return 0;
}
INIT_APP_EXPORT(w5500_eth_auto_start);

int w5500_eth_check_ready_link_locked(void)
{
    uint8_t link = PHY_LINK_OFF;

    if (g_w5500_eth_ready != RT_TRUE)
    {
        if (w5500_eth_init_stack() != 0)
        {
            return -1;
        }
    }

    if (ctlwizchip(CW_GET_PHYLINK, &link) != 0)
    {
        g_w5500_eth_stats.link_errors++;
        return -1;
    }

    g_w5500_eth_stats.link_state = link;
    g_w5500_eth_stats.phycfgr = getPHYCFGR();
    g_w5500_eth_stats.int_level = w5500_hwspi_int_level();
    if (link != PHY_LINK_ON)
    {
        rt_kprintf("[w5500_eth] link is off, check cable/switch first\n");
        return -1;
    }

    return 0;
}

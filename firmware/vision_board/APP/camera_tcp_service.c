#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "camera_tcp_service.h"
#include "camera_app.h"
#include "w5500_eth_app.h"
#include "Components/Ethernet/W5500/w5500.h"

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define CAMERA_TCP_SERVICE_PORT       5000U
#define CAMERA_TCP_SERVICE_STACK      2048U
#define CAMERA_TCP_SERVICE_PRIO       27U
#define CAMERA_TCP_SERVICE_TICK       10U
#define CAMERA_TCP_SERVICE_LOCK_MS    50U
#define CAMERA_TCP_SERVICE_IDLE_MS    50U
#define CAMERA_TCP_SERVICE_RETRY_MS   500U
#define CAMERA_TCP_SERVICE_RX_BYTES   96U
#define CAMERA_TCP_SERVICE_PONG       "PONG\n"
#define CAMERA_TCP_SERVICE_FRAME_TIMEOUT_MS  3000U
#define CAMERA_TCP_SERVICE_FRAME_CHUNK      2048U

typedef enum
{
    CAMERA_TCP_SERVICE_STATE_STOPPED = 0,
    CAMERA_TCP_SERVICE_STATE_WAIT_NET,
    CAMERA_TCP_SERVICE_STATE_LISTEN,
    CAMERA_TCP_SERVICE_STATE_CONNECTED,
    CAMERA_TCP_SERVICE_STATE_ERROR,
} camera_tcp_service_state_t;

typedef struct
{
    uint8_t enabled;
    uint8_t running;
    uint8_t socket_open;
    camera_tcp_service_state_t state;
    uint16_t port;
    uint32_t listen_ok;
    uint32_t accepted;
    uint32_t ping_rx;
    uint32_t pong_tx;
    uint32_t frame_sent;
    uint32_t recv_errors;
    uint32_t send_errors;
    uint32_t reconnects;
    uint8_t peer_ip[4];
    uint16_t peer_port;
    rt_tick_t last_active_tick;
    rt_tick_t last_ping_tick;
    rt_tick_t last_frame_tick;
} camera_tcp_service_ctx_t;

static camera_tcp_service_ctx_t s_camera_tcp_service = {
    .enabled = 1U,
    .running = 0U,
    .socket_open = 0U,
    .state = CAMERA_TCP_SERVICE_STATE_STOPPED,
    .port = CAMERA_TCP_SERVICE_PORT,
};

static rt_thread_t s_camera_tcp_service_thread = RT_NULL;

static void camera_tcp_service_mark_active(void)
{
    s_camera_tcp_service.last_active_tick = rt_tick_get();
}

static void camera_tcp_service_mark_ping_ok(void)
{
    rt_tick_t now = rt_tick_get();

    s_camera_tcp_service.last_active_tick = now;
    s_camera_tcp_service.last_ping_tick = now;
}

static void camera_tcp_service_mark_frame_ok(void)
{
    rt_tick_t now = rt_tick_get();

    s_camera_tcp_service.last_active_tick = now;
    s_camera_tcp_service.last_frame_tick = now;
}

static uint32_t camera_tcp_service_active_age_ms(rt_tick_t last_tick)
{
    if (last_tick == 0)
    {
        return UINT32_MAX;
    }

    return (uint32_t)(((uint64_t)(rt_tick_t)(rt_tick_get() - last_tick) * 1000ULL) / RT_TICK_PER_SECOND);
}

static const char *camera_tcp_service_state_name(camera_tcp_service_state_t state)
{
    switch (state)
    {
    case CAMERA_TCP_SERVICE_STATE_STOPPED:
        return "STOPPED";
    case CAMERA_TCP_SERVICE_STATE_WAIT_NET:
        return "WAIT_NET";
    case CAMERA_TCP_SERVICE_STATE_LISTEN:
        return "LISTEN";
    case CAMERA_TCP_SERVICE_STATE_CONNECTED:
        return "CONNECTED";
    case CAMERA_TCP_SERVICE_STATE_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

static void camera_tcp_service_set_state(camera_tcp_service_state_t state)
{
    s_camera_tcp_service.state = state;
}

static void camera_tcp_service_close_socket(void)
{
    if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) != 0)
    {
        return;
    }

    w5500_eth_tcp_service_close_locked();
    s_camera_tcp_service.socket_open = 0U;
    w5500_eth_tcp_service_unlock();
}

static void camera_tcp_service_close_session_locked(void)
{
    w5500_eth_tcp_service_close_locked();
    s_camera_tcp_service.socket_open = 0U;
    camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);
}

static int camera_tcp_service_open_listen(void)
{
    int ret;

    if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) != 0)
    {
        return -RT_EBUSY;
    }

    ret = w5500_eth_tcp_service_listen_locked(s_camera_tcp_service.port);
    if (ret == 0)
    {
        s_camera_tcp_service.socket_open = 1U;
        s_camera_tcp_service.listen_ok++;
        camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_LISTEN);
    }
    else
    {
        s_camera_tcp_service.socket_open = 0U;
        camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);
    }

    w5500_eth_tcp_service_unlock();
    return ret;
}

static int camera_tcp_service_poll_listen(void)
{
    uint8_t status;
    int ret = 0;

    if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) != 0)
    {
        return -RT_EBUSY;
    }

    status = w5500_eth_tcp_service_status_locked();
    if (status == SOCK_ESTABLISHED)
    {
        w5500_eth_tcp_service_get_peer_locked(s_camera_tcp_service.peer_ip,
                                              &s_camera_tcp_service.peer_port);
        s_camera_tcp_service.accepted++;
        camera_tcp_service_mark_active();
        camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_CONNECTED);
    }
    else if ((status == SOCK_CLOSED) || (status == SOCK_CLOSE_WAIT))
    {
        camera_tcp_service_close_session_locked();
        s_camera_tcp_service.reconnects++;
        ret = -RT_ERROR;
    }

    w5500_eth_tcp_service_unlock();
    return ret;
}

static void camera_tcp_service_handle_ping(void)
{
    /* PONG 回复很短且不阻塞，可在持锁状态下发送 */
    if (w5500_eth_tcp_service_send_locked(CAMERA_TCP_SERVICE_PONG,
                                          sizeof(CAMERA_TCP_SERVICE_PONG) - 1U,
                                          64U) == 0)
    {
        s_camera_tcp_service.ping_rx++;
        s_camera_tcp_service.pong_tx++;
        camera_tcp_service_mark_ping_ok();
        camera_tcp_service_close_session_locked();
    }
    else
    {
        s_camera_tcp_service.send_errors++;
        camera_tcp_service_close_session_locked();
    }
}

/*
 * 处理 GET_FRAME 请求。
 *
 * 调用时必须未持有 w5500_eth_tcp_service_lock：
 * camera_app_request_tcp_service_frame() 会阻塞在信号量上（最长约 3 秒）
 * 等待相机线程采集并打包，而相机线程在其他路径上也需要同一把 W5500 锁，
 * 持锁等待会造成死锁。
 *
 * 流程：
 * 1. 向相机线程请求一帧（阻塞等待信号量）；
 * 2. 取得已打包的 VIMG 负载指针；
 * 3. 重新获取 W5500 锁，在已建立的连接上发送负载；
 * 4. 释放锁并结束本次负载状态。
 */
static void camera_tcp_service_handle_get_frame(void)
{
    camera_app_tcp_frame_payload_t payload;
    int req_ret;

    camera_tcp_service_mark_active();
    req_ret = camera_app_request_tcp_service_frame(0U, CAMERA_TCP_SERVICE_FRAME_TIMEOUT_MS);
    if (req_ret != 0)
    {
        s_camera_tcp_service.send_errors++;
        if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) == 0)
        {
            camera_tcp_service_close_session_locked();
            w5500_eth_tcp_service_unlock();
        }
        return;
    }

    if (camera_app_get_tcp_service_payload(&payload) != 0)
    {
        camera_app_finish_tcp_service_payload();
        s_camera_tcp_service.send_errors++;
        if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) == 0)
        {
            camera_tcp_service_close_session_locked();
            w5500_eth_tcp_service_unlock();
        }
        return;
    }

    if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) != 0)
    {
        camera_app_finish_tcp_service_payload();
        s_camera_tcp_service.send_errors++;
        return;
    }

    if (w5500_eth_tcp_service_send_locked(payload.data, payload.len,
                                          CAMERA_TCP_SERVICE_FRAME_CHUNK) == 0)
    {
        s_camera_tcp_service.frame_sent++;
        camera_tcp_service_mark_frame_ok();
        camera_tcp_service_close_session_locked();
    }
    else
    {
        s_camera_tcp_service.send_errors++;
        camera_tcp_service_close_session_locked();
    }

    w5500_eth_tcp_service_unlock();
    camera_app_finish_tcp_service_payload();
}

/*
 * 分发一条收到的命令行。
 *
 * 调用方（camera_tcp_service_poll_connected）必须先释放 W5500 锁，
 * 因为 GET_FRAME 会阻塞等待相机信号量；PING 则短暂重新加锁后直接回复 PONG。
 */
static void camera_tcp_service_handle_rx(const char *rx_buf, int rx_len)
{
    if ((rx_buf == RT_NULL) || (rx_len <= 0))
    {
        return;
    }

    if ((strstr(rx_buf, "PING") != RT_NULL) || (strstr(rx_buf, "ping") != RT_NULL))
    {
        if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) == 0)
        {
            camera_tcp_service_handle_ping();
            w5500_eth_tcp_service_unlock();
        }
        else
        {
            s_camera_tcp_service.send_errors++;
        }
    }
    else if ((strstr(rx_buf, "GET_FRAME") != RT_NULL) ||
             (strstr(rx_buf, "get_frame") != RT_NULL))
    {
        camera_tcp_service_handle_get_frame();
    }
}

static int camera_tcp_service_poll_connected(void)
{
    uint8_t status;
    char rx_buf[CAMERA_TCP_SERVICE_RX_BYTES];
    int recv_len;
    int ret = 0;

    if (w5500_eth_tcp_service_lock(CAMERA_TCP_SERVICE_LOCK_MS) != 0)
    {
        return -RT_EBUSY;
    }

    status = w5500_eth_tcp_service_status_locked();
    if (status == SOCK_ESTABLISHED)
    {
        recv_len = w5500_eth_tcp_service_recv_locked(rx_buf, sizeof(rx_buf) - 1U);
        if (recv_len > 0)
        {
            rx_buf[recv_len] = '\0';
            camera_tcp_service_mark_active();
            /* 分发命令前先释放锁：GET_FRAME 会阻塞等待相机信号量，不能持有 W5500 锁 */
            w5500_eth_tcp_service_unlock();
            camera_tcp_service_handle_rx(rx_buf, recv_len);
            return ret;
        }
        else if (recv_len < 0)
        {
            s_camera_tcp_service.recv_errors++;
            ret = -RT_ERROR;
        }
    }
    else if ((status == SOCK_CLOSE_WAIT) || (status == SOCK_CLOSED))
    {
        camera_tcp_service_close_session_locked();
        s_camera_tcp_service.reconnects++;
        ret = -RT_ERROR;
    }
    else
    {
        camera_tcp_service_close_session_locked();
        s_camera_tcp_service.reconnects++;
        ret = -RT_ERROR;
    }

    w5500_eth_tcp_service_unlock();
    return ret;
}

static void camera_tcp_service_entry(void *parameter)
{
    RT_UNUSED(parameter);

    s_camera_tcp_service.running = 1U;
    camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);

    while (s_camera_tcp_service.enabled != 0U)
    {
        switch (s_camera_tcp_service.state)
        {
        case CAMERA_TCP_SERVICE_STATE_WAIT_NET:
        case CAMERA_TCP_SERVICE_STATE_ERROR:
            if (camera_tcp_service_open_listen() != 0)
            {
                rt_thread_mdelay(CAMERA_TCP_SERVICE_RETRY_MS);
            }
            break;

        case CAMERA_TCP_SERVICE_STATE_LISTEN:
            if (s_camera_tcp_service.socket_open == 0U)
            {
                camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);
            }
            else if (camera_tcp_service_poll_listen() != 0)
            {
                camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);
                rt_thread_mdelay(CAMERA_TCP_SERVICE_RETRY_MS);
            }
            break;

        case CAMERA_TCP_SERVICE_STATE_CONNECTED:
            (void)camera_tcp_service_poll_connected();
            break;

        case CAMERA_TCP_SERVICE_STATE_STOPPED:
        default:
            camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_WAIT_NET);
            break;
        }

        rt_thread_mdelay(CAMERA_TCP_SERVICE_IDLE_MS);
    }

    camera_tcp_service_close_socket();
    camera_tcp_service_set_state(CAMERA_TCP_SERVICE_STATE_STOPPED);
    s_camera_tcp_service.running = 0U;
    s_camera_tcp_service_thread = RT_NULL;
}

int camera_tcp_service_start(void)
{
    rt_thread_t thread;

    s_camera_tcp_service.enabled = 1U;
    if ((s_camera_tcp_service_thread != RT_NULL) || (s_camera_tcp_service.running != 0U))
    {
        return 0;
    }

    thread = rt_thread_create("camtcpsvc",
                              camera_tcp_service_entry,
                              RT_NULL,
                              CAMERA_TCP_SERVICE_STACK,
                              CAMERA_TCP_SERVICE_PRIO,
                              CAMERA_TCP_SERVICE_TICK);
    if (thread == RT_NULL)
    {
        rt_kprintf("[camera_tcp] service thread create failed\n");
        return -RT_ERROR;
    }

    s_camera_tcp_service_thread = thread;
    rt_thread_startup(thread);
    return 0;
}

void camera_tcp_service_stop(void)
{
    s_camera_tcp_service.enabled = 0U;
}

int camera_tcp_service_is_enabled(void)
{
    return (s_camera_tcp_service.enabled != 0U) ? 1 : 0;
}

int camera_tcp_service_get_snapshot(camera_tcp_service_snapshot_t *out_snapshot)
{
    camera_tcp_service_snapshot_t snapshot;
    rt_tick_t last_active_tick;
    rt_tick_t last_ping_tick;
    rt_tick_t last_frame_tick;
    rt_base_t level;

    if (out_snapshot == RT_NULL)
    {
        return -RT_ERROR;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    level = rt_hw_interrupt_disable();
    snapshot.enabled = s_camera_tcp_service.enabled;
    snapshot.running = s_camera_tcp_service.running;
    snapshot.connected = (s_camera_tcp_service.state == CAMERA_TCP_SERVICE_STATE_CONNECTED) ? 1U : 0U;
    snapshot.socket_open = s_camera_tcp_service.socket_open;
    snapshot.port = s_camera_tcp_service.port;
    snapshot.peer_port = s_camera_tcp_service.peer_port;
    snapshot.peer_ip[0] = s_camera_tcp_service.peer_ip[0];
    snapshot.peer_ip[1] = s_camera_tcp_service.peer_ip[1];
    snapshot.peer_ip[2] = s_camera_tcp_service.peer_ip[2];
    snapshot.peer_ip[3] = s_camera_tcp_service.peer_ip[3];
    snapshot.listen_ok = s_camera_tcp_service.listen_ok;
    snapshot.accepted = s_camera_tcp_service.accepted;
    snapshot.ping_rx = s_camera_tcp_service.ping_rx;
    snapshot.pong_tx = s_camera_tcp_service.pong_tx;
    snapshot.frame_sent = s_camera_tcp_service.frame_sent;
    snapshot.reconnects = s_camera_tcp_service.reconnects;
    snapshot.recv_errors = s_camera_tcp_service.recv_errors;
    snapshot.send_errors = s_camera_tcp_service.send_errors;
    snapshot.service_ready =
        ((s_camera_tcp_service.last_ping_tick != 0) ||
         (s_camera_tcp_service.last_frame_tick != 0)) ? 1U : 0U;
    last_active_tick = s_camera_tcp_service.last_active_tick;
    last_ping_tick = s_camera_tcp_service.last_ping_tick;
    last_frame_tick = s_camera_tcp_service.last_frame_tick;
    rt_hw_interrupt_enable(level);

    snapshot.last_active_age_ms = camera_tcp_service_active_age_ms(last_active_tick);
    snapshot.last_ping_age_ms = camera_tcp_service_active_age_ms(last_ping_tick);
    snapshot.last_frame_age_ms = camera_tcp_service_active_age_ms(last_frame_tick);
    *out_snapshot = snapshot;
    return RT_EOK;
}

static int camera_tcp_service_auto_start(void)
{
    if (camera_tcp_service_start() != 0)
    {
        return -1;
    }

    rt_kprintf("[camera_tcp] service auto started port=%u\n",
               (unsigned int)s_camera_tcp_service.port);
    return 0;
}
INIT_APP_EXPORT(camera_tcp_service_auto_start);

static uint32_t camera_tcp_service_parse_u32(const char *text, uint32_t default_value)
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

static int camera_tcp_service_cmd(int argc, char **argv)
{
    if (argc > 1)
    {
        if (rt_strcmp(argv[1], "on") == 0)
        {
            if (argc > 2)
            {
                uint32_t port = camera_tcp_service_parse_u32(argv[2], CAMERA_TCP_SERVICE_PORT);
                if ((port == 0U) || (port > 65535U))
                {
                    rt_kprintf("[camera_tcp] bad port: %s\n", argv[2]);
                    return -1;
                }
                s_camera_tcp_service.port = (uint16_t)port;
            }
            return camera_tcp_service_start();
        }
        if (rt_strcmp(argv[1], "off") == 0)
        {
            camera_tcp_service_stop();
            rt_kprintf("[camera_tcp] service stopping\n");
            return 0;
        }
        if (rt_strcmp(argv[1], "stat") == 0)
        {
            /* 继续执行下方的状态打印 */
        }
        else
        {
            rt_kprintf("[camera_tcp] usage: camera_tcp_service [on [port]|off|stat]\n");
            return -1;
        }
    }

    rt_kprintf("[camera_tcp] enabled=%u running=%u state=%s port=%u socket=%u peer=%u.%u.%u.%u:%u\n",
               (unsigned int)s_camera_tcp_service.enabled,
               (unsigned int)s_camera_tcp_service.running,
               camera_tcp_service_state_name(s_camera_tcp_service.state),
               (unsigned int)s_camera_tcp_service.port,
               (unsigned int)s_camera_tcp_service.socket_open,
               (unsigned int)s_camera_tcp_service.peer_ip[0],
               (unsigned int)s_camera_tcp_service.peer_ip[1],
               (unsigned int)s_camera_tcp_service.peer_ip[2],
               (unsigned int)s_camera_tcp_service.peer_ip[3],
               (unsigned int)s_camera_tcp_service.peer_port);
    rt_kprintf("[camera_tcp] listen_ok=%lu accepted=%lu ping=%lu pong=%lu frame=%lu reconnect=%lu recv_err=%lu send_err=%lu age=%lu ping_age=%lu frame_age=%lu\n",
               (unsigned long)s_camera_tcp_service.listen_ok,
               (unsigned long)s_camera_tcp_service.accepted,
               (unsigned long)s_camera_tcp_service.ping_rx,
               (unsigned long)s_camera_tcp_service.pong_tx,
               (unsigned long)s_camera_tcp_service.frame_sent,
               (unsigned long)s_camera_tcp_service.reconnects,
               (unsigned long)s_camera_tcp_service.recv_errors,
               (unsigned long)s_camera_tcp_service.send_errors,
               (unsigned long)camera_tcp_service_active_age_ms(s_camera_tcp_service.last_active_tick),
               (unsigned long)camera_tcp_service_active_age_ms(s_camera_tcp_service.last_ping_tick),
               (unsigned long)camera_tcp_service_active_age_ms(s_camera_tcp_service.last_frame_tick));
    return 0;
}

#ifdef RT_USING_FINSH
MSH_CMD_EXPORT_ALIAS(camera_tcp_service_cmd, camera_tcp_service, persistent camera TCP service);
#endif

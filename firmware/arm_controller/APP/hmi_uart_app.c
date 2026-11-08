#include "hmi_uart_app.h"
#include "mydefine.h"

#define HMI_UART_RX_BUFFER_SIZE       (128U)
#define HMI_UART_FRAME_BUFFER_SIZE    (128U)
#define HMI_UART_TX_BUFFER_SIZE       (128U)
#define HMI_UART_TX_TIMEOUT_MS        (100U)
#define HMI_UART_SENDME_DELAY_MS      (500U)
#define HMI_UART_FRAME_HEADER         (0xA5U)
#define HMI_UART_CMD_SERVO_US         (0x01U)
#define HMI_UART_CMD_HOME             (0x02U)
#define HMI_UART_CMD_SAVE_HOME        (0x03U)
#define HMI_UART_CMD_JOINT_QUERY      (0x04U)
#define HMI_UART_CMD_ACTION_ADD       (0x10U)
#define HMI_UART_CMD_ACTION_DELETE    (0x11U)
#define HMI_UART_CMD_ACTION_CLEAR     (0x12U)
#define HMI_UART_CMD_ACTION_PLAY      (0x13U)
#define HMI_UART_CMD_ACTION_STOP      (0x14U)
#define HMI_UART_CMD_ACTION_QUERY     (0x15U)
#define HMI_UART_CMD_MANUAL_JOG       (0x30U)
#define HMI_UART_CMD_MANUAL_ENTER     (0x31U)
#define HMI_UART_CMD_MANUAL_LEAVE     (0x32U)
#define HMI_UART_CMD_MANUAL_QUERY     (0x33U)
#define HMI_UART_CMD_MANUAL_COORD     (0x34U)
#define HMI_UART_CMD_HOME_QUERY       (0x40U)
#define HMI_UART_VISION_ONLINE_TIMEOUT_MS (1000U)
#define HMI_UART_SERVO_FRAME_LEN      (8U)
#define HMI_UART_HOME_FRAME_LEN       (5U)
#define HMI_UART_SAVE_HOME_FRAME_LEN  (5U)
#define HMI_UART_JOINT_QUERY_LEN      (5U)
#define HMI_UART_ACTION_ADD_FRAME_LEN (7U)
#define HMI_UART_ACTION_CMD_FRAME_LEN (5U)
#define HMI_UART_MANUAL_JOG_FRAME_LEN (11U)
#define HMI_UART_MANUAL_PAGE_FRAME_LEN (5U)
#define HMI_UART_MANUAL_COORD_FRAME_LEN (6U)
#define HMI_UART_HOME_QUERY_FRAME_LEN (5U)

static struct rt_ringbuffer g_hmi_uart_rx_rb;
static uint8_t g_hmi_uart_rx_pool[HMI_UART_RX_BUFFER_SIZE];
static volatile bool g_hmi_uart_tx_done = false;
static volatile uint32_t g_hmi_uart_rx_overflows = 0;
static volatile uint32_t g_hmi_uart_rx_errors = 0;
static uint8_t g_hmi_uart_frame_buffer[HMI_UART_FRAME_BUFFER_SIZE];
static uint16_t g_hmi_uart_frame_len = 0;
static bool g_hmi_uart_frame_overflow = false;
static uint32_t g_hmi_uart_init_tick = 0;
static bool g_hmi_uart_sendme_sent = false;
static bool g_hmi_uart_ready = false;
static bool g_hmi_uart_manual_status_valid = false;
static bool g_hmi_uart_vision_activity_seen = false;
static uint32_t g_hmi_uart_vision_last_activity_tick = 0;
static arm_manual_info_t g_hmi_uart_last_manual_info;

static const uint8_t g_hmi_text_online_gb2312[]     = {0xD4U, 0xDAU, 0xCFU, 0xDFU};
static const uint8_t g_hmi_text_offline_gb2312[]    = {0xC0U, 0xEBU, 0xCFU, 0xDFU};
static const uint8_t g_hmi_text_busy_gb2312[]       = {0xC3U, 0xA6U, 0xC2U, 0xB5U};
static const uint8_t g_hmi_text_idle_gb2312[]       = {0xBFU, 0xD5U, 0xCFU, 0xD0U};
static const uint8_t g_hmi_text_manual_gb2312[]     = {0xCAU, 0xD6U, 0xB6U, 0xAFU};
static const uint8_t g_hmi_text_action_gb2312[]     = {0xB6U, 0xAFU, 0xD7U, 0xF7U};
static const uint8_t g_hmi_text_homed_gb2312[]      = {0xD2U, 0xD1U, 0xB8U, 0xB4U, 0xCEU, 0xBBU};
static const uint8_t g_hmi_text_not_homed_gb2312[]  = {0xCEU, 0xB4U, 0xB8U, 0xB4U, 0xCEU, 0xBBU};
static const uint8_t g_hmi_text_normal_gb2312[]     = {0xD5U, 0xFDU, 0xB3U, 0xA3U};
static const uint8_t g_hmi_text_abnormal_gb2312[]   = {0xD2U, 0xECU, 0xB3U, 0xA3U};
static const uint8_t g_hmi_text_ng_sort_gb2312[]    = {0x4EU, 0x47U, 0xB7U, 0xD6U, 0xBCU, 0xF0U};

static const uint8_t g_hmi_status_ready_gb2312[]  = {0xBEU, 0xCDU, 0xD0U, 0xF7U};
static const uint8_t g_hmi_status_record_gb2312[] = {0xBCU, 0xC7U, 0xC2U, 0xBCU};
static const uint8_t g_hmi_status_play_gb2312[]   = {0xB2U, 0xA5U, 0xB7U, 0xC5U};
static const uint8_t g_hmi_status_stop_gb2312[]   = {0xCDU, 0xA3U, 0xD6U, 0xB9U};
static const uint8_t g_hmi_status_done_gb2312[]   = {0xCDU, 0xEAU, 0xB3U, 0xC9U};
static const uint8_t g_hmi_status_error_gb2312[]  = {0xB4U, 0xEDU, 0xCEU, 0xF3U};

static fsp_err_t hmi_uart_write_bytes(const uint8_t *p_data, uint32_t len);
static fsp_err_t hmi_uart_write_text_gb2312(const char *p_obj,
                                            const uint8_t *p_text,
                                            uint32_t text_len);
static void hmi_uart_send_joint_target_status(void);
static void hmi_uart_send_action_status(void);
static void hmi_uart_send_manual_status(bool force);
static void hmi_uart_send_home_status(void);
static bool hmi_uart_vision_board_is_online(void);
static void hmi_uart_manual_status_invalidate(void);
static void hmi_uart_leave_manual_if_active(void);

static void hmi_uart_frame_reset(void)
{
    g_hmi_uart_frame_len = 0;
    g_hmi_uart_frame_overflow = false;
}

static bool hmi_uart_frame_has_tail(void)
{
    if (g_hmi_uart_frame_len < 3U)
    {
        return false;
    }

    return ((0xFFU == g_hmi_uart_frame_buffer[g_hmi_uart_frame_len - 1U]) &&
            (0xFFU == g_hmi_uart_frame_buffer[g_hmi_uart_frame_len - 2U]) &&
            (0xFFU == g_hmi_uart_frame_buffer[g_hmi_uart_frame_len - 3U]));
}

static void hmi_uart_frame_handle(void)
{
    uint8_t channel;
    uint8_t axis;
    uint8_t direction;
    uint8_t mode;
    uint8_t coord_mode;
    uint8_t speed;
    uint8_t soft;
    uint8_t step;
    uint16_t pulse_us;
    uint16_t duration_ms;
    arm_action_info_t action_info;
    fsp_err_t err;

    if ((g_hmi_uart_frame_len < 2U) ||
        (HMI_UART_FRAME_HEADER != g_hmi_uart_frame_buffer[0]))
    {
        return;
    }

    if (HMI_UART_CMD_SERVO_US == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_SERVO_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        channel = g_hmi_uart_frame_buffer[2];
        pulse_us = (uint16_t)g_hmi_uart_frame_buffer[3] |
                   (uint16_t)((uint16_t)g_hmi_uart_frame_buffer[4] << 8U);

        err = arm_control_set_target_us(channel, pulse_us, ARM_CONTROL_SOURCE_HMI);
        if (FSP_SUCCESS == err)
        {
            pulse_us = arm_control_get_target_us(channel);
            my_printf(&g_debug_uart,
                      "HMI SERVO ch=%u us=%u\r\n",
                      (unsigned int)channel,
                      (unsigned int)pulse_us);
        }
        else
        {
            my_printf(&g_debug_uart,
                      "HMI SERVO ERR ch=%u us=%u err=%d\r\n",
                      (unsigned int)channel,
                      (unsigned int)pulse_us,
                      err);
        }
    }
    else if (HMI_UART_CMD_HOME == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_HOME_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_action_home(ARM_ACTION_HOME_DURATION_MS, ARM_CONTROL_SOURCE_HMI);
        if (FSP_SUCCESS == err)
        {
            my_printf(&g_debug_uart, "HMI HOME ok\r\n");
        }
        else
        {
            my_printf(&g_debug_uart, "HMI HOME ERR err=%d\r\n", err);
        }
    }
    else if (HMI_UART_CMD_SAVE_HOME == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_SAVE_HOME_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_control_save_home_from_current_targets();
        if (FSP_SUCCESS == err)
        {
            my_printf(&g_debug_uart, "HMI SAVE HOME flash ok\r\n");
        }
        else
        {
            my_printf(&g_debug_uart, "HMI SAVE HOME flash err=%d\r\n", err);
        }
    }
    else if (HMI_UART_CMD_JOINT_QUERY == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_JOINT_QUERY_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        hmi_uart_send_joint_target_status();
        my_printf(&g_debug_uart, "HMI JOINT QUERY\r\n");
    }
    else if (HMI_UART_CMD_ACTION_ADD == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_ADD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        duration_ms = (uint16_t)g_hmi_uart_frame_buffer[2] |
                      (uint16_t)((uint16_t)g_hmi_uart_frame_buffer[3] << 8U);

        err = arm_action_add_step(duration_ms);
        arm_action_get_info(&action_info);
        hmi_uart_send_action_status();
        my_printf(&g_debug_uart,
                  "HMI ACTION ADD duration=%u err=%d count=%u\r\n",
                  (unsigned int)duration_ms,
                  err,
                  (unsigned int)action_info.step_count);
    }
    else if (HMI_UART_CMD_ACTION_DELETE == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_CMD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_action_delete_last_step();
        arm_action_get_info(&action_info);
        hmi_uart_send_action_status();
        my_printf(&g_debug_uart,
                  "HMI ACTION DELETE err=%d count=%u\r\n",
                  err,
                  (unsigned int)action_info.step_count);
    }
    else if (HMI_UART_CMD_ACTION_CLEAR == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_CMD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_action_clear();
        hmi_uart_send_action_status();
        my_printf(&g_debug_uart, "HMI ACTION CLEAR err=%d\r\n", err);
    }
    else if (HMI_UART_CMD_ACTION_PLAY == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_CMD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_action_play();
        arm_action_get_info(&action_info);
        hmi_uart_send_action_status();
        my_printf(&g_debug_uart,
                  "HMI ACTION PLAY err=%d count=%u\r\n",
                  err,
                  (unsigned int)action_info.step_count);
    }
    else if (HMI_UART_CMD_ACTION_STOP == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_CMD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        err = arm_action_stop();
        hmi_uart_send_action_status();
        my_printf(&g_debug_uart, "HMI ACTION STOP err=%d\r\n", err);
    }
    else if (HMI_UART_CMD_ACTION_QUERY == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_ACTION_CMD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();

        hmi_uart_send_action_status();
        arm_action_get_info(&action_info);
        my_printf(&g_debug_uart,
                  "HMI ACTION QUERY count=%u current=%u last=%u\r\n",
                  (unsigned int)action_info.step_count,
                  (unsigned int)action_info.current_step,
                  (unsigned int)action_info.last_step);
    }
    else if (HMI_UART_CMD_MANUAL_JOG == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_MANUAL_JOG_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        axis = g_hmi_uart_frame_buffer[2];
        direction = g_hmi_uart_frame_buffer[3];
        mode = g_hmi_uart_frame_buffer[4];
        speed = g_hmi_uart_frame_buffer[5];
        soft = g_hmi_uart_frame_buffer[6];
        step = g_hmi_uart_frame_buffer[7];
        err = arm_manual_jog_command(axis, direction, mode, speed, soft, step);

        if ((FSP_SUCCESS == err) &&
            ((0U == direction) || (ARM_MANUAL_MODE_STEP == mode)))
        {
            hmi_uart_send_manual_status(true);
        }

        if (0U == direction)
        {
            my_printf(&g_debug_uart,
                      "HMI MANUAL STOP axis=%u mode=%u speed=%u soft=%u step=%u err=%d\r\n",
                      (unsigned int)axis,
                      (unsigned int)mode,
                      (unsigned int)speed,
                      (unsigned int)soft,
                      (unsigned int)step,
                      err);
        }
        else
        {
            my_printf(&g_debug_uart,
                      "HMI MANUAL JOG axis=%u dir=%u mode=%u speed=%u soft=%u step=%u err=%d\r\n",
                      (unsigned int)axis,
                      (unsigned int)direction,
                      (unsigned int)mode,
                      (unsigned int)speed,
                      (unsigned int)soft,
                      (unsigned int)step,
                      err);
        }
    }
    else if (HMI_UART_CMD_MANUAL_ENTER == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_MANUAL_PAGE_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        err = arm_manual_set_page_active(true);
        if (FSP_SUCCESS == err)
        {
            hmi_uart_manual_status_invalidate();
            hmi_uart_send_manual_status(true);
        }
        my_printf(&g_debug_uart, "HMI MANUAL ENTER err=%d\r\n", err);
    }
    else if (HMI_UART_CMD_MANUAL_LEAVE == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_MANUAL_PAGE_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        (void)arm_manual_set_page_active(false);
        hmi_uart_manual_status_invalidate();
        my_printf(&g_debug_uart, "HMI MANUAL LEAVE\r\n");
    }
    else if (HMI_UART_CMD_MANUAL_QUERY == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_MANUAL_PAGE_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        if (arm_manual_is_page_active())
        {
            hmi_uart_send_manual_status(true);
        }
        my_printf(&g_debug_uart, "HMI MANUAL QUERY\r\n");
    }
    else if (HMI_UART_CMD_MANUAL_COORD == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_MANUAL_COORD_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        coord_mode = g_hmi_uart_frame_buffer[2];
        err = arm_manual_set_coord_mode(coord_mode);
        if (arm_manual_is_page_active())
        {
            hmi_uart_manual_status_invalidate();
            hmi_uart_send_manual_status(true);
        }

        if ((ARM_MANUAL_COORD_WORKPIECE == coord_mode) ||
            (ARM_MANUAL_COORD_CAMERA == coord_mode))
        {
            my_printf(&g_debug_uart,
                      "HMI MANUAL COORD reserved coord=%u err=%d\r\n",
                      (unsigned int)coord_mode,
                      err);
        }
        else
        {
            my_printf(&g_debug_uart,
                      "HMI MANUAL COORD coord=%u err=%d\r\n",
                      (unsigned int)coord_mode,
                      err);
        }
    }
    else if (HMI_UART_CMD_HOME_QUERY == g_hmi_uart_frame_buffer[1])
    {
        if (HMI_UART_HOME_QUERY_FRAME_LEN != g_hmi_uart_frame_len)
        {
            return;
        }

        hmi_uart_leave_manual_if_active();
        hmi_uart_send_home_status();
        my_printf(&g_debug_uart, "HMI HOME QUERY\r\n");
    }
    else
    {
        /* 未知命令：仅保留原始帧打印，不做响应。 */
    }
}

static void hmi_uart_frame_print(void)
{
    if (g_hmi_uart_frame_overflow)
    {
        my_printf(&g_debug_uart, "HMI RX FRAME OVERFLOW\r\n");
        hmi_uart_frame_reset();
        return;
    }

    my_printf(&g_debug_uart, "HMI RX(%u):", (unsigned int)g_hmi_uart_frame_len);
    for (uint16_t i = 0; i < g_hmi_uart_frame_len; i++)
    {
        my_printf(&g_debug_uart, " %02X", (unsigned int)g_hmi_uart_frame_buffer[i]);
    }
    my_printf(&g_debug_uart, "\r\n");

    hmi_uart_frame_handle();
    hmi_uart_frame_reset();
}

static fsp_err_t hmi_uart_write_bytes(const uint8_t *p_data, uint32_t len)
{
    fsp_err_t err;
    uint32_t start_tick;

    if ((NULL == p_data) || (0U == len))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!g_hmi_uart_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    g_hmi_uart_tx_done = false;
    err = g_hmi_uart.p_api->write(g_hmi_uart.p_ctrl, p_data, len);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    start_tick = g_tick;
    while (!g_hmi_uart_tx_done)
    {
        if ((uint32_t)(g_tick - start_tick) > HMI_UART_TX_TIMEOUT_MS)
        {
            return FSP_ERR_TIMEOUT;
        }
    }

    return FSP_SUCCESS;
}

static const uint8_t *hmi_uart_action_state_text(arm_action_state_t state, uint32_t *p_len)
{
    switch (state)
    {
        case ARM_ACTION_STATE_RECORD:
            *p_len = sizeof(g_hmi_status_record_gb2312);
            return g_hmi_status_record_gb2312;

        case ARM_ACTION_STATE_PLAYING:
            *p_len = sizeof(g_hmi_status_play_gb2312);
            return g_hmi_status_play_gb2312;

        case ARM_ACTION_STATE_STOPPED:
            *p_len = sizeof(g_hmi_status_stop_gb2312);
            return g_hmi_status_stop_gb2312;

        case ARM_ACTION_STATE_DONE:
            *p_len = sizeof(g_hmi_status_done_gb2312);
            return g_hmi_status_done_gb2312;

        case ARM_ACTION_STATE_ERROR:
            *p_len = sizeof(g_hmi_status_error_gb2312);
            return g_hmi_status_error_gb2312;

        case ARM_ACTION_STATE_READY:
        default:
            *p_len = sizeof(g_hmi_status_ready_gb2312);
            return g_hmi_status_ready_gb2312;
    }
}

static fsp_err_t hmi_uart_write_text_gb2312(const char *p_obj,
                                            const uint8_t *p_text,
                                            uint32_t text_len)
{
    uint8_t tx_buf[HMI_UART_TX_BUFFER_SIZE];
    uint32_t obj_len;
    uint32_t pos = 0;

    if ((NULL == p_obj) || (NULL == p_text) || (0U == text_len))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    obj_len = (uint32_t)strlen(p_obj);
    if ((0U == obj_len) || ((obj_len + 6U + text_len + 4U) > HMI_UART_TX_BUFFER_SIZE))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    memcpy(&tx_buf[pos], p_obj, obj_len);
    pos += obj_len;
    memcpy(&tx_buf[pos], ".txt=\"", 6U);
    pos += 6U;
    memcpy(&tx_buf[pos], p_text, text_len);
    pos += text_len;
    tx_buf[pos++] = '"';
    tx_buf[pos++] = 0xFFU;
    tx_buf[pos++] = 0xFFU;
    tx_buf[pos++] = 0xFFU;

    return hmi_uart_write_bytes(tx_buf, pos);
}

static int32_t hmi_uart_calc_joint_display_value(uint8_t channel, uint16_t pulse_us)
{
    int32_t us = (int32_t)pulse_us;

    switch (channel)
    {
        case 0:
            return ((us - 1500) * 9) / 100;

        case 1:
            return ((2327 - us) * 90) / 1015;

        case 2:
            if (pulse_us <= 1500U)
            {
                return ((us - 1500) * 90) / 1000;
            }
            return ((us - 1500) * 90) / 856;

        case 3:
            return 0 - (((us - 1500) * 90) / 943);

        case 4:
            if (pulse_us <= 1457U)
            {
                return ((us - 1457) * 90) / 957;
            }
            return ((us - 1457) * 90) / 958;

        case 5:
            return ((us - 500) * 100) / 1000;

        default:
            return 0;
    }
}

static void hmi_uart_write_num_value(const char *p_obj, int32_t value)
{
    char cmd[32];

    if (value < 0)
    {
        (void)snprintf(cmd, sizeof(cmd), "%s.val=0-%lu", p_obj, (unsigned long)(0 - value));
    }
    else
    {
        (void)snprintf(cmd, sizeof(cmd), "%s.val=%lu", p_obj, (unsigned long)value);
    }

    (void)hmi_uart_write_cmd(cmd);
}

static bool hmi_uart_vision_board_is_online(void)
{
    if (!g_hmi_uart_vision_activity_seen)
    {
        return false;
    }

    return ((uint32_t)(g_tick - g_hmi_uart_vision_last_activity_tick) <=
            HMI_UART_VISION_ONLINE_TIMEOUT_MS);
}

static bool hmi_uart_home_pose_is_reached(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (arm_control_get_target_us(i) != arm_control_get_home_us(i))
        {
            return false;
        }
    }

    return true;
}

static void hmi_uart_send_home_status(void)
{
    arm_action_info_t action_info;
    arm_manual_info_t manual_info;
    const uint8_t *p_state_text = g_hmi_status_ready_gb2312;
    uint32_t state_text_len = sizeof(g_hmi_status_ready_gb2312);
    const uint8_t *p_mode_text = g_hmi_text_idle_gb2312;
    uint32_t mode_text_len = sizeof(g_hmi_text_idle_gb2312);
    const uint8_t *p_home_text;
    uint32_t home_text_len;
    const uint8_t *p_comm_text;
    uint32_t comm_text_len;
    const uint8_t *p_device_text;
    uint32_t device_text_len;

    (void)arm_manual_sync_from_control();
    arm_action_get_info(&action_info);
    arm_manual_get_info(&manual_info);

    if (!arm_control_is_ready() || (ARM_ACTION_STATE_ERROR == action_info.state))
    {
        p_state_text = g_hmi_status_error_gb2312;
        state_text_len = sizeof(g_hmi_status_error_gb2312);
    }
    else if (action_info.playing || manual_info.moving)
    {
        p_state_text = g_hmi_text_busy_gb2312;
        state_text_len = sizeof(g_hmi_text_busy_gb2312);
    }

    if ((ARM_ACTION_STATE_RECORD == action_info.state) ||
        (ARM_ACTION_STATE_PLAYING == action_info.state) ||
        action_info.playing)
    {
        p_mode_text = g_hmi_text_action_gb2312;
        mode_text_len = sizeof(g_hmi_text_action_gb2312);
    }
    else if (manual_info.page_active || manual_info.moving)
    {
        p_mode_text = g_hmi_text_manual_gb2312;
        mode_text_len = sizeof(g_hmi_text_manual_gb2312);
    }
    else
    {
        p_mode_text = g_hmi_text_idle_gb2312;
        mode_text_len = sizeof(g_hmi_text_idle_gb2312);
    }

    if (hmi_uart_home_pose_is_reached())
    {
        p_home_text = g_hmi_text_homed_gb2312;
        home_text_len = sizeof(g_hmi_text_homed_gb2312);
    }
    else
    {
        p_home_text = g_hmi_text_not_homed_gb2312;
        home_text_len = sizeof(g_hmi_text_not_homed_gb2312);
    }

    if ((0U == g_hmi_uart_rx_errors) && (0U == g_hmi_uart_rx_overflows))
    {
        p_comm_text = g_hmi_text_normal_gb2312;
        comm_text_len = sizeof(g_hmi_text_normal_gb2312);
    }
    else
    {
        p_comm_text = g_hmi_text_abnormal_gb2312;
        comm_text_len = sizeof(g_hmi_text_abnormal_gb2312);
    }

    if (hmi_uart_vision_board_is_online())
    {
        p_device_text = g_hmi_text_online_gb2312;
        device_text_len = sizeof(g_hmi_text_online_gb2312);
    }
    else
    {
        p_device_text = g_hmi_text_offline_gb2312;
        device_text_len = sizeof(g_hmi_text_offline_gb2312);
    }

    (void)hmi_uart_write_text_gb2312("t2", p_device_text, device_text_len);
    (void)hmi_uart_write_text_gb2312("t14", p_state_text, state_text_len);
    (void)hmi_uart_write_text_gb2312("t15", p_mode_text, mode_text_len);
    (void)hmi_uart_write_text_gb2312("t16", p_home_text, home_text_len);
    hmi_uart_write_num_value("o0", 0);
    (void)hmi_uart_write_text_gb2312("t18", p_comm_text, comm_text_len);
    (void)hmi_uart_write_text_gb2312("t19", g_hmi_text_ng_sort_gb2312, sizeof(g_hmi_text_ng_sort_gb2312));
    hmi_uart_write_num_value("o1", (int32_t)action_info.current_step);
    hmi_uart_write_num_value("o2", (int32_t)action_info.step_count);
    hmi_uart_write_num_value("o3", manual_info.value[ARM_MANUAL_AXIS_X]);
    hmi_uart_write_num_value("o4", manual_info.value[ARM_MANUAL_AXIS_Y]);
    hmi_uart_write_num_value("o5", manual_info.value[ARM_MANUAL_AXIS_Z]);
    hmi_uart_write_num_value("o6", manual_info.value[ARM_MANUAL_AXIS_PITCH]);
    hmi_uart_write_num_value("o7", manual_info.value[ARM_MANUAL_AXIS_ROLL]);
    hmi_uart_write_num_value("o8", manual_info.value[ARM_MANUAL_AXIS_GRIP]);
}

static void hmi_uart_send_joint_target_status(void)
{
    static const char * const p_slider_obj[ARM_SERVO_COUNT] =
    {
        "h0", "h1", "h2", "h3", "h4", "h5"
    };
    static const char * const p_pulse_obj[ARM_SERVO_COUNT] =
    {
        "n0", "n1", "n2", "n3", "n4", "n5"
    };
    static const char * const p_display_obj[ARM_SERVO_COUNT] =
    {
        "n6", "n7", "n8", "n9", "n10", "n11"
    };

    uint16_t pulse_us;
    int32_t display_value;

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        pulse_us = arm_control_get_target_us(i);
        display_value = hmi_uart_calc_joint_display_value(i, pulse_us);

        hmi_uart_write_num_value(p_slider_obj[i], (int32_t)pulse_us);
        hmi_uart_write_num_value(p_pulse_obj[i], (int32_t)pulse_us);
        hmi_uart_write_num_value(p_display_obj[i], display_value);
    }
}

static void hmi_uart_send_action_status(void)
{
    arm_action_info_t info;
    const uint8_t *p_state_text;
    uint32_t state_text_len;
    char cmd[32];

    arm_action_get_info(&info);

    (void)snprintf(cmd, sizeof(cmd), "n0.val=%u", (unsigned int)info.slot_index);
    (void)hmi_uart_write_cmd(cmd);
    (void)snprintf(cmd, sizeof(cmd), "n1.val=%u", (unsigned int)info.step_count);
    (void)hmi_uart_write_cmd(cmd);
    (void)snprintf(cmd, sizeof(cmd), "n2.val=%u", (unsigned int)info.current_step);
    (void)hmi_uart_write_cmd(cmd);
    (void)snprintf(cmd, sizeof(cmd), "n3.val=%u", (unsigned int)info.step_count);
    (void)hmi_uart_write_cmd(cmd);
    (void)snprintf(cmd, sizeof(cmd), "n4.val=%u", (unsigned int)info.last_step);
    (void)hmi_uart_write_cmd(cmd);

    p_state_text = hmi_uart_action_state_text(info.state, &state_text_len);
    (void)hmi_uart_write_text_gb2312("t1", p_state_text, state_text_len);
}

static void hmi_uart_send_manual_status(bool force)
{
    static const char * const p_value_obj[ARM_MANUAL_AXIS_COUNT] =
    {
        "n0", "n1", "n2", "n3", "n4", "n5"
    };
    static const char * const p_param_obj[3] =
    {
        "n6", "n7", "n8"
    };

    arm_manual_info_t info;
    uint8_t param_value[3];

    arm_manual_get_info(&info);

    for (uint8_t i = 0; i < ARM_MANUAL_AXIS_COUNT; i++)
    {
        if (force ||
            (!g_hmi_uart_manual_status_valid) ||
            (info.value[i] != g_hmi_uart_last_manual_info.value[i]))
        {
            hmi_uart_write_num_value(p_value_obj[i], info.value[i]);
        }
    }

    param_value[0] = info.speed;
    param_value[1] = info.soft;
    param_value[2] = info.step;

    if (force ||
        (!g_hmi_uart_manual_status_valid) ||
        (info.speed != g_hmi_uart_last_manual_info.speed))
    {
        hmi_uart_write_num_value(p_param_obj[0], (int32_t)param_value[0]);
    }

    if (force ||
        (!g_hmi_uart_manual_status_valid) ||
        (info.soft != g_hmi_uart_last_manual_info.soft))
    {
        hmi_uart_write_num_value(p_param_obj[1], (int32_t)param_value[1]);
    }

    if (force ||
        (!g_hmi_uart_manual_status_valid) ||
        (info.step != g_hmi_uart_last_manual_info.step))
    {
        hmi_uart_write_num_value(p_param_obj[2], (int32_t)param_value[2]);
    }

    g_hmi_uart_last_manual_info = info;
    g_hmi_uart_manual_status_valid = true;
}

static void hmi_uart_manual_status_invalidate(void)
{
    g_hmi_uart_manual_status_valid = false;
}

static void hmi_uart_leave_manual_if_active(void)
{
    if (arm_manual_is_page_active())
    {
        (void)arm_manual_set_page_active(false);
        hmi_uart_manual_status_invalidate();
    }
}

fsp_err_t hmi_uart_init(void)
{
    fsp_err_t err;

    rt_ringbuffer_init(&g_hmi_uart_rx_rb,
                       g_hmi_uart_rx_pool,
                       (rt_int16_t)sizeof(g_hmi_uart_rx_pool));

    g_hmi_uart_tx_done = false;
    g_hmi_uart_rx_overflows = 0;
    g_hmi_uart_rx_errors = 0;
    g_hmi_uart_init_tick = g_tick;
    g_hmi_uart_sendme_sent = false;
    g_hmi_uart_ready = false;
    g_hmi_uart_vision_activity_seen = false;
    g_hmi_uart_vision_last_activity_tick = 0;
    hmi_uart_manual_status_invalidate();
    hmi_uart_frame_reset();

    err = g_hmi_uart.p_api->open(g_hmi_uart.p_ctrl, g_hmi_uart.p_cfg);
    if (FSP_SUCCESS == err)
    {
        g_hmi_uart_ready = true;
    }

    return err;
}

void hmi_uart_notify_vision_board_activity(void)
{
    g_hmi_uart_vision_activity_seen = true;
    g_hmi_uart_vision_last_activity_tick = g_tick;
}

void hmi_uart_callback(uart_callback_args_t *p_args)
{
    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        g_hmi_uart_tx_done = true;
    }
    else if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t rx_data = (uint8_t)p_args->data;

        if (0 == rt_ringbuffer_putchar(&g_hmi_uart_rx_rb, rx_data))
        {
            g_hmi_uart_rx_overflows++;
        }
    }
    else if ((UART_EVENT_ERR_PARITY == p_args->event) ||
             (UART_EVENT_ERR_FRAMING == p_args->event) ||
             (UART_EVENT_ERR_OVERFLOW == p_args->event) ||
             (UART_EVENT_BREAK_DETECT == p_args->event))
    {
        g_hmi_uart_rx_errors++;
    }
    else
    {
        /* 其他 UART 事件无需处理。 */
    }
}

fsp_err_t hmi_uart_write_cmd(const char *p_cmd)
{
    uint8_t tx_buf[HMI_UART_TX_BUFFER_SIZE];
    uint32_t len;

    if (NULL == p_cmd)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    len = (uint32_t)strlen(p_cmd);
    if ((0U == len) || ((len + 3U) > HMI_UART_TX_BUFFER_SIZE))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    memcpy(tx_buf, p_cmd, len);
    tx_buf[len] = 0xFFU;
    tx_buf[len + 1U] = 0xFFU;
    tx_buf[len + 2U] = 0xFFU;

    return hmi_uart_write_bytes(tx_buf, len + 3U);
}

bool hmi_uart_get_char(uint8_t *p_ch)
{
    if (NULL == p_ch)
    {
        return false;
    }

    return (1 == rt_ringbuffer_getchar(&g_hmi_uart_rx_rb, p_ch));
}

uint32_t hmi_uart_available(void)
{
    return (uint32_t)rt_ringbuffer_data_len(&g_hmi_uart_rx_rb);
}

uint32_t hmi_uart_get_rx_overflows(void)
{
    return g_hmi_uart_rx_overflows;
}

uint32_t hmi_uart_get_rx_errors(void)
{
    return g_hmi_uart_rx_errors;
}

void hmi_uart_task(void)
{
    uint8_t ch;
    fsp_err_t err;

    if (!g_hmi_uart_ready)
    {
        return;
    }

    while (hmi_uart_get_char(&ch))
    {
        if (g_hmi_uart_frame_len < HMI_UART_FRAME_BUFFER_SIZE)
        {
            g_hmi_uart_frame_buffer[g_hmi_uart_frame_len] = ch;
            g_hmi_uart_frame_len++;
        }
        else
        {
            g_hmi_uart_frame_overflow = true;
        }

        if (g_hmi_uart_frame_overflow || hmi_uart_frame_has_tail())
        {
            hmi_uart_frame_print();
        }
    }

    if (!g_hmi_uart_sendme_sent &&
        ((uint32_t)(g_tick - g_hmi_uart_init_tick) >= HMI_UART_SENDME_DELAY_MS))
    {
        g_hmi_uart_sendme_sent = true;
        err = hmi_uart_write_cmd("sendme");
        my_printf(&g_debug_uart, "HMI TX: sendme err=%d\r\n", err);
    }

}

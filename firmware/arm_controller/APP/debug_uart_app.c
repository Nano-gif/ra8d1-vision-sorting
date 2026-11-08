#include "debug_uart_app.h"
#include "mydefine.h"

#define DEBUG_UART_RX_BUFFER_SIZE    (128U)
#define DEBUG_UART_FRAME_BUFFER_SIZE (128U)
#define DEBUG_UART_FRAME_TIMEOUT_MS  (10U)
#define DEBUG_UART_SERVO_MAX_CHANNEL (ARM_SERVO_COUNT - 1U)

static struct rt_ringbuffer g_debug_uart_rx_rb;
static uint8_t g_debug_uart_rx_pool[DEBUG_UART_RX_BUFFER_SIZE];
static volatile uint32_t g_debug_uart_rx_overflows = 0;
static volatile uint32_t g_debug_uart_last_rx_tick = 0;
static uint8_t g_debug_uart_frame_buffer[DEBUG_UART_FRAME_BUFFER_SIZE + 1U];
static uint16_t g_debug_uart_frame_len = 0;
static bool g_debug_uart_frame_overflow = false;

static void debug_uart_frame_reset(void)
{
    g_debug_uart_frame_len = 0;
    g_debug_uart_frame_overflow = false;
}

static bool debug_uart_is_digit(uint8_t ch)
{
    return ((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9'));
}

static bool debug_uart_is_separator(uint8_t ch)
{
    return ((ch == (uint8_t)' ') ||
            (ch == (uint8_t)'\t') ||
            (ch == (uint8_t)',') ||
            (ch == (uint8_t)'\r') ||
            (ch == (uint8_t)'\n'));
}

static bool debug_uart_parse_u16(uint16_t *p_index, uint16_t *p_value)
{
    uint32_t value = 0;
    bool has_digit = false;

    while ((*p_index < g_debug_uart_frame_len) &&
           debug_uart_is_separator(g_debug_uart_frame_buffer[*p_index]))
    {
        (*p_index)++;
    }

    while ((*p_index < g_debug_uart_frame_len) &&
           debug_uart_is_digit(g_debug_uart_frame_buffer[*p_index]))
    {
        has_digit = true;
        value = (value * 10U) + (uint32_t)(g_debug_uart_frame_buffer[*p_index] - (uint8_t)'0');
        if (value > 65535U)
        {
            return false;
        }
        (*p_index)++;
    }

    if (!has_digit)
    {
        return false;
    }

    *p_value = (uint16_t)value;
    return true;
}

static void debug_uart_frame_execute(void)
{
    uint16_t index = 0;
    uint16_t channel = 0;
    uint16_t pulse_us = 0;
    fsp_err_t err;

    if (g_debug_uart_frame_overflow)
    {
        my_printf(&g_debug_uart, "RX FRAME OVERFLOW\r\n");
        debug_uart_frame_reset();
        return;
    }

    if (!debug_uart_parse_u16(&index, &channel) ||
        !debug_uart_parse_u16(&index, &pulse_us))
    {
        my_printf(&g_debug_uart, "ERR\r\n");
        debug_uart_frame_reset();
        return;
    }

    while ((index < g_debug_uart_frame_len) &&
           debug_uart_is_separator(g_debug_uart_frame_buffer[index]))
    {
        index++;
    }

    if ((index != g_debug_uart_frame_len) ||
        (channel > DEBUG_UART_SERVO_MAX_CHANNEL))
    {
        my_printf(&g_debug_uart, "ERR\r\n");
        debug_uart_frame_reset();
        return;
    }

    err = arm_control_set_target_us((uint8_t)channel, pulse_us, ARM_CONTROL_SOURCE_DEBUG);
    if (FSP_SUCCESS == err)
    {
        pulse_us = arm_control_get_target_us((uint8_t)channel);
        my_printf(&g_debug_uart,
                  "OK ch=%u us=%u tick=%u\r\n",
                  (unsigned int)channel,
                  (unsigned int)pulse_us,
                  (unsigned int)pca9685_servo_us_to_tick(pulse_us));
    }
    else
    {
        my_printf(&g_debug_uart, "ERR %d\r\n", err);
    }

    debug_uart_frame_reset();
}

fsp_err_t debug_uart_init(void)
{
    rt_ringbuffer_init(&g_debug_uart_rx_rb,
                       g_debug_uart_rx_pool,
                       (rt_int16_t)sizeof(g_debug_uart_rx_pool));

    g_debug_uart_rx_overflows = 0;
    g_debug_uart_last_rx_tick = 0;
    debug_uart_frame_reset();

    return g_debug_uart.p_api->open(g_debug_uart.p_ctrl, g_debug_uart.p_cfg);
}

void debug_uart_callback(uart_callback_args_t *p_args)
{
    my_printf_callback(p_args);

    if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t rx_data = (uint8_t)p_args->data;

        if (0 == rt_ringbuffer_putchar(&g_debug_uart_rx_rb, rx_data))
        {
            g_debug_uart_rx_overflows++;
        }

        g_debug_uart_last_rx_tick = g_tick;
    }
}

bool debug_uart_get_char(uint8_t *p_ch)
{
    if (NULL == p_ch)
    {
        return false;
    }

    return (1 == rt_ringbuffer_getchar(&g_debug_uart_rx_rb, p_ch));
}

uint32_t debug_uart_available(void)
{
    return (uint32_t)rt_ringbuffer_data_len(&g_debug_uart_rx_rb);
}

uint32_t debug_uart_get_rx_overflows(void)
{
    return g_debug_uart_rx_overflows;
}

void debug_uart_task(void)
{
    uint8_t ch;

    while (debug_uart_get_char(&ch))
    {
        if (g_debug_uart_frame_len < DEBUG_UART_FRAME_BUFFER_SIZE)
        {
            g_debug_uart_frame_buffer[g_debug_uart_frame_len] = ch;
            g_debug_uart_frame_len++;
        }
        else
        {
            g_debug_uart_frame_overflow = true;
        }
    }

    if ((g_debug_uart_frame_len > 0U) || g_debug_uart_frame_overflow)
    {
        if ((uint32_t)(g_tick - g_debug_uart_last_rx_tick) >= DEBUG_UART_FRAME_TIMEOUT_MS)
        {
            debug_uart_frame_execute();
        }
    }
}

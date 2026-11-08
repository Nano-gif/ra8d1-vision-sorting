#include "rs485_uart_app.h"
#include "mydefine.h"

#define RS485_UART_RX_BUFFER_SIZE       (256U)
#define RS485_UART_TX_TIMEOUT_MS        (100U)
#define RS485_UART_DE_SETTLE_US         (2U)

static struct rt_ringbuffer g_rs485_uart_rx_rb;
static uint8_t g_rs485_uart_rx_pool[RS485_UART_RX_BUFFER_SIZE];
static volatile bool g_rs485_uart_tx_done = false;
static volatile uint32_t g_rs485_uart_rx_overflows = 0;
static volatile uint32_t g_rs485_uart_rx_errors = 0;
static bool g_rs485_uart_ready = false;

static void rs485_uart_de_set(bool enable)
{
    (void)R_IOPORT_PinWrite(&g_ioport_ctrl,
                            RA6_485_DE,
                            enable ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

fsp_err_t rs485_uart_init(void)
{
    fsp_err_t err;

    rt_ringbuffer_init(&g_rs485_uart_rx_rb,
                       g_rs485_uart_rx_pool,
                       (rt_int16_t)sizeof(g_rs485_uart_rx_pool));

    g_rs485_uart_tx_done = false;
    g_rs485_uart_rx_overflows = 0;
    g_rs485_uart_rx_errors = 0;
    g_rs485_uart_ready = false;
    rs485_uart_de_set(false);

    err = g_rs485_uart.p_api->open(g_rs485_uart.p_ctrl, g_rs485_uart.p_cfg);
    if (FSP_SUCCESS == err)
    {
        g_rs485_uart_ready = true;
        rs485_uart_de_set(false);
    }

    return err;
}

void rs485_uart_callback(uart_callback_args_t *p_args)
{
    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        g_rs485_uart_tx_done = true;
    }
    else if (UART_EVENT_RX_CHAR == p_args->event)
    {
        uint8_t rx_data = (uint8_t)p_args->data;

        if (0 == rt_ringbuffer_putchar(&g_rs485_uart_rx_rb, rx_data))
        {
            g_rs485_uart_rx_overflows++;
        }
    }
    else if ((UART_EVENT_ERR_PARITY == p_args->event) ||
             (UART_EVENT_ERR_FRAMING == p_args->event) ||
             (UART_EVENT_ERR_OVERFLOW == p_args->event) ||
             (UART_EVENT_BREAK_DETECT == p_args->event))
    {
        g_rs485_uart_rx_errors++;
    }
    else
    {
        /* 其他 UART 事件无需处理。 */
    }
}

fsp_err_t rs485_uart_write(const uint8_t *p_data, uint32_t len)
{
    fsp_err_t err;
    uint32_t start_tick;

    if ((NULL == p_data) || (0U == len))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (!g_rs485_uart_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    g_rs485_uart_tx_done = false;
    rs485_uart_de_set(true);
    R_BSP_SoftwareDelay(RS485_UART_DE_SETTLE_US, BSP_DELAY_UNITS_MICROSECONDS);

    err = g_rs485_uart.p_api->write(g_rs485_uart.p_ctrl, p_data, len);
    if (FSP_SUCCESS != err)
    {
        rs485_uart_de_set(false);
        return err;
    }

    start_tick = g_tick;
    while (!g_rs485_uart_tx_done)
    {
        if ((uint32_t)(g_tick - start_tick) > RS485_UART_TX_TIMEOUT_MS)
        {
            rs485_uart_de_set(false);
            return FSP_ERR_TIMEOUT;
        }
    }

    R_BSP_SoftwareDelay(RS485_UART_DE_SETTLE_US, BSP_DELAY_UNITS_MICROSECONDS);
    rs485_uart_de_set(false);

    return FSP_SUCCESS;
}

bool rs485_uart_get_char(uint8_t *p_ch)
{
    if (NULL == p_ch)
    {
        return false;
    }

    return (1 == rt_ringbuffer_getchar(&g_rs485_uart_rx_rb, p_ch));
}

uint32_t rs485_uart_available(void)
{
    return (uint32_t)rt_ringbuffer_data_len(&g_rs485_uart_rx_rb);
}

uint32_t rs485_uart_get_rx_overflows(void)
{
    return g_rs485_uart_rx_overflows;
}

uint32_t rs485_uart_get_rx_errors(void)
{
    return g_rs485_uart_rx_errors;
}

void rs485_uart_task(void)
{
    /* 接收字节由 modbus_slave_task() 消费。 */
}

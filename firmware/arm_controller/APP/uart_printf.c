#include "uart_printf.h"

#define MY_PRINTF_TX_TIMEOUT_MS (100U)
#define MY_PRINTF_BUFFER_SIZE   (512U)

static volatile bool my_printf_tx_done = false;
static char g_my_printf_buffer[MY_PRINTF_BUFFER_SIZE];

int my_printf(const uart_instance_t *uart, const char *format,...)
{
	va_list arg;
	int len;
	fsp_err_t err;
	uint32_t start_tick;

	va_start(arg, format);
	len = vsnprintf(g_my_printf_buffer, sizeof(g_my_printf_buffer), format, arg);
	va_end(arg);
	
	if(len<=0)
	{
		return len;
	}
	
	if(len >= (int)sizeof(g_my_printf_buffer))
	{
		len = sizeof(g_my_printf_buffer) - 1;
	}
	
	my_printf_tx_done = false;
	
	err = uart->p_api->write(uart->p_ctrl, (uint8_t *)g_my_printf_buffer, (uint32_t)len);
	if(FSP_SUCCESS != err)
	{
		return -1;
	}

	start_tick = g_tick;
	while(!my_printf_tx_done)
	{
		if ((uint32_t)(g_tick - start_tick) > MY_PRINTF_TX_TIMEOUT_MS)
		{
			return -1;
		}
	}
	return len;
}

void my_printf_callback(uart_callback_args_t *p_args)
{
	if (p_args->event == UART_EVENT_TX_COMPLETE)
	{
			my_printf_tx_done = true;
	}
}

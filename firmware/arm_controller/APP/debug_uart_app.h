#ifndef DEBUG_UART_APP_H_
#define DEBUG_UART_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

fsp_err_t debug_uart_init(void);
void debug_uart_callback(uart_callback_args_t *p_args);
bool debug_uart_get_char(uint8_t *p_ch);
uint32_t debug_uart_available(void);
uint32_t debug_uart_get_rx_overflows(void);
void debug_uart_task(void);

#endif

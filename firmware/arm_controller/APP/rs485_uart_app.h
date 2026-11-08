#ifndef RS485_UART_APP_H_
#define RS485_UART_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

fsp_err_t rs485_uart_init(void);
void rs485_uart_callback(uart_callback_args_t *p_args);
fsp_err_t rs485_uart_write(const uint8_t *p_data, uint32_t len);
bool rs485_uart_get_char(uint8_t *p_ch);
uint32_t rs485_uart_available(void);
uint32_t rs485_uart_get_rx_overflows(void);
uint32_t rs485_uart_get_rx_errors(void);
void rs485_uart_task(void);

#endif

#ifndef UART_PRINTF_H_
#define UART_PRINTF_H_
#include "mydefine.h"


int my_printf(const uart_instance_t *uart, const char *format, ...);
void my_printf_callback(uart_callback_args_t *p_args);

#endif
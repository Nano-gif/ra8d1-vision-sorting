#ifndef HMI_UART_APP_H_
#define HMI_UART_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

fsp_err_t hmi_uart_init(void);
void hmi_uart_notify_vision_board_activity(void);
void hmi_uart_callback(uart_callback_args_t *p_args);
fsp_err_t hmi_uart_write_cmd(const char *p_cmd);
bool hmi_uart_get_char(uint8_t *p_ch);
uint32_t hmi_uart_available(void);
uint32_t hmi_uart_get_rx_overflows(void);
uint32_t hmi_uart_get_rx_errors(void);
void hmi_uart_task(void);

#endif

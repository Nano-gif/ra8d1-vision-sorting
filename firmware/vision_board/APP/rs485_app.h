#ifndef RS485_APP_H
#define RS485_APP_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS485_APP_DEFAULT_DEVICE "soft485"

int rs485_app_open(const char *dev_name);
rt_size_t rs485_app_send(const void *data, rt_size_t size);
rt_size_t rs485_app_read(void *buffer, rt_size_t size);
int rs485_app_start_rx_window(uint32_t timeout_ms);
void rs485_app_clear_rx(void);
const char *rs485_app_device_name(void);

#ifdef __cplusplus
}
#endif

#endif /* RS485_APP_H */

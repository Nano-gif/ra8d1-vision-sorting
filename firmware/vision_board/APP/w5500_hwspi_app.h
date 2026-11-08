#ifndef APP_W5500_HWSPI_APP_H_
#define APP_W5500_HWSPI_APP_H_

#include <rtthread.h>

int w5500_hwspi_open(void);
void w5500_hwspi_reset(void);
void w5500_hwspi_cs_select(void);
void w5500_hwspi_cs_deselect(void);
int w5500_hwspi_transfer(const rt_uint8_t *tx, rt_uint8_t *rx, rt_uint32_t length);
rt_uint8_t w5500_hwspi_transfer_byte(rt_uint8_t value);
void w5500_hwspi_read_burst(rt_uint8_t *buf, rt_uint16_t len);
void w5500_hwspi_write_burst(rt_uint8_t *buf, rt_uint16_t len);
int w5500_hwspi_int_level(void);

#endif /* APP_W5500_HWSPI_APP_H_ */

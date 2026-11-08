#ifndef MODBUS_SLAVE_APP_H_
#define MODBUS_SLAVE_APP_H_

#include "hal_data.h"
#include <stdint.h>

fsp_err_t modbus_slave_init(void);
void modbus_slave_task(void);
uint16_t modbus_slave_get_comm_error_count(void);

#endif

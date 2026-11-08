/* 应用层公共头文件：汇总系统头、各 APP 模块头与全局 tick */

#ifndef MYDEFINE_H_
#define MYDEFINE_H_

/* 系统头文件 */
#include "hal_data.h"
#include "scheduler.h"
#include "bsp_systick.h"
#include "stdio.h"
#include "stdarg.h"
#include "string.h"
#include <stdbool.h>
/* APP */
#include "uart_printf.h"
#include "debug_uart_app.h"
#include "hmi_uart_app.h"
#include "rs485_uart_app.h"
#include "modbus_slave_app.h"
#include "pca9685_app.h"
#include "arm_control_app.h"
#include "arm_config_app.h"
#include "arm_kinematics_app.h"
#include "arm_action_app.h"
#include "arm_manual_app.h"
#include "fault_diag_app.h"
#include "buzzer_app.h"
/* Components */
#include "ringbuffer.h"

/* 外部引用 */
extern volatile uint32_t g_tick;
#endif /* MYDEFINE_H_ */

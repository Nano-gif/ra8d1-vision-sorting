/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_sci_uart.h"
            #include "r_uart_api.h"
#include "r_flash_hp.h"
#include "r_flash_api.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
FSP_HEADER
/** UART on SCI Instance. */
            extern const uart_instance_t      g_rs485_uart;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_uart_instance_ctrl_t     g_rs485_uart_ctrl;
            extern const uart_cfg_t g_rs485_uart_cfg;
            extern const sci_uart_extended_cfg_t g_rs485_uart_cfg_extend;

            #ifndef rs485_uart_callback
            void rs485_uart_callback(uart_callback_args_t * p_args);
            #endif
/* Flash on Flash HP Instance */
extern const flash_instance_t g_flash0;

/** Access the Flash HP instance using these structures when calling API functions directly (::p_api is not used). */
extern flash_hp_instance_ctrl_t g_flash0_ctrl;
extern const flash_cfg_t g_flash0_cfg;

#ifndef NULL
void NULL(flash_callback_args_t * p_args);
#endif
/** UART on SCI Instance. */
            extern const uart_instance_t      g_hmi_uart;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_uart_instance_ctrl_t     g_hmi_uart_ctrl;
            extern const uart_cfg_t g_hmi_uart_cfg;
            extern const sci_uart_extended_cfg_t g_hmi_uart_cfg_extend;

            #ifndef hmi_uart_callback
            void hmi_uart_callback(uart_callback_args_t * p_args);
            #endif
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_pca_i2c;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_pca_i2c_ctrl;
extern const i2c_master_cfg_t g_pca_i2c_cfg;

#ifndef pca_i2c_callback
void pca_i2c_callback(i2c_master_callback_args_t * p_args);
#endif
/** UART on SCI Instance. */
            extern const uart_instance_t      g_debug_uart;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_uart_instance_ctrl_t     g_debug_uart_ctrl;
            extern const uart_cfg_t g_debug_uart_cfg;
            extern const sci_uart_extended_cfg_t g_debug_uart_cfg_extend;

            #ifndef debug_uart_callback
            void debug_uart_callback(uart_callback_args_t * p_args);
            #endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */

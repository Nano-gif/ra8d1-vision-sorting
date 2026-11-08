/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_dtc.h"
#include "r_transfer_api.h"
#include "r_sci_b_spi.h"
#include "r_spi_api.h"
#include "r_dmac.h"
#include "r_transfer_api.h"
#include "r_sdhi.h"
#include "r_sdmmc_api.h"
#include "r_capture_api.h"
            #include "r_ceu.h"
#include "r_sci_b_i2c.h"
#include "r_i2c_master_api.h"
#include "r_sci_b_uart.h"
            #include "r_uart_api.h"
FSP_HEADER
/** Timer on GPT Instance. */
extern const timer_instance_t g_light_pwm_timer;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_light_pwm_timer_ctrl;
extern const timer_cfg_t g_light_pwm_timer_cfg;

#ifndef light_pwm_app_timer_cb
void light_pwm_app_timer_cb(timer_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_rs485_rx_timer;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_rs485_rx_timer_ctrl;
extern const timer_cfg_t g_rs485_rx_timer_cfg;

#ifndef rs485_app_rx_timer_cb
void rs485_app_rx_timer_cb(timer_callback_args_t * p_args);
#endif
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer2;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer2_ctrl;
extern const transfer_cfg_t g_transfer2_cfg;
/* Transfer on DTC Instance. */
extern const transfer_instance_t g_transfer1;

/** Access the DTC instance using these structures when calling API functions directly (::p_api is not used). */
extern dtc_instance_ctrl_t g_transfer1_ctrl;
extern const transfer_cfg_t g_transfer1_cfg;
/** SPI on SCI Instance. */
extern const spi_instance_t g_sci_spi2;

/** Access the SCI_B_SPI instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_b_spi_instance_ctrl_t g_sci_spi2_ctrl;
extern const spi_cfg_t g_sci_spi2_cfg;

/** Called by the driver when a transfer has completed or an error has occurred (Must be implemented by the user). */
#ifndef sci2_b_spi_callback
void sci2_b_spi_callback(spi_callback_args_t * p_args);
#endif
/* Transfer on DMAC Instance. */
extern const transfer_instance_t g_transfer0;

/** Access the DMAC instance using these structures when calling API functions directly (::p_api is not used). */
extern dmac_instance_ctrl_t g_transfer0_ctrl;
extern const transfer_cfg_t g_transfer0_cfg;

#ifndef g_sdmmc1_dmac_callback
void g_sdmmc1_dmac_callback(dmac_callback_args_t * p_args);
#endif
/** SDMMC on SDMMC Instance. */
extern const sdmmc_instance_t g_sdmmc1;


/** Access the SDMMC instance using these structures when calling API functions directly (::p_api is not used). */
extern sdhi_instance_ctrl_t g_sdmmc1_ctrl;
extern sdmmc_cfg_t g_sdmmc1_cfg;

#ifndef sdhi_callback
void sdhi_callback(sdmmc_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer7;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer7_ctrl;
extern const timer_cfg_t g_timer7_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/* CEU on CAPTURE instance */
            extern const capture_instance_t g_ceu_qvga;
            /* Access the CEU instance using these structures when calling API functions directly (::p_api is not used). */
            extern ceu_instance_ctrl_t g_ceu_qvga_ctrl;
            extern const capture_cfg_t g_ceu_qvga_cfg;
            #ifndef g_ceu_callback
            void g_ceu_callback(capture_callback_args_t * p_args);
            #endif
extern const i2c_master_cfg_t g_sci3_cfg;
/* I2C on SCI Instance. */
extern const i2c_master_instance_t g_sci3;
#ifndef sci_i2c_irq_callback
void sci_i2c_irq_callback(i2c_master_callback_args_t * p_args);
#endif

extern const sci_b_i2c_extended_cfg_t g_sci3_cfg_extend;
extern sci_b_i2c_instance_ctrl_t g_sci3_ctrl;
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer6;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer6_ctrl;
extern const timer_cfg_t g_timer6_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/** UART on SCI Instance. */
            extern const uart_instance_t      g_uart9;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_b_uart_instance_ctrl_t     g_uart9_ctrl;
            extern const uart_cfg_t g_uart9_cfg;
            extern const sci_b_uart_extended_cfg_t g_uart9_cfg_extend;

            #ifndef user_uart9_callback
            void user_uart9_callback(uart_callback_args_t * p_args);
            #endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */

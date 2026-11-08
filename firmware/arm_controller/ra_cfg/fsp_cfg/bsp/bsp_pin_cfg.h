/* generated configuration header file - do not edit */
#ifndef BSP_PIN_CFG_H_
#define BSP_PIN_CFG_H_
#include "r_ioport.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

#define RA6_485_DE (BSP_IO_PORT_04_PIN_07)
#define RA6_485_RX (BSP_IO_PORT_04_PIN_08)
#define RA6_485_TX (BSP_IO_PORT_04_PIN_09)
#define RA6_TJC_RX (BSP_IO_PORT_04_PIN_10)
#define RA6_TJC_TX (BSP_IO_PORT_04_PIN_11)
#define RA6_BUZZER (BSP_IO_PORT_04_PIN_13)
#define RA6_PCA_SDA (BSP_IO_PORT_04_PIN_14)
#define RA6_PCA_SCL (BSP_IO_PORT_04_PIN_15)
#define RA6_DBG_TX (BSP_IO_PORT_05_PIN_01)
#define RA6_DBG_RX (BSP_IO_PORT_05_PIN_02)

extern const ioport_cfg_t g_bsp_pin_cfg; /* R7FA6M5BF3CFP.pincfg */

void BSP_PinConfigSecurityInit();

/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER
#endif /* BSP_PIN_CFG_H_ */

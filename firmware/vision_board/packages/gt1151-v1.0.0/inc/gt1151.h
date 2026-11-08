/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author          Notes
 * 2020-11-25     tang jia       the first version
 */
#ifndef APPLICATIONS_GT1151_H_
#define APPLICATIONS_GT1151_H_

#include "rtconfig.h"

/*
 * Vision Board 4.3-inch touch panel:
 *
 * - GT1151 RST -> P000
 * - GT1151 INT -> P010
 * - GT1151 I2C -> SCI3 Simple I2C bus, device name "sci3i"
 *
 * RT-Thread package default values 51/108 are from another BSP's pin numbering.
 * RA BSP uses FSP pin encoding, for example:
 *
 * - BSP_IO_PORT_00_PIN_00 = 0x0000
 * - BSP_IO_PORT_00_PIN_10 = 0x000A
 *
 * Keep this board override here so the package is readable and reusable.
 */
#ifdef SOC_R7FA8D1AH
#include "hal_data.h"

#undef GT1151_BUS_NAME
#define GT1151_BUS_NAME    "sci3i"

#undef GT1151_RST_PIN
#define GT1151_RST_PIN     BSP_IO_PORT_00_PIN_00

#undef GT1151_IRQ_PIN
#define GT1151_IRQ_PIN     BSP_IO_PORT_00_PIN_10
#endif

#ifndef GT1151_BUS_NAME 
#define GT1151_BUS_NAME    "i2c1"
#endif

#ifndef GT1151_RST_PIN
#define GT1151_RST_PIN     51
#endif

#ifndef GT1151_IRQ_PIN
#define GT1151_IRQ_PIN     108
#endif

#ifndef GT1151_TOUCH_WIDTH
#define GT1151_TOUCH_WIDTH  800
#endif

#ifndef GT1151_TOUCH_HEIGHT
#define GT1151_TOUCH_HEIGHT 480
#endif

#ifndef GT1151_SUPPORT_POINTS
#define GT1151_SUPPORT_POINTS 10
#endif

#endif /* APPLICATIONS_GT1151_H_ */

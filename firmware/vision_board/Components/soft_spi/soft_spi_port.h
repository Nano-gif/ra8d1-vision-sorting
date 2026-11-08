#ifndef SOFT_SPI_PORT_H_
#define SOFT_SPI_PORT_H_

#if !defined(SOFT_SPI_GPIO_MODE_OUTPUT) || \
    !defined(SOFT_SPI_GPIO_MODE_INPUT) || \
    !defined(SOFT_SPI_GPIO_MODE_INPUT_PULLUP) || \
    !defined(SOFT_SPI_GPIO_WRITE) || \
    !defined(SOFT_SPI_GPIO_READ) || \
    !defined(SOFT_SPI_DELAY_US)
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#endif

/*
 * Default port layer is RT-Thread. To port to another MCU, replace these
 * macros with the target HAL GPIO and delay functions.
 */
#ifndef SOFT_SPI_PIN_LOW
#define SOFT_SPI_PIN_LOW                 0
#endif

#ifndef SOFT_SPI_PIN_HIGH
#define SOFT_SPI_PIN_HIGH                1
#endif

#ifndef SOFT_SPI_GPIO_MODE_OUTPUT
#define SOFT_SPI_GPIO_MODE_OUTPUT(pin)   rt_pin_mode((pin), PIN_MODE_OUTPUT)
#endif

#ifndef SOFT_SPI_GPIO_MODE_INPUT
#define SOFT_SPI_GPIO_MODE_INPUT(pin)    rt_pin_mode((pin), PIN_MODE_INPUT)
#endif

#ifndef SOFT_SPI_GPIO_MODE_INPUT_PULLUP
#define SOFT_SPI_GPIO_MODE_INPUT_PULLUP(pin) rt_pin_mode((pin), PIN_MODE_INPUT_PULLUP)
#endif

#ifndef SOFT_SPI_GPIO_WRITE
#define SOFT_SPI_GPIO_WRITE(pin, level)  rt_pin_write((pin), (level))
#endif

#ifndef SOFT_SPI_GPIO_READ
#define SOFT_SPI_GPIO_READ(pin)          rt_pin_read((pin))
#endif

#ifndef SOFT_SPI_DELAY_US
#define SOFT_SPI_DELAY_US(us)            rt_hw_us_delay((rt_uint32_t)(us))
#endif

#endif /* SOFT_SPI_PORT_H_ */

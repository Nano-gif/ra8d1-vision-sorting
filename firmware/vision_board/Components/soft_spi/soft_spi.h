#ifndef SOFT_SPI_H_
#define SOFT_SPI_H_

#include <stddef.h>
#include <stdint.h>

#include "soft_spi_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_SPI_PIN_NONE                0xFFFFFFFFUL

typedef enum
{
    SOFT_SPI_MODE_0 = 0,
    SOFT_SPI_MODE_1 = 1,
    SOFT_SPI_MODE_2 = 2,
    SOFT_SPI_MODE_3 = 3
} soft_spi_mode_t;

typedef enum
{
    SOFT_SPI_MSB_FIRST = 0,
    SOFT_SPI_LSB_FIRST = 1
} soft_spi_bit_order_t;

typedef enum
{
    SOFT_SPI_CS_ACTIVE_LOW = 0,
    SOFT_SPI_CS_ACTIVE_HIGH = 1
} soft_spi_cs_active_t;

typedef struct
{
    uint32_t sck_pin;
    uint32_t mosi_pin;
    uint32_t miso_pin;
    uint32_t cs_pin;
    uint32_t delay_us;
    soft_spi_mode_t mode;
    soft_spi_bit_order_t bit_order;
    soft_spi_cs_active_t cs_active;
    uint8_t tx_fill;
} soft_spi_bus_t;

void soft_spi_init(const soft_spi_bus_t *bus);
void soft_spi_set_mode(soft_spi_bus_t *bus, soft_spi_mode_t mode);
void soft_spi_set_bit_order(soft_spi_bus_t *bus, soft_spi_bit_order_t bit_order);
void soft_spi_set_delay_us(soft_spi_bus_t *bus, uint32_t delay_us);
void soft_spi_select(const soft_spi_bus_t *bus);
void soft_spi_deselect(const soft_spi_bus_t *bus);
uint8_t soft_spi_transfer_byte(const soft_spi_bus_t *bus, uint8_t tx);
void soft_spi_transfer(const soft_spi_bus_t *bus,
                       const uint8_t *tx,
                       uint8_t *rx,
                       size_t length,
                       uint8_t tx_fill);
void soft_spi_transfer_with_cs(const soft_spi_bus_t *bus,
                               const uint8_t *tx,
                               uint8_t *rx,
                               size_t length,
                               uint8_t tx_fill);
void soft_spi_write(const soft_spi_bus_t *bus, const uint8_t *tx, size_t length);
void soft_spi_read(const soft_spi_bus_t *bus, uint8_t *rx, size_t length);
void soft_spi_write_then_read(const soft_spi_bus_t *bus,
                              const uint8_t *tx,
                              size_t tx_length,
                              uint8_t *rx,
                              size_t rx_length);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_SPI_H_ */

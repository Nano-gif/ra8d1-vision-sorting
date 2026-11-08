#include "soft_spi.h"

static uint8_t soft_spi_cpol(const soft_spi_bus_t *bus)
{
    return (((uint8_t)bus->mode & 0x02U) != 0U) ? SOFT_SPI_PIN_HIGH : SOFT_SPI_PIN_LOW;
}

static uint8_t soft_spi_cpha(const soft_spi_bus_t *bus)
{
    return (((uint8_t)bus->mode & 0x01U) != 0U) ? 1U : 0U;
}

static uint8_t soft_spi_active_level(const soft_spi_bus_t *bus)
{
    return (soft_spi_cpol(bus) == SOFT_SPI_PIN_HIGH) ? SOFT_SPI_PIN_LOW : SOFT_SPI_PIN_HIGH;
}

static uint8_t soft_spi_cs_active_level(const soft_spi_bus_t *bus)
{
    return (bus->cs_active == SOFT_SPI_CS_ACTIVE_HIGH) ? SOFT_SPI_PIN_HIGH : SOFT_SPI_PIN_LOW;
}

static uint8_t soft_spi_cs_inactive_level(const soft_spi_bus_t *bus)
{
    return (bus->cs_active == SOFT_SPI_CS_ACTIVE_HIGH) ? SOFT_SPI_PIN_LOW : SOFT_SPI_PIN_HIGH;
}

static void soft_spi_delay(const soft_spi_bus_t *bus)
{
    if (bus->delay_us > 0U)
    {
        SOFT_SPI_DELAY_US(bus->delay_us);
    }
}

static uint8_t soft_spi_bit_mask(const soft_spi_bus_t *bus, uint8_t bit_index)
{
    return (bus->bit_order == SOFT_SPI_LSB_FIRST) ? (uint8_t)(1U << bit_index) : (uint8_t)(1U << (7U - bit_index));
}

void soft_spi_init(const soft_spi_bus_t *bus)
{
    SOFT_SPI_GPIO_MODE_OUTPUT(bus->sck_pin);
    if (bus->mosi_pin != SOFT_SPI_PIN_NONE)
    {
        SOFT_SPI_GPIO_MODE_OUTPUT(bus->mosi_pin);
        SOFT_SPI_GPIO_WRITE(bus->mosi_pin, SOFT_SPI_PIN_LOW);
    }

    if (bus->miso_pin != SOFT_SPI_PIN_NONE)
    {
        SOFT_SPI_GPIO_MODE_INPUT(bus->miso_pin);
    }

    if (bus->cs_pin != SOFT_SPI_PIN_NONE)
    {
        SOFT_SPI_GPIO_MODE_OUTPUT(bus->cs_pin);
        SOFT_SPI_GPIO_WRITE(bus->cs_pin, soft_spi_cs_inactive_level(bus));
    }

    SOFT_SPI_GPIO_WRITE(bus->sck_pin, soft_spi_cpol(bus));
}

void soft_spi_set_mode(soft_spi_bus_t *bus, soft_spi_mode_t mode)
{
    bus->mode = mode;
    SOFT_SPI_GPIO_WRITE(bus->sck_pin, soft_spi_cpol(bus));
}

void soft_spi_set_bit_order(soft_spi_bus_t *bus, soft_spi_bit_order_t bit_order)
{
    bus->bit_order = bit_order;
}

void soft_spi_set_delay_us(soft_spi_bus_t *bus, uint32_t delay_us)
{
    bus->delay_us = delay_us;
}

void soft_spi_select(const soft_spi_bus_t *bus)
{
    if (bus->cs_pin != SOFT_SPI_PIN_NONE)
    {
        SOFT_SPI_GPIO_WRITE(bus->cs_pin, soft_spi_cs_active_level(bus));
        soft_spi_delay(bus);
    }
}

void soft_spi_deselect(const soft_spi_bus_t *bus)
{
    if (bus->cs_pin != SOFT_SPI_PIN_NONE)
    {
        soft_spi_delay(bus);
        SOFT_SPI_GPIO_WRITE(bus->cs_pin, soft_spi_cs_inactive_level(bus));
    }
}

uint8_t soft_spi_transfer_byte(const soft_spi_bus_t *bus, uint8_t tx)
{
    uint8_t rx = 0U;
    uint8_t bit_index;
    uint8_t idle_level = soft_spi_cpol(bus);
    uint8_t active_level = soft_spi_active_level(bus);
    uint8_t cpha = soft_spi_cpha(bus);

    SOFT_SPI_GPIO_WRITE(bus->sck_pin, idle_level);

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        uint8_t mask = soft_spi_bit_mask(bus, bit_index);

        if (cpha == 0U)
        {
            if (bus->mosi_pin != SOFT_SPI_PIN_NONE)
            {
                SOFT_SPI_GPIO_WRITE(bus->mosi_pin, ((tx & mask) != 0U) ? SOFT_SPI_PIN_HIGH : SOFT_SPI_PIN_LOW);
            }

            soft_spi_delay(bus);
            SOFT_SPI_GPIO_WRITE(bus->sck_pin, active_level);
            soft_spi_delay(bus);

            if ((bus->miso_pin != SOFT_SPI_PIN_NONE) && (SOFT_SPI_GPIO_READ(bus->miso_pin) == SOFT_SPI_PIN_HIGH))
            {
                rx |= mask;
            }

            SOFT_SPI_GPIO_WRITE(bus->sck_pin, idle_level);
            soft_spi_delay(bus);
        }
        else
        {
            SOFT_SPI_GPIO_WRITE(bus->sck_pin, active_level);
            if (bus->mosi_pin != SOFT_SPI_PIN_NONE)
            {
                SOFT_SPI_GPIO_WRITE(bus->mosi_pin, ((tx & mask) != 0U) ? SOFT_SPI_PIN_HIGH : SOFT_SPI_PIN_LOW);
            }

            soft_spi_delay(bus);
            SOFT_SPI_GPIO_WRITE(bus->sck_pin, idle_level);
            soft_spi_delay(bus);

            if ((bus->miso_pin != SOFT_SPI_PIN_NONE) && (SOFT_SPI_GPIO_READ(bus->miso_pin) == SOFT_SPI_PIN_HIGH))
            {
                rx |= mask;
            }
        }
    }

    SOFT_SPI_GPIO_WRITE(bus->sck_pin, idle_level);

    return rx;
}

void soft_spi_transfer(const soft_spi_bus_t *bus,
                       const uint8_t *tx,
                       uint8_t *rx,
                       size_t length,
                       uint8_t tx_fill)
{
    size_t i;

    for (i = 0U; i < length; i++)
    {
        uint8_t tx_byte = (tx != NULL) ? tx[i] : tx_fill;
        uint8_t rx_byte = soft_spi_transfer_byte(bus, tx_byte);
        if (rx != NULL)
        {
            rx[i] = rx_byte;
        }
    }
}

void soft_spi_transfer_with_cs(const soft_spi_bus_t *bus,
                               const uint8_t *tx,
                               uint8_t *rx,
                               size_t length,
                               uint8_t tx_fill)
{
    soft_spi_select(bus);
    soft_spi_transfer(bus, tx, rx, length, tx_fill);
    soft_spi_deselect(bus);
}

void soft_spi_write(const soft_spi_bus_t *bus, const uint8_t *tx, size_t length)
{
    soft_spi_transfer(bus, tx, NULL, length, bus->tx_fill);
}

void soft_spi_read(const soft_spi_bus_t *bus, uint8_t *rx, size_t length)
{
    soft_spi_transfer(bus, NULL, rx, length, bus->tx_fill);
}

void soft_spi_write_then_read(const soft_spi_bus_t *bus,
                              const uint8_t *tx,
                              size_t tx_length,
                              uint8_t *rx,
                              size_t rx_length)
{
    soft_spi_select(bus);
    soft_spi_write(bus, tx, tx_length);
    soft_spi_read(bus, rx, rx_length);
    soft_spi_deselect(bus);
}

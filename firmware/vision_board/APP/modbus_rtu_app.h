#ifndef MODBUS_RTU_APP_H
#define MODBUS_RTU_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_RTU_MAX_REGISTERS        32U
#define MODBUS_RTU_DEFAULT_TIMEOUT_MS   200U
#define MODBUS_RTU_DEFAULT_RETRIES      1U

typedef struct
{
    uint8_t addr;
    uint8_t function;
    uint8_t exception_code;
    uint16_t register_count;
    uint16_t registers[MODBUS_RTU_MAX_REGISTERS];
} modbus_rtu_response_t;

int modbus_rtu_init(void);
/*
 * 停车专属关键区：begin/end 之间 W5500 SPI 收发口会短暂让路，
 * 保证软件 UART 接收停车回执时不被 SPI 完成中断抖乱。仅用于停车等关键命令。
 */
void modbus_rtu_begin_critical(void);
void modbus_rtu_end_critical(void);
int modbus_rtu_is_critical(void);
uint16_t modbus_rtu_crc16(const uint8_t *data, uint16_t len);
int modbus_rtu_read_holding_registers(uint8_t addr, uint16_t start_reg, uint16_t count,
                                      uint16_t *out_regs, uint32_t timeout_ms, uint8_t retries);
int modbus_rtu_read_input_registers(uint8_t addr, uint16_t start_reg, uint16_t count,
                                    uint16_t *out_regs, uint32_t timeout_ms, uint8_t retries);
int modbus_rtu_write_single_register(uint8_t addr, uint16_t reg, uint16_t value,
                                     uint32_t timeout_ms, uint8_t retries);
int modbus_rtu_write_multiple_registers(uint8_t addr, uint16_t start_reg,
                                        const uint16_t *values, uint16_t count,
                                        uint32_t timeout_ms, uint8_t retries);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_APP_H */

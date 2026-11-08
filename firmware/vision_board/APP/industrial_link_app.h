#ifndef INDUSTRIAL_LINK_APP_H
#define INDUSTRIAL_LINK_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INDUSTRIAL_LINK_MAGIC          0xA15AU
#define INDUSTRIAL_LINK_PROTOCOL_VER   0x0001U

typedef enum
{
    INDUSTRIAL_LINK_REG_MAGIC = 0x0000,
    INDUSTRIAL_LINK_REG_RUN_STATE = 0x0001,
    INDUSTRIAL_LINK_REG_STAGE = 0x0002,
    INDUSTRIAL_LINK_REG_LATEST_RESULT = 0x0003,
    INDUSTRIAL_LINK_REG_SCORE_X10 = 0x0004,
    INDUSTRIAL_LINK_REG_THRESHOLD_X10 = 0x0005,
    INDUSTRIAL_LINK_REG_TOTAL_COUNT_LO = 0x0006,
    INDUSTRIAL_LINK_REG_TOTAL_COUNT_HI = 0x0007,
    INDUSTRIAL_LINK_REG_OK_COUNT_LO = 0x0008,
    INDUSTRIAL_LINK_REG_OK_COUNT_HI = 0x0009,
    INDUSTRIAL_LINK_REG_NG_COUNT_LO = 0x000A,
    INDUSTRIAL_LINK_REG_NG_COUNT_HI = 0x000B,
    INDUSTRIAL_LINK_REG_AI_TIME_MS = 0x000C,
    INDUSTRIAL_LINK_REG_CAPTURE_MS = 0x000D,
    INDUSTRIAL_LINK_REG_MODEL_INPUT_W = 0x000E,
    INDUSTRIAL_LINK_REG_MODEL_INPUT_H = 0x000F,
    INDUSTRIAL_LINK_REG_GRID_W = 0x0010,
    INDUSTRIAL_LINK_REG_GRID_H = 0x0011,
    INDUSTRIAL_LINK_REG_COMMAND = 0x0012,
    INDUSTRIAL_LINK_REG_VERSION = 0x0013,
    INDUSTRIAL_LINK_REG_COUNT
} industrial_link_reg_t;

typedef enum
{
    INDUSTRIAL_LINK_CMD_NONE = 0,
    INDUSTRIAL_LINK_CMD_START = 1,
    INDUSTRIAL_LINK_CMD_STOP = 2,
    INDUSTRIAL_LINK_CMD_ONCE = 3,
} industrial_link_command_t;

int industrial_link_read_holding(uint16_t address, uint16_t *out_value);
int industrial_link_read_holding_block(uint16_t start_address,
                                       uint16_t count,
                                       uint16_t *out_values);
int industrial_link_write_holding(uint16_t address, uint16_t value);
uint16_t industrial_link_get_holding_count(void);

#ifdef __cplusplus
}
#endif

#endif /* INDUSTRIAL_LINK_APP_H */

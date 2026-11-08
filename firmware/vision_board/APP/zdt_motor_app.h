#ifndef ZDT_MOTOR_APP_H
#define ZDT_MOTOR_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZDT_MOTOR_DEFAULT_ADDR          2U

typedef enum
{
    ZDT_MOTOR_DIR_CW = 0,
    ZDT_MOTOR_DIR_CCW = 1,
} zdt_motor_dir_t;

typedef struct
{
    int16_t speed_rpm;
    int32_t position_raw;
    uint8_t motor_flags;
    uint8_t home_flags;
} zdt_motor_status_t;

int zdt_motor_stop(uint8_t addr);
int zdt_motor_stop_fast(uint8_t addr, uint8_t repeats);
int zdt_motor_speed(uint8_t addr, zdt_motor_dir_t dir, uint16_t rpm, uint8_t acc, uint8_t sync);
int zdt_motor_position(uint8_t addr, zdt_motor_dir_t dir, uint16_t rpm, uint8_t acc,
                       uint32_t pulses, uint8_t mode, uint8_t sync);
int zdt_motor_read_speed(uint8_t addr, zdt_motor_status_t *status);
int zdt_motor_read_position(uint8_t addr, zdt_motor_status_t *status);
int zdt_motor_read_flags(uint8_t addr, zdt_motor_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ZDT_MOTOR_APP_H */

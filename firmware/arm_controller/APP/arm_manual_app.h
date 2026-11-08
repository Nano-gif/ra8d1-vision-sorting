#ifndef ARM_MANUAL_APP_H_
#define ARM_MANUAL_APP_H_

#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

#define ARM_MANUAL_AXIS_COUNT       (6U)
#define ARM_MANUAL_AXIS_X           (0U)
#define ARM_MANUAL_AXIS_Y           (1U)
#define ARM_MANUAL_AXIS_Z           (2U)
#define ARM_MANUAL_AXIS_PITCH       (3U)
#define ARM_MANUAL_AXIS_ROLL        (4U)
#define ARM_MANUAL_AXIS_GRIP        (5U)
#define ARM_MANUAL_AXIS_ALL         (0xFFU)
#define ARM_MANUAL_MODE_CONTINUOUS  (0U)
#define ARM_MANUAL_MODE_STEP        (1U)
#define ARM_MANUAL_COORD_BASE       (0U)
#define ARM_MANUAL_COORD_TOOL       (1U)
#define ARM_MANUAL_COORD_WORKPIECE  (2U)
#define ARM_MANUAL_COORD_CAMERA     (3U)
#define ARM_MANUAL_DIR_STOP         (0U)
#define ARM_MANUAL_DIR_NEGATIVE     (1U)
#define ARM_MANUAL_DIR_POSITIVE     (2U)

typedef struct st_arm_manual_info
{
    int32_t value[ARM_MANUAL_AXIS_COUNT];
    uint8_t active_axis;
    uint8_t direction;
    uint8_t mode;
    uint8_t coord_mode;
    uint8_t speed;
    uint8_t soft;
    uint8_t step;
    bool page_active;
    bool moving;
} arm_manual_info_t;

fsp_err_t arm_manual_init(void);
fsp_err_t arm_manual_set_page_active(bool active);
fsp_err_t arm_manual_set_params(uint8_t mode, uint8_t speed, uint8_t soft, uint8_t step);
fsp_err_t arm_manual_set_coord_mode(uint8_t coord_mode);
fsp_err_t arm_manual_sync_from_control(void);
fsp_err_t arm_manual_jog_command(uint8_t axis,
                                 uint8_t direction,
                                 uint8_t mode,
                                 uint8_t speed,
                                 uint8_t soft,
                                 uint8_t step);
bool arm_manual_is_page_active(void);
void arm_manual_task(void);
void arm_manual_get_info(arm_manual_info_t *p_info);

#endif

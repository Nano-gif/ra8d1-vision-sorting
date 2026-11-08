#ifndef BUZZER_APP_H_
#define BUZZER_APP_H_

#include "hal_data.h"
#include <stdint.h>

#define BUZZER_DEFAULT_BEEP_MS (200U)

fsp_err_t buzzer_init(void);
void buzzer_beep(uint16_t duration_ms);
void buzzer_task(void);

#endif

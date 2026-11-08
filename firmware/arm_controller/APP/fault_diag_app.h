#ifndef FAULT_DIAG_APP_H_
#define FAULT_DIAG_APP_H_

#include "hal_data.h"

#define FAULT_DIAG_MAGIC (0xFA061BADUL)

typedef enum e_fault_diag_type
{
    FAULT_DIAG_TYPE_NONE = 0,
    FAULT_DIAG_TYPE_HARDFAULT = 1,
    FAULT_DIAG_TYPE_MEMMANAGE = 2,
    FAULT_DIAG_TYPE_BUSFAULT = 3,
    FAULT_DIAG_TYPE_USAGEFAULT = 4,
} fault_diag_type_t;

typedef struct st_fault_diag_info
{
    volatile uint32_t magic;
    volatile uint32_t type;
    volatile uint32_t ipsr;
    volatile uint32_t control;
    volatile uint32_t msp;
    volatile uint32_t psp;
    volatile uint32_t lr;
    volatile uint32_t cfsr;
    volatile uint32_t hfsr;
    volatile uint32_t dfsr;
    volatile uint32_t afsr;
    volatile uint32_t mmfar;
    volatile uint32_t bfar;
} fault_diag_info_t;

extern volatile fault_diag_info_t g_fault_diag;

void fault_diag_clear(void);

#endif

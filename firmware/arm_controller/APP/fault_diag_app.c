#include "fault_diag_app.h"

volatile fault_diag_info_t g_fault_diag;

void fault_diag_clear(void)
{
    g_fault_diag.magic = 0U;
    g_fault_diag.type = FAULT_DIAG_TYPE_NONE;
    g_fault_diag.ipsr = 0U;
    g_fault_diag.control = 0U;
    g_fault_diag.msp = 0U;
    g_fault_diag.psp = 0U;
    g_fault_diag.lr = 0U;
    g_fault_diag.cfsr = 0U;
    g_fault_diag.hfsr = 0U;
    g_fault_diag.dfsr = 0U;
    g_fault_diag.afsr = 0U;
    g_fault_diag.mmfar = 0U;
    g_fault_diag.bfar = 0U;
}

static void fault_diag_capture(fault_diag_type_t type, uint32_t lr_value)
{
    g_fault_diag.magic = FAULT_DIAG_MAGIC;
    g_fault_diag.type = (uint32_t)type;
    g_fault_diag.ipsr = __get_IPSR();
    g_fault_diag.control = __get_CONTROL();
    g_fault_diag.msp = __get_MSP();
    g_fault_diag.psp = __get_PSP();
    g_fault_diag.lr = lr_value;
    g_fault_diag.cfsr = SCB->CFSR;
    g_fault_diag.hfsr = SCB->HFSR;
    g_fault_diag.dfsr = SCB->DFSR;
    g_fault_diag.afsr = SCB->AFSR;
    g_fault_diag.mmfar = SCB->MMFAR;
    g_fault_diag.bfar = SCB->BFAR;
}

static void fault_diag_stop(fault_diag_type_t type, uint32_t lr_value)
{
    fault_diag_capture(type, lr_value);

    __disable_irq();
    __BKPT(0);

    while (1)
    {
        __NOP();
    }
}

void HardFault_Handler(void)
{
    uint32_t lr_value;

    __ASM volatile ("MOV %0, lr" : "=r" (lr_value));
    fault_diag_stop(FAULT_DIAG_TYPE_HARDFAULT, lr_value);
}

void MemManage_Handler(void)
{
    uint32_t lr_value;

    __ASM volatile ("MOV %0, lr" : "=r" (lr_value));
    fault_diag_stop(FAULT_DIAG_TYPE_MEMMANAGE, lr_value);
}

void BusFault_Handler(void)
{
    uint32_t lr_value;

    __ASM volatile ("MOV %0, lr" : "=r" (lr_value));
    fault_diag_stop(FAULT_DIAG_TYPE_BUSFAULT, lr_value);
}

void UsageFault_Handler(void)
{
    uint32_t lr_value;

    __ASM volatile ("MOV %0, lr" : "=r" (lr_value));
    fault_diag_stop(FAULT_DIAG_TYPE_USAGEFAULT, lr_value);
}

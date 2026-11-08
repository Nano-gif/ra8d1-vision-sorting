/*
 * bsp_systick.h
 *
 *  Created on: 2026年3月30日
 *      Author: Water21
 */

#ifndef BSP_SYSTICK_H_
#define BSP_SYSTICK_H_

#include "mydefine.h"

void systick_init_us(uint32_t period_us);
void SysTick_Handler(void);
#endif /* BSP_SYSTICK_H_ */

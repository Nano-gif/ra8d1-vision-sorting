/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2023-12-17     Rbb666        first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "hal_data.h"

#define LED_PIN    BSP_IO_PORT_01_PIN_02 /* Onboard LED pins */

#define SDRAM_TEST_SIZE (1024 * 1024)
static rt_uint8_t sdram_test_buf[SDRAM_TEST_SIZE]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

static rt_err_t sdram_test(void)
{
    rt_size_t i;

    rt_kprintf("\n[SDRAM] test start\r\n");
    rt_kprintf("[SDRAM] buffer addr = 0x%08X, size = %d bytes\r\n",
               (unsigned int)sdram_test_buf, SDRAM_TEST_SIZE);

    //写入
    for(i=0;i<SDRAM_TEST_SIZE;i++)
    {
        sdram_test_buf[i] = (rt_uint8_t)(i & 0xff);//让这块test的内容在0~255间循环
    }
    //读取 并校验数据
    for(i=0;i<SDRAM_TEST_SIZE;i++)
    {
        rt_uint8_t expect = (rt_uint8_t)(i & 0xFF);
                 if (sdram_test_buf[i] != expect)
                 {
                     rt_kprintf("[SDRAM] test fail at %d, expect = 0x%02X, actual = 0x%02X\r\n",
                             i, expect, sdram_test_buf[i]);
                     return -RT_ERROR;
                 }
    }
    rt_kprintf("[SDRAM] test pass\r\n");
    return RT_EOK;
}


void hal_entry(void)
{
    rt_kprintf("\nHello RT-Thread!\n");
    if(sdram_test()!=RT_EOK)
        rt_kprintf("[SDRAM] verification failed\r\n");

    while (1)
    {
        rt_pin_write(LED_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}

/***********************************************************************************************************************
 * Copyright [2020-2023] Renesas Electronics Corporation and/or its affiliates.  All Rights Reserved.
 *
 * This software and documentation are supplied by Renesas Electronics America Inc. and may only be used with products
 * of Renesas Electronics Corp. and its affiliates ("Renesas").  No other uses are authorized.  Renesas products are
 * sold pursuant to Renesas terms and conditions of sale.  Purchasers are solely responsible for the selection and use
 * of Renesas products and Renesas assumes no liability.  No license, express or implied, to any intellectual property
 * right is granted by Renesas. This software is protected under all applicable laws, including copyright laws. Renesas
 * reserves the right to change or discontinue this software and/or this documentation. THE SOFTWARE AND DOCUMENTATION
 * IS DELIVERED TO YOU "AS IS," AND RENESAS MAKES NO REPRESENTATIONS OR WARRANTIES, AND TO THE FULLEST EXTENT
 * PERMISSIBLE UNDER APPLICABLE LAW, DISCLAIMS ALL WARRANTIES, WHETHER EXPLICITLY OR IMPLICITLY, INCLUDING WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT, WITH RESPECT TO THE SOFTWARE OR
 * DOCUMENTATION.  RENESAS SHALL HAVE NO LIABILITY ARISING OUT OF ANY SECURITY VULNERABILITY OR BREACH.  TO THE MAXIMUM
 * EXTENT PERMITTED BY LAW, IN NO EVENT WILL RENESAS BE LIABLE TO YOU IN CONNECTION WITH THE SOFTWARE OR DOCUMENTATION
 * (OR ANY PERSON OR ENTITY CLAIMING RIGHTS DERIVED FROM YOU) FOR ANY LOSS, DAMAGES, OR CLAIMS WHATSOEVER, INCLUDING,
 * WITHOUT LIMITATION, ANY DIRECT, CONSEQUENTIAL, SPECIAL, INDIRECT, PUNITIVE, OR INCIDENTAL DAMAGES; ANY LOST PROFITS,
 * OTHER ECONOMIC DAMAGE, PROPERTY DAMAGE, OR PERSONAL INJURY; AND EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH LOSS, DAMAGES, CLAIMS OR COSTS.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * 头文件包含：<系统头文件>，"工程头文件"
 **********************************************************************************************************************/

#include "r_ioport.h"
#include "bsp_cfg.h"
#include "bsp_pin_cfg.h"

#include <rtthread.h>

#define DRV_DEBUG
#define LOG_TAG         "drv_sdram"
#include <drv_log.h>

/*
 * 一行被激活后，至少要保持多久
 * 设置 ACTIVE 到 PRECHARGE 命令之间的时序（tRAS）
 * 例如：tRAS = 42ns -> 在 SDCLK 120MHz 下需要 6 个周期
 *      tRAS = 37ns -> 在 SDCLK 120MHz 下需要 5 个周期
 */
#define BSP_PRV_SDRAM_TRAS                     (6U)

/*
 * 开行之后，多久后才能真正读写
 * 设置 ACTIVE 到 READ/WRITE 的延时（tRCD）
 * 例如：tRCD = 18ns -> 在 SDCLK 120MHz 下需要 3 个周期
 *      tRCD = 15ns -> 在 SDCLK 120MHz 下需要 2 个周期
 */
#define BSP_PRV_SDRAM_TRCD                     (3U)

/*
 * 关完一行后，多久后才能开始下一轮操作
 * 设置 PRECHARGE 命令持续时间（tRP）
 * 例如：tRP  = 18ns -> 在 SDCLK 120MHz 下需要 3 个周期
 *      tRP  = 15ns -> 在 SDCLK 120MHz 下需要 2 个周期
 */
#define BSP_PRV_SDRAM_TRP                      (3U)

/*
 * 写完数据后，不能立刻收工
 * 设置写恢复时间（tWR）
 * 例如：tWR  = 1CLK + 6ns -> 在 SDCLK 120MHz 下需要 2 个周期
 *      tWR  = 1CLK + 7ns -> 在 SDCLK 120MHz 下需要 2 个周期
 */
#define BSP_PRV_SDRAM_TWR                      (2U)

/*
 * 设置 CAS（读）延时（CL）
 * 例如：CL = 18ns -> 在 SDCLK 120MHz 下需要 3 个周期
 *      CL = 15ns -> 在 SDCLK 120MHz 下需要 2 个周期
 */
#define BSP_PRV_SDRAM_CL                       (3U)

/*
 * 表示一次 Auto Refresh 相关等待时间按 8 个周期处理---->每次刷新所需要耗费的时间
 * 设置自动刷新持续时间（tRFC）
 * 例如：tRFC = 60nS -> 在 SDCLK 120MHz 下需要 8 个周期
 *      tRFC = 66nS -> 在 SDCLK 120MHz 下需要 8 个周期
 */
#define BSP_PRV_SDRAM_TRFC                     (8U)

/*
 *
 * 设置平均刷新周期                          -------------------->两次刷新命令之间平均隔多久
 * 例如：tREF = 64ms/8192 行 -> 每行约 7.8125us，在 SDCLK 120MHz 下需要 937 个周期
 */
#define BSP_PRV_SDRAM_REF_CMD_INTERVAL         (937U)

/*
 * 设置初始化序列中自动刷新的触发次数
 * 典型 SDR SDRAM 设备通常需要执行 2 次 Auto-Refresh
 */
#define BSP_PRV_SDRAM_SDIR_REF_TIMES           (2U)

/*
 * 设置行地址偏移
 * 可选设置：
 * 8  : 8 位
 * 9  : 9 位
 * 10 : 10 位
 * 11 : 11 位
 */
#define BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET    (9U)

/*
 * 选择 SDRAM 地址空间的大小端模式
 * 0 : SDRAM 地址空间与当前运行模式的大小端一致
 * 1 : SDRAM 地址空间与当前运行模式的大小端不一致
 */
#define BSP_PRV_SDRAM_ENDIAN_MODE              (0U)

/*
 * 选择访问模式
 * 一般建议开启连续访问模式，以获得更高的 SDRAM 带宽
 * 0: 禁用连续访问
 * 1: 启用连续访问
 */
#define BSP_PRV_SDRAM_CONTINUOUS_ACCESSMODE    (1U)

/*
 * 选择总线宽度
 * 0: 16 位
 * 1: 32 位
 * 2: 8 位
 */
#define BSP_PRV_SDRAM_BUS_WIDTH                (0U)

#if ((BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET != 8U) && (BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET != 9U) \
    && (BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET != 10U) && (BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET > 11U))
#error "BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET must be either of 8,9,10 or 11"
#endif

#if ((BSP_PRV_SDRAM_BUS_WIDTH != 0) && (BSP_PRV_SDRAM_BUS_WIDTH != 1U) && (BSP_PRV_SDRAM_BUS_WIDTH != 2U))
    #error "BSP_PRV_SDRAM_BUS_WIDTH must be either of 0(16-bit) or 1(32-bit) or 2(8-bit)"
#endif

#if ((BSP_PRV_SDRAM_ENDIAN_MODE != 0) && (BSP_PRV_SDRAM_ENDIAN_MODE != 1))
    #error \
    "BSP_PRV_SDRAM_ENDIAN_MODE must be either of 0(same endian as operating mode) or 2(another endian against operating mode)"
#endif

#if ((BSP_PRV_SDRAM_CONTINUOUS_ACCESSMODE != 0) && (BSP_PRV_SDRAM_CONTINUOUS_ACCESSMODE != 1))
    #error \
    "BSP_PRV_SDRAM_CONTINUOUS_ACCESSMODE must be either of 0(continuous access is disabled) or 1(continuous access is enabled)"
#endif

#define BSP_PRV_SDRAM_MR_WB_SINGLE_LOC_ACC    (1U) /* MR.M9                : 单次位置访问 */
#define BSP_PRV_SDRAM_MR_OP_MODE              (0U) /* MR.M8:M7             : 标准工作模式 */
#define BSP_PRV_SDRAM_MR_BT_SEQUENCTIAL       (0U) /* MR.M3 Burst Type     : 顺序突发 */
#define BSP_PRV_SDRAM_MR_BURST_LENGTH         (0U) /* MR.M2:M0 Burst Length: 0（突发长度为 1） */

/***********************************************************************************************************************
 * 类型定义
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * 导出的全局变量（供其他文件访问）
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * 私有全局变量和函数
 **********************************************************************************************************************/

void drv_sdram_init(void)
{
    /** 配置 SDRAM 初始化序列相关参数 */
#if (BSP_PRV_SDRAM_TRP < 3)
    R_BUS->SDRAM.SDIR_b.PRC = 3U;
#else
    R_BUS->SDRAM.SDIR_b.PRC = BSP_PRV_SDRAM_TRP - 3U;
#endif

    while (R_BUS->SDRAM.SDSR)
    {
        /* 根据硬件手册，在修改 SDIR 前，需要确认 SDSR 中所有状态位都为 0。 */
    }

    R_BUS->SDRAM.SDIR_b.ARFC = BSP_PRV_SDRAM_SDIR_REF_TIMES;  //配置自动刷新次数

    while (R_BUS->SDRAM.SDSR)
    {
        /* 根据硬件手册，在修改 SDIR 前，需要确认 SDSR 中所有状态位都为 0。 */
    }

#if (BSP_PRV_SDRAM_TRFC < 3)
    R_BUS->SDRAM.SDIR_b.ARFI = 0U;
#else
    R_BUS->SDRAM.SDIR_b.ARFI = BSP_PRV_SDRAM_TRFC - 3U;     //配置每次刷新需要的时间
#endif

    while (R_BUS->SDRAM.SDSR)
    {
        /* 根据硬件手册，在修改 SDICR 前，需要确认 SDSR 中所有状态位都为 0。 */
    }

    /** 启动 SDRAM 初始化序列。  这一步才是真正开始执行初始化操作
     * 当设置 SDICR.INIRQ 位后，控制器会自动完成以下动作：
     * 执行 PRECHARGE ALL 命令，并至少等待 tRP 时间。
     * 执行一次 AUTO REFRESH 命令，并至少等待 tRFC 时间。
     * 再执行一次 AUTO REFRESH 命令，并至少等待 tRFC 时间。
     */
    R_BUS->SDRAM.SDICR_b.INIRQ = 1U;
    while (R_BUS->SDRAM.SDSR_b.INIST)
    {
        /* 等待初始化序列结束。 */
    }

    /** 配置 SDRAM 控制器 */
    R_BUS->SDRAM.SDCCR_b.BSIZE  = BSP_PRV_SDRAM_BUS_WIDTH;             /* 设置 SDRAM 总线宽度 */
    R_BUS->SDRAM.SDAMOD_b.BE    = BSP_PRV_SDRAM_CONTINUOUS_ACCESSMODE; /* 启用连续访问模式 */
    R_BUS->SDRAM.SDCMOD_b.EMODE = BSP_PRV_SDRAM_ENDIAN_MODE;           /* 设置 SDRAM 地址空间大小端模式 */

    while (R_BUS->SDRAM.SDSR)
    {
        /* 根据硬件手册，在修改 SDMOD 前，需要确认 SDSR 中所有状态位都为 0。 */
    }

    /** 使用 LMR 命令写入模式寄存器 */
    R_BUS->SDRAM.SDMOD = ((((uint16_t)(BSP_PRV_SDRAM_MR_WB_SINGLE_LOC_ACC << 9) |
                            (uint16_t)(BSP_PRV_SDRAM_MR_OP_MODE << 7)) |
                           (uint16_t)(BSP_PRV_SDRAM_CL << 4)) |
                          (uint16_t)(BSP_PRV_SDRAM_MR_BT_SEQUENCTIAL << 3)) |
                         (uint16_t)(BSP_PRV_SDRAM_MR_BURST_LENGTH << 0);

    /** 至少等待 tMRD 时间 */
    while (R_BUS->SDRAM.SDSR_b.MRSST)
    {
        /* 等待模式寄存器设置完成。 */
    }

    /** 设置 SDRAM 时序参数 */
    R_BUS->SDRAM.SDTR_b.RAS = BSP_PRV_SDRAM_TRAS - 1U; /* 设置 ACTIVE 到 PRECHARGE 的周期数 */
    R_BUS->SDRAM.SDTR_b.RCD = BSP_PRV_SDRAM_TRCD - 1U; /* 设置 ACTIVE 到 READ/WRITE 的延时周期 */
    R_BUS->SDRAM.SDTR_b.RP  = BSP_PRV_SDRAM_TRP - 1U;  /* 设置 PRECHARGE 命令周期数 */
    R_BUS->SDRAM.SDTR_b.WR  = BSP_PRV_SDRAM_TWR - 1U;  /* 设置写恢复周期 */
    R_BUS->SDRAM.SDTR_b.CL  = BSP_PRV_SDRAM_CL;        /* 设置 SDRAM 列访问延时周期 */

    /** 设置目标 SDRAM 的行地址偏移 */
    R_BUS->SDRAM.SDADR_b.MXC = BSP_PRV_SDRAM_SDADR_ROW_ADDR_OFFSET - 8U;

    R_BUS->SDRAM.SDRFCR_b.REFW = (uint16_t)(BSP_PRV_SDRAM_TRFC - 1U);  /* 设置 Auto-Refresh 持续周期 */
    R_BUS->SDRAM.SDRFCR_b.RFC  = BSP_PRV_SDRAM_REF_CMD_INTERVAL - 1U;  /* 设置 Auto-Refresh 周期 */

    /** 启动自动刷新 */
    R_BUS->SDRAM.SDRFEN_b.RFEN = 1U;

    /** 使能 SDRAM 访问 */
    R_BUS->SDRAM.SDCCR_b.EXENB = 1U;
}

#ifdef BSP_USING_SDRAM
#ifdef RT_USING_MEMHEAP_AS_HEAP
    struct rt_memheap system_heap;
#endif

static int SDRAM_Init(void)
{
    drv_sdram_init();

    LOG_D("sdram init success, mapped at 0x%X, size is %d bytes, data width is %d", 0x68000000, BSP_USING_SDRAM_SIZE, 16);
#ifdef RT_USING_MEMHEAP_AS_HEAP
    /* 如果启用了 RT_USING_MEMHEAP_AS_HEAP，则将 SDRAM 初始化为系统堆 */
    //暂时用不到动态分配
//    rt_memheap_init(&system_heap, "sdram", (void *)0x68000000, BSP_USING_SDRAM_SIZE);
#endif
    return RT_EOK;
}
INIT_BOARD_EXPORT(SDRAM_Init);
#endif

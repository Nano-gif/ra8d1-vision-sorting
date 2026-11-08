/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2024-04-06     Rbb666        first version
 */
#include <string.h>
#include <stdint.h>

#include "sensor.h"
#include "hal_data.h"

#define DRV_DEBUG
#define LOG_TAG             "sensor"
#include <drv_log.h>

#define CEU_EVENT_FRAME_END_TIMEOUT 80

sensor_t sensor = {0};
capture_instance_t *gp_ceu_instance = NULL;

static struct rt_completion ceu_completion;

static int ceu_device_open(void);

int sensor_init(void)
{
    int init_ret = 0;

    /* 先把全局 sensor 对象清零，避免沿用上一次的状态。 */
    memset(&sensor, 0, sizeof(sensor_t));

    /* 给 sensor 挂上默认 snapshot 实现。后面应用层抓图会走这里。 */
    sensor.snapshot = sensor_snapshot;

    /* 先启动摄像头外部时钟 XCLK。很多 sensor 没有时钟时不会响应总线探测。 */
    if (sensor_set_xclk_frequency(OMV_XCLK_FREQUENCY) != 0)
    {
        /* 外部时钟没起来，后续探测和初始化都会失败。 */
        return SENSOR_ERROR_TIM_INIT_FAILED;
    }

    /* 通过 SCCB/I2C 去探测真正接上的 sensor 型号，并绑定对应驱动。 */
    if ((init_ret = sensor_probe_init(ISC_I2C_ID, ISC_I2C_SPEED)) != 0)
    {
        LOG_E("not found any sensors !!");
        return init_ret;
    }

    /* 到这里表示 sensor 已经被识别并初始化成功。 */
    sensor.pwdn_pol = ACTIVE_HIGH;

    /* 当前例程不使用 VSYNC 回调，先关掉。 */
    sensor_set_vsync_callback(NULL);

    /* 标记当前 sensor 已经可用。 */
    sensor.detected = true;

    /* CEU 采图完成后，应用层会靠这个 completion 等待一帧结束。 */
    rt_completion_init(&ceu_completion);

    return 0;
}

int sensor_set_xclk_frequency(uint32_t frequency)
{
    static uint8_t is_enable = 0;
    if (is_enable)
        return 0;

    R_GPT_Open(&g_timer7_ctrl, &g_timer7_cfg);
    R_GPT_Enable(&g_timer7_ctrl);
    R_GPT_Start(&g_timer7_ctrl);
    LOG_D("Enable 24MHz pwm for camera");

    is_enable = 1;

    return 0;
}

int sensor_set_vsync_callback(vsync_cb_t vsync_cb)
{
    return 0;
}

int sensor_dcmi_config(uint32_t pixformat)
{
    return 0;
}

int sensor_abort(void)
{
    return 0;
}

static int ceu_device_open(void)
{
    capture_status_t ceu_state;
    /* 先看 CEU 当前是不是空闲态，避免重复打开。 */
    R_CEU_StatusGet(gp_ceu_instance->p_ctrl, (capture_status_t *)&ceu_state);
    if (ceu_state.state == CAPTURE_STATE_IDLE)
    {
        /* 打开 CEU，让 MCU 侧准备好接收摄像头像素流。 */
        fsp_err_t err = R_CEU_Open(gp_ceu_instance->p_ctrl, gp_ceu_instance->p_cfg);
        if (err != FSP_SUCCESS)
        {
            LOG_E("R_CEU_Open API FAILED:%d", err);
            return -1;
        }
    }

    LOG_I("Open CEU device");
    return 0;
}

int sensor_set_framesize(framesize_t framesize)
{
    if (sensor.framesize == framesize)
    {
        return 0;
    }

    switch (framesize)
    {
    case FRAMESIZE_QVGA: /* 320x240 */
        /* 当前例程只演示 QVGA，因此这里选择 QVGA 对应的 CEU 捕获配置。 */
        gp_ceu_instance = (capture_instance_t *)&g_ceu_qvga;
        break;
    default:
        break;
    }
    LOG_I("set framesize:%dX%d", gp_ceu_instance->p_cfg->x_capture_pixels, gp_ceu_instance->p_cfg->y_capture_pixels);

    /* 先把 MCU 侧的 CEU 打开并按目标分辨率准备好。 */
    ceu_device_open();

    /* 改分辨率前先停掉可能还在进行的采图。 */
    sensor_abort();

    /* 再调用 sensor 专属驱动（例如 ov5640.c）修改摄像头本体输出尺寸。 */
    if (sensor.set_framesize == NULL)
    {
        return SENSOR_ERROR_CTL_UNSUPPORTED;
    }

    if (sensor.set_framesize(&sensor, framesize) != 0)
    {
        return SENSOR_ERROR_CTL_FAILED;
    }

    /* 分辨率切换后给摄像头一点稳定时间。 */
    rt_thread_mdelay(100);

    /* 软件侧记录当前已经生效的分辨率。 */
    sensor.framesize = framesize;

    return 0;
}

void g_ceu_callback(capture_callback_args_t *p_args)
{
    rt_interrupt_enter();

    if (CEU_EVENT_HD == p_args->event)
    {
        /* 一帧采集开始（V-Sync），当前无需处理。 */
    }
    if (CEU_EVENT_FRAME_END == p_args->event)
    {
        if (sensor.frame_callback)
        {
            sensor.frame_callback();
        }
        rt_completion_done(&ceu_completion);
    }

    rt_interrupt_leave();
}

int write_raw_yuyv_to_gray(sensor_t *sensor, uint8_t *out, const uint8_t *in, size_t len)
{
    /* YUV 转灰度：每 2 字节取 Y 分量。 */
    if (sensor->hw_flags.gs_bpp == 2)
    {
        size_t end = len / 8;
        for (size_t i = 0; i < end; ++i)
        {
            out[0] = in[0];
            out[1] = in[2];
            out[2] = in[4];
            out[3] = in[6];
            out += 4;
            in += 8;
        }
        return len / 2;
    }

    /* 其余情况直接拷贝。 */
    memcpy(out, in, len);
    return len;
}

/* 默认抓帧实现，具体 sensor 驱动可在初始化时替换。 */
int sensor_snapshot(sensor_t *sensor, uint8_t *image, uint32_t flags)
{
    if (sensor->pixformat == PIXFORMAT_INVALID)
    {
        return SENSOR_ERROR_INVALID_PIXFORMAT;
    }

    if (sensor->framesize == FRAMESIZE_INVALID)
    {
        return SENSOR_ERROR_INVALID_FRAMESIZE;
    }

    /* 通知 CEU 开始抓一帧，并把这一帧写进 image 指向的缓冲区。 */
    fsp_err_t err = R_CEU_CaptureStart(gp_ceu_instance->p_ctrl, image);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_CEU_CaptureStart API FAILED:%d", err);
        return -1;
    }

    /* 阻塞等待 CEU 回调报告“本帧采集结束”。 */
    if (rt_completion_wait(&ceu_completion, CEU_EVENT_FRAME_END_TIMEOUT) != RT_EOK)
    {
        /* 超时：关闭 CEU 以终止本次采集，再重新打开，保证下一帧可以正常启动。 */
        LOG_E("CEU frame end timeout");
        R_CEU_Close(gp_ceu_instance->p_ctrl);
        rt_completion_init(&ceu_completion);
        err = R_CEU_Open(gp_ceu_instance->p_ctrl, gp_ceu_instance->p_cfg);
        if (err != FSP_SUCCESS)
        {
            LOG_E("R_CEU_Open API FAILED:%d", err);
        }
        return -RT_ETIMEOUT;
    }

    return 0;
}

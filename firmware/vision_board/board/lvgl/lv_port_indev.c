/*
 * LVGL touch input port for RT-Thread Vision Board.
 *
 * Data flow:
 * GT1151 touch IC -> RT-Thread touch device "gt1151" -> LVGL pointer input.
 */

#include <lvgl.h>
#include <rtconfig.h>
#include <rtdevice.h>

#if defined(PKG_USING_GT1151)
#include <drivers/touch.h>

#define TOUCH_DEVICE_NAME "gt1151"

static rt_device_t touch_dev = RT_NULL;
static struct rt_touch_data touch_data;
static lv_point_t last_point;

/*
 * LVGL calls this function periodically from its own thread.
 * We read one touch point from RT-Thread's touch device and translate it into
 * LVGL's pointer state: pressed or released.
 */
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    rt_size_t read_num;

    (void)indev;

    if (touch_dev == RT_NULL)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = last_point;
        return;
    }

    rt_memset(&touch_data, 0, sizeof(touch_data));
    read_num = rt_device_read(touch_dev, 0, &touch_data, 1);
    if (read_num == 0)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = last_point;
        return;
    }

    last_point.x = touch_data.x_coordinate;
    last_point.y = touch_data.y_coordinate;
    data->point = last_point;

    if ((touch_data.event == RT_TOUCH_EVENT_DOWN) ||
        (touch_data.event == RT_TOUCH_EVENT_MOVE))
    {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static rt_err_t touchpad_init(void)
{
    rt_uint8_t id[4] = {0};
    struct rt_touch_info info = {0};

    touch_dev = rt_device_find(TOUCH_DEVICE_NAME);
    if (touch_dev == RT_NULL)
    {
        rt_kprintf("LVGL touch: can't find device %s\n", TOUCH_DEVICE_NAME);
        return -RT_ERROR;
    }

    if (rt_device_open(touch_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        rt_kprintf("LVGL touch: open %s failed\n", TOUCH_DEVICE_NAME);
        touch_dev = RT_NULL;
        return -RT_ERROR;
    }

    rt_device_control(touch_dev, RT_TOUCH_CTRL_GET_ID, id);
    rt_device_control(touch_dev, RT_TOUCH_CTRL_GET_INFO, &info);

    rt_kprintf("LVGL touch: %s ready, id=%c%c%c%c, range=%d*%d, points=%d\n",
               TOUCH_DEVICE_NAME,
               id[0], id[1], id[2], id[3],
               info.range_x, info.range_y, info.point_num);

    return RT_EOK;
}
#endif

void lv_port_indev_init(void)
{
#if defined(PKG_USING_GT1151)
    lv_indev_t *indev_touchpad;

    if (touchpad_init() != RT_EOK)
    {
        rt_kprintf("LVGL touch input disabled: GT1151 init failed\n");
        return;
    }

    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
#else
    rt_kprintf("LVGL touch input disabled: PKG_USING_GT1151 is not enabled\n");
#endif
}

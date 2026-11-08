#include <rtthread.h>
#include "hal_data.h"

#ifdef BSP_USING_FS

#include <dfs_fs.h>

#define DBG_TAG "app.filesystem"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef BSP_USING_ONCHIP_FS
#include "fal.h"
#define FS_PARTITION_NAME  "disk"

static void sd_mount(void)
{
    struct rt_device *flash_dev = fal_blk_device_create(FS_PARTITION_NAME);

    if (flash_dev == NULL)
    {
        rt_kprintf("Can't create a block device on '%s' partition.\n", FS_PARTITION_NAME);
    }
    else
    {
        rt_kprintf("Create a block device on the %s partition of flash successful.\n", FS_PARTITION_NAME);
    }

    if(rt_device_find(FS_PARTITION_NAME) != RT_NULL)
    {
        dfs_mkfs("elm", FS_PARTITION_NAME);
        if (dfs_mount(FS_PARTITION_NAME, "/", "elm", 0, 0) == RT_EOK)
        {
            rt_kprintf("onchip elm filesystem mount to '/'\n");
        }
        else
        {
            rt_kprintf("onchip elm filesystem mount to '/' failed!\n");
        }
    }
    else
    {
        rt_kprintf("find filesystem portion failed\r\n");
    }
}

#elif defined(BSP_USING_SDCARD_FS)
#include <drv_sdhi.h>
#include "storage_app.h"

#ifdef FINSH_USING_MSH
#include <finsh.h>
#endif

/* SD Card hot plug detection pin */
#define SD_CHECK_PIN  RA_SDHI_CD_PIN
#define SD_MOUNT_POINT "/"
#define SD_SCAN_TIMEOUT_MS 3000
#define SD_SCAN_POLL_MS 100

static rt_base_t sd_check_pin = 0;
static rt_bool_t s_sd_mounted = RT_FALSE;

static rt_device_t sd_wait_device_ready(rt_int32_t timeout_ms)
{
    rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    rt_device_t device;

    do
    {
        device = rt_device_find("sd");
        if (device != RT_NULL)
        {
            return device;
        }

        rt_thread_mdelay(SD_SCAN_POLL_MS);
    } while ((rt_int32_t)(deadline - rt_tick_get()) > 0);

    return rt_device_find("sd");
}

static void _sdcard_mount(void)
{
    rt_device_t device;

    device = rt_device_find("sd");
    LOG_D("rt_device_find(\"sd\")=%p", device);
    if ((device != RT_NULL) && (s_sd_mounted == RT_TRUE))
    {
        return;
    }

    if (device == RT_NULL)
    {
        /*
         * Ask the RT-Thread MMC/SD core to enumerate the card again, then wait
         * for the block device named "sd" to be registered by block_dev.c.
         */
        mmcsd_wait_cd_changed(0);
        sdcard_change();
        mmcsd_wait_cd_changed(rt_tick_from_millisecond(SD_SCAN_TIMEOUT_MS));
        device = sd_wait_device_ready(SD_SCAN_TIMEOUT_MS);
    }

    if (device != RT_NULL)
    {
        if (dfs_mount("sd", SD_MOUNT_POINT, "elm", 0, 0) == RT_EOK)
        {
            s_sd_mounted = RT_TRUE;
            storage_app_on_card_mounted();
            LOG_I("sd card mount to '%s'", SD_MOUNT_POINT);
        }
        else
        {
            LOG_W("sd card mount to '%s' failed!", SD_MOUNT_POINT);
        }
    }
    else
    {
        LOG_W("sd block device not found after rescan");
    }
}

static void _sdcard_unmount(void)
{
    rt_thread_mdelay(200);
    if (s_sd_mounted)
    {
        dfs_unmount(SD_MOUNT_POINT);
        s_sd_mounted = RT_FALSE;
        storage_app_on_card_removed();
        LOG_I("Unmount \"%s\"", SD_MOUNT_POINT);
    }

    /*
     * Notify the MMC/SD core after unmounting. If host->card is still set, this
     * call removes the old block device so a later insert can create a fresh one.
     */
    mmcsd_wait_cd_changed(0);
    sdcard_change();
    mmcsd_wait_cd_changed(rt_tick_from_millisecond(SD_SCAN_TIMEOUT_MS));
}

static void _sdcard_resync(void)
{
    /*
     * The card-detect GPIO level is board/FSP dependent after SDHI owns the pin.
     * Treat any stable level change as "card state changed": remove the old
     * block device first, then try to enumerate and mount a card again.
     */
    _sdcard_unmount();
    rt_thread_mdelay(200);
    _sdcard_mount();
}

static void sd_auto_mount(void *parameter)
{
    rt_uint8_t last_sd_check_pin;
    rt_thread_mdelay(500);

    /*
     * First scan does not depend on GPIO level. P503 is also configured as
     * SDHI1 CD pin, so reading it as normal GPIO can be unreliable on FSP.
     */
    _sdcard_mount();

    last_sd_check_pin = (rt_uint8_t)rt_pin_read(sd_check_pin);

    while (1)
    {
        rt_uint8_t current_level;

        rt_thread_mdelay(200);
        current_level = (rt_uint8_t)rt_pin_read(sd_check_pin);

        if (current_level != last_sd_check_pin)
        {
            rt_thread_mdelay(50);
            current_level = (rt_uint8_t)rt_pin_read(sd_check_pin);
            if (current_level != last_sd_check_pin)
            {
                last_sd_check_pin = current_level;
                LOG_I("sd card detect changed, level=%d", current_level);
                _sdcard_resync();
            }
        }
    }
}

static void sd_mount(void)
{
    rt_thread_t tid;

    sd_check_pin = rt_pin_get(SD_CHECK_PIN);
    LOG_I("sd card detect pin: %s -> %d", SD_CHECK_PIN, sd_check_pin);
    rt_pin_mode(sd_check_pin, PIN_MODE_INPUT_PULLUP);

    tid = rt_thread_create("sd_mount", sd_auto_mount, RT_NULL,
                           2048, RT_THREAD_PRIORITY_MAX - 2, 20);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        LOG_E("create sd_mount thread err!");
        return;
    }
}

#ifdef FINSH_USING_MSH
static int sd_mount_probe_cmd(int argc, char **argv)
{
    rt_device_t device;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("[sd] cd pin %s index=%d level=%d\r\n",
               SD_CHECK_PIN, sd_check_pin, rt_pin_read(sd_check_pin));

    device = rt_device_find("sd");
    rt_kprintf("[sd] before probe rt_device_find(\"sd\")=%p mounted=%d\r\n",
               device, s_sd_mounted);

    _sdcard_mount();
    device = rt_device_find("sd");
    rt_kprintf("[sd] after probe rt_device_find(\"sd\")=%p mounted=%d\r\n",
               device, s_sd_mounted);

    return (device && s_sd_mounted) ? 0 : -1;
}
MSH_CMD_EXPORT(sd_mount_probe_cmd, trigger SDHI card scan and print sd device state);
#endif

#else
#include <spi_msd.h>
#include "drv_sci_spi.h"
int sd_mount(void)
{
    uint32_t cs_pin = BSP_IO_PORT_10_PIN_05;
    rt_hw_sci_spi_device_attach("scpi2", "scpi20", cs_pin);
    msd_init("sd0", "scpi20");
    if (dfs_mount("sd0", "/", "elm", 0, 0) == 0)
    {
        LOG_I("Mount \"/dev/sd0\" on \"/\"\n");
    }
    else
    {
        LOG_W("sd card mount to '/' failed!");
    }
    return 0;
}
#endif /* BSP_USING_SDCARD_FS */

int mount_init(void)
{
    sd_mount();
    return RT_EOK;
}
INIT_ENV_EXPORT(mount_init);
#endif

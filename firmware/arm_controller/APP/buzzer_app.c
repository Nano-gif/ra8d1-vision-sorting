#include "buzzer_app.h"
#include "mydefine.h"

static bool g_buzzer_active = false;
static uint32_t g_buzzer_stop_tick = 0;

static void buzzer_set_level(bsp_io_level_t level)
{
    (void)R_IOPORT_PinWrite(&g_ioport_ctrl, RA6_BUZZER, level);
}

fsp_err_t buzzer_init(void)
{
    g_buzzer_active = false;
    g_buzzer_stop_tick = 0;
    buzzer_set_level(BSP_IO_LEVEL_LOW);

    return FSP_SUCCESS;
}

void buzzer_beep(uint16_t duration_ms)
{
    if (0U == duration_ms)
    {
        buzzer_set_level(BSP_IO_LEVEL_LOW);
        g_buzzer_active = false;
        return;
    }

    g_buzzer_stop_tick = g_tick + duration_ms;
    g_buzzer_active = true;
    buzzer_set_level(BSP_IO_LEVEL_HIGH);
}

void buzzer_task(void)
{
    if (!g_buzzer_active)
    {
        return;
    }

    if ((int32_t)(g_tick - g_buzzer_stop_tick) >= 0)
    {
        buzzer_set_level(BSP_IO_LEVEL_LOW);
        g_buzzer_active = false;
    }
}

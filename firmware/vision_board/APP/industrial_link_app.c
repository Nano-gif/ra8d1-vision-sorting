#include "industrial_link_app.h"
#include "detect_app.h"

#include <rtthread.h>
#include <stdlib.h>

static uint16_t industrial_link_u32_lo(uint32_t value)
{
    return (uint16_t)(value & 0xFFFFU);
}

static uint16_t industrial_link_u32_hi(uint32_t value)
{
    return (uint16_t)((value >> 16U) & 0xFFFFU);
}

static uint16_t industrial_link_latest_result(const ui_detect_snapshot_t *snapshot,
                                              const detect_app_stats_t *stats)
{
    if ((snapshot == RT_NULL) || (stats == RT_NULL) || (stats->total_count == 0U))
    {
        return 0U;
    }

    return snapshot->ai.anomaly ? 1U : 0U;
}

uint16_t industrial_link_get_holding_count(void)
{
    return (uint16_t)INDUSTRIAL_LINK_REG_COUNT;
}

int industrial_link_read_holding(uint16_t address, uint16_t *out_value)
{
    ui_detect_snapshot_t snapshot;
    detect_app_stats_t stats;

    if (out_value == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (address >= (uint16_t)INDUSTRIAL_LINK_REG_COUNT)
    {
        return -RT_EINVAL;
    }

    if ((detect_app_get_snapshot(&snapshot) != RT_EOK) ||
        (detect_app_get_stats(&stats) != RT_EOK))
    {
        return -RT_ERROR;
    }

    switch ((industrial_link_reg_t)address)
    {
    case INDUSTRIAL_LINK_REG_MAGIC:
        *out_value = INDUSTRIAL_LINK_MAGIC;
        break;
    case INDUSTRIAL_LINK_REG_RUN_STATE:
        *out_value = (uint16_t)snapshot.run_state;
        break;
    case INDUSTRIAL_LINK_REG_STAGE:
        *out_value = (uint16_t)snapshot.stage;
        break;
    case INDUSTRIAL_LINK_REG_LATEST_RESULT:
        *out_value = industrial_link_latest_result(&snapshot, &stats);
        break;
    case INDUSTRIAL_LINK_REG_SCORE_X10:
        *out_value = snapshot.ai.score_x10;
        break;
    case INDUSTRIAL_LINK_REG_THRESHOLD_X10:
        *out_value = snapshot.ai.threshold_x10;
        break;
    case INDUSTRIAL_LINK_REG_TOTAL_COUNT_LO:
        *out_value = industrial_link_u32_lo(stats.total_count);
        break;
    case INDUSTRIAL_LINK_REG_TOTAL_COUNT_HI:
        *out_value = industrial_link_u32_hi(stats.total_count);
        break;
    case INDUSTRIAL_LINK_REG_OK_COUNT_LO:
        *out_value = industrial_link_u32_lo(stats.ok_count);
        break;
    case INDUSTRIAL_LINK_REG_OK_COUNT_HI:
        *out_value = industrial_link_u32_hi(stats.ok_count);
        break;
    case INDUSTRIAL_LINK_REG_NG_COUNT_LO:
        *out_value = industrial_link_u32_lo(stats.ng_count);
        break;
    case INDUSTRIAL_LINK_REG_NG_COUNT_HI:
        *out_value = industrial_link_u32_hi(stats.ng_count);
        break;
    case INDUSTRIAL_LINK_REG_AI_TIME_MS:
        *out_value = snapshot.ai.ai_time_ms;
        break;
    case INDUSTRIAL_LINK_REG_CAPTURE_MS:
        *out_value = stats.capture_ms;
        break;
    case INDUSTRIAL_LINK_REG_MODEL_INPUT_W:
        *out_value = snapshot.model.input_w;
        break;
    case INDUSTRIAL_LINK_REG_MODEL_INPUT_H:
        *out_value = snapshot.model.input_h;
        break;
    case INDUSTRIAL_LINK_REG_GRID_W:
        *out_value = snapshot.model.grid_w;
        break;
    case INDUSTRIAL_LINK_REG_GRID_H:
        *out_value = snapshot.model.grid_h;
        break;
    case INDUSTRIAL_LINK_REG_COMMAND:
        *out_value = INDUSTRIAL_LINK_CMD_NONE;
        break;
    case INDUSTRIAL_LINK_REG_VERSION:
        *out_value = INDUSTRIAL_LINK_PROTOCOL_VER;
        break;
    default:
        return -RT_EINVAL;
    }

    return RT_EOK;
}

int industrial_link_read_holding_block(uint16_t start_address,
                                       uint16_t count,
                                       uint16_t *out_values)
{
    uint16_t i;

    if ((out_values == RT_NULL) || (count == 0U))
    {
        return -RT_ERROR;
    }

    if ((start_address >= (uint16_t)INDUSTRIAL_LINK_REG_COUNT) ||
        (count > ((uint16_t)INDUSTRIAL_LINK_REG_COUNT - start_address)))
    {
        return -RT_EINVAL;
    }

    for (i = 0U; i < count; i++)
    {
        int ret = industrial_link_read_holding((uint16_t)(start_address + i), &out_values[i]);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    return RT_EOK;
}

int industrial_link_write_holding(uint16_t address, uint16_t value)
{
    if (address != (uint16_t)INDUSTRIAL_LINK_REG_COMMAND)
    {
        return -RT_EINVAL;
    }

    switch ((industrial_link_command_t)value)
    {
    case INDUSTRIAL_LINK_CMD_NONE:
        return RT_EOK;

    case INDUSTRIAL_LINK_CMD_START:
        detect_app_handle_command_from(UI_DETECT_CMD_START, "link");
        return RT_EOK;

    case INDUSTRIAL_LINK_CMD_STOP:
        detect_app_handle_command_from(UI_DETECT_CMD_STOP, "link");
        return RT_EOK;

    case INDUSTRIAL_LINK_CMD_ONCE:
        detect_app_handle_command_from(UI_DETECT_CMD_REFRESH, "link");
        return RT_EOK;

    default:
        return -RT_EINVAL;
    }
}

static uint16_t industrial_link_parse_u16(const char *text)
{
    unsigned long value;
    char *end_ptr;

    if (text == RT_NULL)
    {
        return 0U;
    }

    value = strtoul(text, &end_ptr, 0);
    (void)end_ptr;

    if (value > 0xFFFFUL)
    {
        value = 0xFFFFUL;
    }

    return (uint16_t)value;
}

static const char *industrial_link_reg_name(uint16_t address)
{
    switch ((industrial_link_reg_t)address)
    {
    case INDUSTRIAL_LINK_REG_MAGIC:
        return "magic";
    case INDUSTRIAL_LINK_REG_RUN_STATE:
        return "run_state";
    case INDUSTRIAL_LINK_REG_STAGE:
        return "stage";
    case INDUSTRIAL_LINK_REG_LATEST_RESULT:
        return "latest_result";
    case INDUSTRIAL_LINK_REG_SCORE_X10:
        return "score_x10";
    case INDUSTRIAL_LINK_REG_THRESHOLD_X10:
        return "threshold_x10";
    case INDUSTRIAL_LINK_REG_TOTAL_COUNT_LO:
        return "total_lo";
    case INDUSTRIAL_LINK_REG_TOTAL_COUNT_HI:
        return "total_hi";
    case INDUSTRIAL_LINK_REG_OK_COUNT_LO:
        return "ok_lo";
    case INDUSTRIAL_LINK_REG_OK_COUNT_HI:
        return "ok_hi";
    case INDUSTRIAL_LINK_REG_NG_COUNT_LO:
        return "ng_lo";
    case INDUSTRIAL_LINK_REG_NG_COUNT_HI:
        return "ng_hi";
    case INDUSTRIAL_LINK_REG_AI_TIME_MS:
        return "ai_ms";
    case INDUSTRIAL_LINK_REG_CAPTURE_MS:
        return "cap_ms";
    case INDUSTRIAL_LINK_REG_MODEL_INPUT_W:
        return "input_w";
    case INDUSTRIAL_LINK_REG_MODEL_INPUT_H:
        return "input_h";
    case INDUSTRIAL_LINK_REG_GRID_W:
        return "grid_w";
    case INDUSTRIAL_LINK_REG_GRID_H:
        return "grid_h";
    case INDUSTRIAL_LINK_REG_COMMAND:
        return "command";
    case INDUSTRIAL_LINK_REG_VERSION:
        return "version";
    default:
        return "unknown";
    }
}

static int industrial_link_regs_cmd(int argc, char **argv)
{
    uint16_t start = 0U;
    uint16_t count = (uint16_t)INDUSTRIAL_LINK_REG_COUNT;
    uint16_t i;

    if (argc >= 2)
    {
        start = industrial_link_parse_u16(argv[1]);
    }
    if (argc >= 3)
    {
        count = industrial_link_parse_u16(argv[2]);
    }

    if ((start >= (uint16_t)INDUSTRIAL_LINK_REG_COUNT) ||
        (count == 0U) ||
        (count > ((uint16_t)INDUSTRIAL_LINK_REG_COUNT - start)))
    {
        rt_kprintf("[link] usage: industrial_link_regs_cmd [start] [count]\n");
        rt_kprintf("[link] valid range: 0x0000..0x%04x\n",
                   (unsigned int)((uint16_t)INDUSTRIAL_LINK_REG_COUNT - 1U));
        return -1;
    }

    rt_kprintf("[link] holding registers count=%u\n",
               (unsigned int)INDUSTRIAL_LINK_REG_COUNT);
    for (i = 0U; i < count; i++)
    {
        uint16_t address = (uint16_t)(start + i);
        uint16_t value = 0U;
        int ret = industrial_link_read_holding(address, &value);

        if (ret == RT_EOK)
        {
            rt_kprintf("[link] 0x%04x %-14s = 0x%04x (%u)\n",
                       (unsigned int)address,
                       industrial_link_reg_name(address),
                       (unsigned int)value,
                       (unsigned int)value);
        }
        else
        {
            rt_kprintf("[link] 0x%04x read failed: %d\n",
                       (unsigned int)address,
                       ret);
            return -1;
        }
    }

    return 0;
}

static int industrial_link_write_cmd(int argc, char **argv)
{
    uint16_t address;
    uint16_t value;
    int ret;

    if (argc < 3)
    {
        rt_kprintf("[link] usage: industrial_link_write_cmd <addr> <value>\n");
        rt_kprintf("[link] command register 0x0012: 1=start 2=stop 3=once\n");
        return -1;
    }

    address = industrial_link_parse_u16(argv[1]);
    value = industrial_link_parse_u16(argv[2]);
    ret = industrial_link_write_holding(address, value);

    if (ret != RT_EOK)
    {
        rt_kprintf("[link] write failed addr=0x%04x value=%u ret=%d\n",
                   (unsigned int)address,
                   (unsigned int)value,
                   ret);
        return -1;
    }

    rt_kprintf("[link] write ok addr=0x%04x value=%u\n",
               (unsigned int)address,
               (unsigned int)value);
    return 0;
}

MSH_CMD_EXPORT(industrial_link_regs_cmd, dump industrial link holding registers);
MSH_CMD_EXPORT(industrial_link_write_cmd, write industrial link holding register);

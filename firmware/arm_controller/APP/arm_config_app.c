#include "arm_config_app.h"
#include <stddef.h>
#include <string.h>

#define ARM_CONFIG_DF_SLOT_SIZE      (BSP_FEATURE_FLASH_HP_DF_BLOCK_SIZE)
#define ARM_CONFIG_DF_SLOT1_ADDR     (BSP_FEATURE_FLASH_DATA_FLASH_START + BSP_DATA_FLASH_SIZE_BYTES - ARM_CONFIG_DF_SLOT_SIZE)
#define ARM_CONFIG_DF_SLOT0_ADDR     (ARM_CONFIG_DF_SLOT1_ADDR - ARM_CONFIG_DF_SLOT_SIZE)
#define ARM_CONFIG_ACTION_SLOT_BLOCKS (6U)
#define ARM_CONFIG_ACTION_SLOT_SIZE  (ARM_CONFIG_DF_SLOT_SIZE * ARM_CONFIG_ACTION_SLOT_BLOCKS)
#define ARM_CONFIG_ACTION_SLOT1_ADDR (ARM_CONFIG_DF_SLOT0_ADDR - ARM_CONFIG_ACTION_SLOT_SIZE)
#define ARM_CONFIG_ACTION_SLOT0_ADDR (ARM_CONFIG_ACTION_SLOT1_ADDR - ARM_CONFIG_ACTION_SLOT_SIZE)
#define ARM_CONFIG_HOME_MAGIC        (0x41524D48UL)
#define ARM_CONFIG_ACTION_MAGIC      (0x41524D41UL)
#define ARM_CONFIG_VERSION           (1U)
#define ARM_CONFIG_HOME_MIN_US       (500U)
#define ARM_CONFIG_HOME_MAX_US       (2500U)
#define ARM_CONFIG_ACTION_MIN_DURATION_MS (100U)
#define ARM_CONFIG_ACTION_MAX_DURATION_MS (10000U)

#if ARM_CONFIG_ACTION_SERVO_COUNT != ARM_CONFIG_HOME_SERVO_COUNT
#error "ARM_CONFIG_ACTION_SERVO_COUNT must match ARM_CONFIG_HOME_SERVO_COUNT"
#endif

typedef struct st_arm_config_home_record
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT];
    uint16_t reserved;
    uint32_t checksum;
    uint32_t reserved2;
} arm_config_home_record_t;

typedef struct st_arm_config_action_record
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint8_t step_count;
    uint8_t reserved0;
    uint16_t reserved1;
    arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS];
    uint32_t checksum;
} arm_config_action_record_t;

typedef char arm_config_action_step_size_check[(sizeof(arm_config_action_step_t) == 14U) ? 1 : -1];
typedef char arm_config_action_record_size_check[(sizeof(arm_config_action_record_t) <= ARM_CONFIG_ACTION_SLOT_SIZE) ? 1 : -1];

static bool g_arm_config_ready = false;
static bool g_arm_config_home_valid = false;
static bool g_arm_config_action_valid = false;
static uint32_t g_arm_config_home_active_sequence = 0;
static uint32_t g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT1_ADDR;
static uint32_t g_arm_config_action_active_sequence = 0;
static uint32_t g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT1_ADDR;

static uint32_t arm_config_checksum_bytes(const void *p_record, uint32_t len)
{
    const uint8_t *p_data = (const uint8_t *)p_record;
    uint32_t checksum = 0x13572468UL;

    for (uint32_t i = 0; i < len; i++)
    {
        checksum += p_data[i];
        checksum = (checksum << 5) | (checksum >> 27);
        checksum ^= 0xA5A5A5A5UL;
    }

    return checksum;
}

static uint32_t arm_config_home_checksum(const arm_config_home_record_t *p_record)
{
    return arm_config_checksum_bytes(p_record, offsetof(arm_config_home_record_t, checksum));
}

static uint32_t arm_config_action_checksum(const arm_config_action_record_t *p_record)
{
    return arm_config_checksum_bytes(p_record, offsetof(arm_config_action_record_t, checksum));
}

static bool arm_config_home_range_is_valid(const uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT])
{
    for (uint8_t i = 0; i < ARM_CONFIG_HOME_SERVO_COUNT; i++)
    {
        if ((home_us[i] < ARM_CONFIG_HOME_MIN_US) || (home_us[i] > ARM_CONFIG_HOME_MAX_US))
        {
            return false;
        }
    }

    return true;
}

static bool arm_config_action_steps_are_valid(const arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                              uint8_t step_count)
{
    if (step_count > ARM_CONFIG_ACTION_MAX_STEPS)
    {
        return false;
    }

    if ((step_count > 0U) && (NULL == steps))
    {
        return false;
    }

    for (uint8_t step = 0; step < step_count; step++)
    {
        for (uint8_t ch = 0; ch < ARM_CONFIG_ACTION_SERVO_COUNT; ch++)
        {
            if ((steps[step].servo_us[ch] < ARM_CONFIG_HOME_MIN_US) ||
                (steps[step].servo_us[ch] > ARM_CONFIG_HOME_MAX_US))
            {
                return false;
            }
        }

        if ((steps[step].duration_ms < ARM_CONFIG_ACTION_MIN_DURATION_MS) ||
            (steps[step].duration_ms > ARM_CONFIG_ACTION_MAX_DURATION_MS))
        {
            return false;
        }
    }

    return true;
}

static bool arm_config_home_record_is_valid(const arm_config_home_record_t *p_record)
{
    if (NULL == p_record)
    {
        return false;
    }

    if ((ARM_CONFIG_HOME_MAGIC != p_record->magic) ||
        (ARM_CONFIG_VERSION != p_record->version) ||
        (sizeof(arm_config_home_record_t) != p_record->length))
    {
        return false;
    }

    if (!arm_config_home_range_is_valid(p_record->home_us))
    {
        return false;
    }

    return (p_record->checksum == arm_config_home_checksum(p_record));
}

static bool arm_config_action_record_is_valid(const arm_config_action_record_t *p_record)
{
    if (NULL == p_record)
    {
        return false;
    }

    if ((ARM_CONFIG_ACTION_MAGIC != p_record->magic) ||
        (ARM_CONFIG_VERSION != p_record->version) ||
        (sizeof(arm_config_action_record_t) != p_record->length))
    {
        return false;
    }

    if (!arm_config_action_steps_are_valid(p_record->steps, p_record->step_count))
    {
        return false;
    }

    return (p_record->checksum == arm_config_action_checksum(p_record));
}

static void arm_config_home_record_fill(arm_config_home_record_t *p_record,
                                        const uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT],
                                        uint32_t sequence)
{
    memset(p_record, 0xFF, sizeof(*p_record));
    p_record->magic = ARM_CONFIG_HOME_MAGIC;
    p_record->version = ARM_CONFIG_VERSION;
    p_record->length = sizeof(arm_config_home_record_t);
    p_record->sequence = sequence;

    for (uint8_t i = 0; i < ARM_CONFIG_HOME_SERVO_COUNT; i++)
    {
        p_record->home_us[i] = home_us[i];
    }

    p_record->reserved = 0xFFFFU;
    p_record->reserved2 = 0xFFFFFFFFUL;
    p_record->checksum = arm_config_home_checksum(p_record);
}

static void arm_config_action_record_fill(arm_config_action_record_t *p_record,
                                          const arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                          uint8_t step_count,
                                          uint32_t sequence)
{
    memset(p_record, 0xFF, sizeof(*p_record));
    p_record->magic = ARM_CONFIG_ACTION_MAGIC;
    p_record->version = ARM_CONFIG_VERSION;
    p_record->length = sizeof(arm_config_action_record_t);
    p_record->sequence = sequence;
    p_record->step_count = step_count;
    p_record->reserved0 = 0xFFU;
    p_record->reserved1 = 0xFFFFU;

    for (uint8_t step = 0; step < step_count; step++)
    {
        p_record->steps[step] = steps[step];
    }

    p_record->checksum = arm_config_action_checksum(p_record);
}

static uint32_t arm_config_next_home_slot_addr(void)
{
    if (ARM_CONFIG_DF_SLOT0_ADDR == g_arm_config_home_active_slot_addr)
    {
        return ARM_CONFIG_DF_SLOT1_ADDR;
    }

    return ARM_CONFIG_DF_SLOT0_ADDR;
}

static uint32_t arm_config_next_action_slot_addr(void)
{
    if (ARM_CONFIG_ACTION_SLOT0_ADDR == g_arm_config_action_active_slot_addr)
    {
        return ARM_CONFIG_ACTION_SLOT1_ADDR;
    }

    return ARM_CONFIG_ACTION_SLOT0_ADDR;
}

fsp_err_t arm_config_init(void)
{
    fsp_err_t err;

    err = g_flash0.p_api->open(g_flash0.p_ctrl, g_flash0.p_cfg);
    if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
    {
        g_arm_config_ready = false;
        return err;
    }

    g_arm_config_ready = true;
    return FSP_SUCCESS;
}

fsp_err_t arm_config_load_home(uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT])
{
    const arm_config_home_record_t *p_slot0 = (const arm_config_home_record_t *)ARM_CONFIG_DF_SLOT0_ADDR;
    const arm_config_home_record_t *p_slot1 = (const arm_config_home_record_t *)ARM_CONFIG_DF_SLOT1_ADDR;
    const arm_config_home_record_t *p_selected = NULL;
    bool slot0_valid;
    bool slot1_valid;

    if (NULL == home_us)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!g_arm_config_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    slot0_valid = arm_config_home_record_is_valid(p_slot0);
    slot1_valid = arm_config_home_record_is_valid(p_slot1);

    if (slot0_valid && slot1_valid)
    {
        if (p_slot0->sequence >= p_slot1->sequence)
        {
            p_selected = p_slot0;
            g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT0_ADDR;
        }
        else
        {
            p_selected = p_slot1;
            g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT1_ADDR;
        }
    }
    else if (slot0_valid)
    {
        p_selected = p_slot0;
        g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT0_ADDR;
    }
    else if (slot1_valid)
    {
        p_selected = p_slot1;
        g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT1_ADDR;
    }
    else
    {
        g_arm_config_home_valid = false;
        g_arm_config_home_active_sequence = 0;
        g_arm_config_home_active_slot_addr = ARM_CONFIG_DF_SLOT1_ADDR;
        return FSP_ERR_NOT_FOUND;
    }

    for (uint8_t i = 0; i < ARM_CONFIG_HOME_SERVO_COUNT; i++)
    {
        home_us[i] = p_selected->home_us[i];
    }

    g_arm_config_home_valid = true;
    g_arm_config_home_active_sequence = p_selected->sequence;
    return FSP_SUCCESS;
}

fsp_err_t arm_config_save_home(const uint16_t home_us[ARM_CONFIG_HOME_SERVO_COUNT])
{
    arm_config_home_record_t record;
    fsp_err_t err;
    uint32_t write_addr;

    if (NULL == home_us)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!g_arm_config_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if (!arm_config_home_range_is_valid(home_us))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    write_addr = arm_config_next_home_slot_addr();
    arm_config_home_record_fill(&record, home_us, g_arm_config_home_active_sequence + 1U);

    err = g_flash0.p_api->erase(g_flash0.p_ctrl, write_addr, 1U);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_flash0.p_api->write(g_flash0.p_ctrl,
                                (uint32_t)&record,
                                write_addr,
                                sizeof(record));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (!arm_config_home_record_is_valid((const arm_config_home_record_t *)write_addr))
    {
        return FSP_ERR_WRITE_FAILED;
    }

    g_arm_config_home_valid = true;
    g_arm_config_home_active_sequence = record.sequence;
    g_arm_config_home_active_slot_addr = write_addr;

    return FSP_SUCCESS;
}

bool arm_config_home_is_valid(void)
{
    return g_arm_config_home_valid;
}

fsp_err_t arm_config_load_action(arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                 uint8_t *p_step_count)
{
    const arm_config_action_record_t *p_slot0 = (const arm_config_action_record_t *)ARM_CONFIG_ACTION_SLOT0_ADDR;
    const arm_config_action_record_t *p_slot1 = (const arm_config_action_record_t *)ARM_CONFIG_ACTION_SLOT1_ADDR;
    const arm_config_action_record_t *p_selected = NULL;
    bool slot0_valid;
    bool slot1_valid;

    if ((NULL == steps) || (NULL == p_step_count))
    {
        return FSP_ERR_ASSERTION;
    }

    if (!g_arm_config_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    slot0_valid = arm_config_action_record_is_valid(p_slot0);
    slot1_valid = arm_config_action_record_is_valid(p_slot1);

    if (slot0_valid && slot1_valid)
    {
        if (p_slot0->sequence >= p_slot1->sequence)
        {
            p_selected = p_slot0;
            g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT0_ADDR;
        }
        else
        {
            p_selected = p_slot1;
            g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT1_ADDR;
        }
    }
    else if (slot0_valid)
    {
        p_selected = p_slot0;
        g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT0_ADDR;
    }
    else if (slot1_valid)
    {
        p_selected = p_slot1;
        g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT1_ADDR;
    }
    else
    {
        g_arm_config_action_valid = false;
        g_arm_config_action_active_sequence = 0;
        g_arm_config_action_active_slot_addr = ARM_CONFIG_ACTION_SLOT1_ADDR;
        *p_step_count = 0;
        return FSP_ERR_NOT_FOUND;
    }

    memset(steps, 0, sizeof(arm_config_action_step_t) * ARM_CONFIG_ACTION_MAX_STEPS);
    for (uint8_t step = 0; step < p_selected->step_count; step++)
    {
        steps[step] = p_selected->steps[step];
    }

    *p_step_count = p_selected->step_count;
    g_arm_config_action_valid = true;
    g_arm_config_action_active_sequence = p_selected->sequence;

    return FSP_SUCCESS;
}

fsp_err_t arm_config_save_action(const arm_config_action_step_t steps[ARM_CONFIG_ACTION_MAX_STEPS],
                                 uint8_t step_count)
{
    arm_config_action_record_t record;
    fsp_err_t err;
    uint32_t write_addr;

    if (NULL == steps)
    {
        return FSP_ERR_ASSERTION;
    }

    if (!g_arm_config_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if (!arm_config_action_steps_are_valid(steps, step_count))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    write_addr = arm_config_next_action_slot_addr();
    arm_config_action_record_fill(&record, steps, step_count, g_arm_config_action_active_sequence + 1U);

    err = g_flash0.p_api->erase(g_flash0.p_ctrl, write_addr, ARM_CONFIG_ACTION_SLOT_BLOCKS);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_flash0.p_api->write(g_flash0.p_ctrl,
                                (uint32_t)&record,
                                write_addr,
                                sizeof(record));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (!arm_config_action_record_is_valid((const arm_config_action_record_t *)write_addr))
    {
        return FSP_ERR_WRITE_FAILED;
    }

    g_arm_config_action_valid = true;
    g_arm_config_action_active_sequence = record.sequence;
    g_arm_config_action_active_slot_addr = write_addr;

    return FSP_SUCCESS;
}

bool arm_config_action_is_valid(void)
{
    return g_arm_config_action_valid;
}

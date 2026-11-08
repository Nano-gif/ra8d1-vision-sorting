#include "modbus_slave_app.h"
#include "mydefine.h"

#define MODBUS_SLAVE_ADDRESS                  (1U)
#define MODBUS_PROTOCOL_VERSION               (0x0001U)
#define MODBUS_FIRMWARE_VERSION               (0x0001U)
#define MODBUS_DEVICE_ID                      (0xA601U)

#define MODBUS_FUNC_READ_INPUT_REGS           (0x04U)
#define MODBUS_FUNC_WRITE_SINGLE_REG          (0x06U)
#define MODBUS_FUNC_WRITE_MULTIPLE_REGS       (0x10U)

#define MODBUS_EX_ILLEGAL_FUNCTION            (0x01U)
#define MODBUS_EX_ILLEGAL_DATA_ADDRESS        (0x02U)
#define MODBUS_EX_ILLEGAL_DATA_VALUE          (0x03U)

#define MODBUS_FRAME_BUFFER_SIZE              (128U)
#define MODBUS_TX_BUFFER_SIZE                 (128U)
#define MODBUS_FRAME_TIMEOUT_MS               (5U)
#define MODBUS_MAX_REG_COUNT                  (32U)
#define MODBUS_REMOTE_ACTIVE_TIMEOUT_MS       (1000U)

#define MODBUS_INPUT_BASE_START               (0x0000U)
#define MODBUS_INPUT_BASE_END                 (0x000FU)
#define MODBUS_INPUT_AXIS_CURRENT_START       (0x0020U)
#define MODBUS_INPUT_AXIS_CURRENT_END         (0x0025U)
#define MODBUS_INPUT_CURRENT_US_START         (0x0030U)
#define MODBUS_INPUT_CURRENT_US_END           (0x0035U)
#define MODBUS_INPUT_AXIS_TARGET_START        (0x0040U)
#define MODBUS_INPUT_AXIS_TARGET_END          (0x0045U)
#define MODBUS_INPUT_TARGET_US_START          (0x0050U)
#define MODBUS_INPUT_TARGET_US_END            (0x0055U)
#define MODBUS_INPUT_ACTION_START             (0x0060U)
#define MODBUS_INPUT_ACTION_END               (0x0064U)

#define MODBUS_HOLD_CONTROL_COMMAND           (0x00F0U)
#define MODBUS_HOLD_TARGET_APPLY              (0x00F1U)
#define MODBUS_HOLD_COMMAND_SEQ               (0x00F2U)
#define MODBUS_HOLD_AXIS_TARGET_START         (0x0100U)
#define MODBUS_HOLD_AXIS_TARGET_END           (0x0105U)
#define MODBUS_HOLD_RAW_US_START              (0x0120U)
#define MODBUS_HOLD_RAW_US_END                (0x0125U)

/* NG 分拣角度补偿寄存器 */
#define MODBUS_HOLD_OBJECT_YAW                (0x00D0U)  /* 写: object_yaw_x10 (int16, 0.1deg) */
#define MODBUS_HOLD_TASK_PARAM_SEQ            (0x00D1U)  /* 写: task_param_seq (uint16) */
#define MODBUS_INPUT_LAST_OBJECT_YAW          (0x0065U)  /* 读: 最近锁存角度 */
#define MODBUS_INPUT_LAST_TASK_PARAM_SEQ      (0x0066U)  /* 读: 最近锁存序号 */
#define MODBUS_INPUT_TASK_PARAM_VALID         (0x0067U)  /* 读: 上次PLAY是否绑定角度(0/1) */

#define MODBUS_COMMAND_ESTOP                  (1U)
#define MODBUS_COMMAND_CLEAR_ESTOP            (2U)
#define MODBUS_COMMAND_RECORD_START           (3U)
#define MODBUS_COMMAND_RECORD_STOP            (4U)
#define MODBUS_COMMAND_PLAY                   (5U)
#define MODBUS_COMMAND_STOP_PLAY              (6U)
#define MODBUS_COMMAND_SAVE_ACTION            (7U)
#define MODBUS_COMMAND_CLEAR_ACTION           (8U)
#define MODBUS_COMMAND_HOME                   (9U)

#define MODBUS_APPLY_NONE                     (0U)
#define MODBUS_APPLY_AXIS_TARGET              (1U)
#define MODBUS_APPLY_RAW_US                   (2U)

#define MODBUS_COMMAND_CODE_APPLY_AXIS        (0x0101U)
#define MODBUS_COMMAND_CODE_APPLY_RAW_US      (0x0102U)

#define MODBUS_COMMAND_RESULT_NONE            (0U)
#define MODBUS_COMMAND_RESULT_ACCEPTED        (1U)
#define MODBUS_COMMAND_RESULT_BUSY            (2U)
#define MODBUS_COMMAND_RESULT_INVALID_PARAM   (3U)
#define MODBUS_COMMAND_RESULT_ESTOP           (4U)
#define MODBUS_COMMAND_RESULT_HMI_ACTIVE      (5U)
#define MODBUS_COMMAND_RESULT_ACTION_EMPTY    (6U)
#define MODBUS_COMMAND_RESULT_FAILED          (7U)
#define MODBUS_COMMAND_RESULT_UNSUPPORTED     (8U)
#define MODBUS_COMMAND_RESULT_DUPLICATE       (9U)

#define MODBUS_MODE_IDLE                      (0U)
#define MODBUS_MODE_MANUAL                    (1U)
#define MODBUS_MODE_RECORD                    (2U)
#define MODBUS_MODE_PLAYBACK                  (3U)
#define MODBUS_MODE_ESTOP                     (4U)

static uint8_t g_modbus_frame[MODBUS_FRAME_BUFFER_SIZE];
static uint16_t g_modbus_frame_len = 0;
static bool g_modbus_frame_overflow = false;
static uint32_t g_modbus_last_byte_tick = 0;
static uint32_t g_modbus_last_activity_tick = 0;
static uint32_t g_modbus_last_heartbeat_tick = 0;
static uint16_t g_modbus_heartbeat_counter = 0;
static uint16_t g_modbus_comm_error_count = 0;

static uint16_t g_modbus_control_command = 0;
static uint16_t g_modbus_target_apply = 0;
static uint16_t g_modbus_command_seq = 0;
static int16_t g_modbus_axis_target[ARM_SERVO_COUNT];
static uint16_t g_modbus_raw_us_target[ARM_SERVO_COUNT];

static uint16_t g_modbus_last_command_seq = 0;
static uint16_t g_modbus_last_command_code = 0;
static uint16_t g_modbus_last_command_result = MODBUS_COMMAND_RESULT_NONE;
static bool g_modbus_has_last_command = false;
static bool g_modbus_emergency_stop = false;

/* NG 分拣角度补偿状态 */
static int16_t  g_modbus_object_yaw_x10 = 0;        /* 0x00D0 写影子 */
static uint16_t g_modbus_task_param_seq_shadow = 0; /* 0x00D1 写影子 */
static int16_t  g_modbus_last_object_yaw_x10 = 0;   /* 0x0065 回读 */
static uint16_t g_modbus_last_task_param_seq = 0;   /* 0x0066 回读 */
static uint16_t g_modbus_task_param_valid = 0;      /* 0x0067 回读 (0/1) */

static void modbus_slave_frame_reset(void);
static void modbus_slave_handle_frame(void);
static uint16_t modbus_slave_crc16(const uint8_t *p_data, uint16_t len);
static uint16_t modbus_slave_read_be16(const uint8_t *p_data);
static void modbus_slave_write_be16(uint8_t *p_data, uint16_t value);
static fsp_err_t modbus_slave_send_response(uint8_t *p_data, uint16_t len);
static void modbus_slave_send_exception(uint8_t function, uint8_t exception_code);
static void modbus_slave_inc_comm_error(void);
static bool modbus_slave_input_range_is_valid(uint16_t start, uint16_t count);
static bool modbus_slave_read_input_register(uint16_t address, uint16_t *p_value);
static void modbus_slave_handle_read_input_registers(const uint8_t *p_frame, uint16_t len);
static void modbus_slave_handle_write_single_register(const uint8_t *p_frame, uint16_t len);
static void modbus_slave_handle_write_multiple_registers(const uint8_t *p_frame, uint16_t len);
static bool modbus_slave_write_shadow(uint16_t address, uint16_t value);
static bool modbus_slave_multiple_write_range_is_supported(uint16_t start, uint16_t count);
static bool modbus_slave_validate_multiple_write(uint16_t start, uint16_t count, const uint16_t *p_values);
static void modbus_slave_apply_multiple_write(uint16_t start, uint16_t count, const uint16_t *p_values);
static void modbus_slave_process_control_shadow(void);
static uint16_t modbus_slave_make_command_code(uint16_t control_command, uint16_t target_apply);
static void modbus_slave_finish_command(uint16_t seq, uint16_t code, uint16_t result);
static uint16_t modbus_slave_execute_control_command(uint16_t command);
static uint16_t modbus_slave_execute_target_apply(uint16_t target_apply);
static bool modbus_slave_targets_from_axis(uint16_t target_us[ARM_SERVO_COUNT]);
static bool modbus_slave_targets_from_raw_us(uint16_t target_us[ARM_SERVO_COUNT]);
static bool modbus_slave_axis_values_to_pwm(const int16_t axis_value[ARM_SERVO_COUNT],
                                            uint16_t target_us[ARM_SERVO_COUNT]);
static bool modbus_slave_raw_us_values_are_valid(const uint16_t raw_us[ARM_SERVO_COUNT]);
static bool modbus_slave_get_axis_value_from_us(uint16_t base_address,
                                                uint16_t address,
                                                int16_t *p_value);
static int16_t modbus_slave_float_to_x10(float value);
static uint16_t modbus_slave_i16_to_reg(int16_t value);
static int16_t modbus_slave_reg_to_i16(uint16_t value);
static uint16_t modbus_slave_get_status_flags(void);
static uint16_t modbus_slave_get_control_mode(void);
static bool modbus_slave_is_remote_active(void);
static bool modbus_slave_is_homed(void);
static bool modbus_slave_is_target_moving(void);
static void modbus_slave_refresh_target_shadow_from_control(void);

fsp_err_t modbus_slave_init(void)
{
    modbus_slave_frame_reset();
    g_modbus_last_activity_tick = 0;
    g_modbus_last_heartbeat_tick = g_tick;
    g_modbus_heartbeat_counter = 0;
    g_modbus_comm_error_count = 0;
    g_modbus_control_command = 0;
    g_modbus_target_apply = 0;
    g_modbus_command_seq = 0;
    g_modbus_last_command_seq = 0;
    g_modbus_last_command_code = 0;
    g_modbus_last_command_result = MODBUS_COMMAND_RESULT_NONE;
    g_modbus_has_last_command = false;
    g_modbus_emergency_stop = false;
    modbus_slave_refresh_target_shadow_from_control();

    return FSP_SUCCESS;
}

void modbus_slave_task(void)
{
    uint8_t ch;

    while (rs485_uart_get_char(&ch))
    {
        if (g_modbus_frame_len < MODBUS_FRAME_BUFFER_SIZE)
        {
            g_modbus_frame[g_modbus_frame_len] = ch;
            g_modbus_frame_len++;
        }
        else
        {
            g_modbus_frame_overflow = true;
        }

        g_modbus_last_byte_tick = g_tick;
    }

    if (g_modbus_frame_overflow)
    {
        modbus_slave_inc_comm_error();
        modbus_slave_frame_reset();
        return;
    }

    if ((g_modbus_frame_len > 0U) &&
        ((uint32_t)(g_tick - g_modbus_last_byte_tick) >= MODBUS_FRAME_TIMEOUT_MS))
    {
        modbus_slave_handle_frame();
        modbus_slave_frame_reset();
    }

    if ((uint32_t)(g_tick - g_modbus_last_heartbeat_tick) >= 100U)
    {
        g_modbus_last_heartbeat_tick = g_tick;
        g_modbus_heartbeat_counter++;
    }
}

uint16_t modbus_slave_get_comm_error_count(void)
{
    return g_modbus_comm_error_count;
}

static void modbus_slave_frame_reset(void)
{
    g_modbus_frame_len = 0;
    g_modbus_frame_overflow = false;
}

static void modbus_slave_handle_frame(void)
{
    uint16_t rx_crc;
    uint16_t calc_crc;
    uint8_t address;
    uint8_t function;

    if (g_modbus_frame_len < 4U)
    {
        modbus_slave_inc_comm_error();
        return;
    }

    address = g_modbus_frame[0];
    if (address != MODBUS_SLAVE_ADDRESS)
    {
        return;
    }

    rx_crc = (uint16_t)g_modbus_frame[g_modbus_frame_len - 2U] |
             (uint16_t)((uint16_t)g_modbus_frame[g_modbus_frame_len - 1U] << 8U);
    calc_crc = modbus_slave_crc16(g_modbus_frame, (uint16_t)(g_modbus_frame_len - 2U));
    if (rx_crc != calc_crc)
    {
        modbus_slave_inc_comm_error();
        return;
    }

    g_modbus_last_activity_tick = g_tick;
    hmi_uart_notify_vision_board_activity();

    function = g_modbus_frame[1];
    if (MODBUS_FUNC_READ_INPUT_REGS == function)
    {
        modbus_slave_handle_read_input_registers(g_modbus_frame, g_modbus_frame_len);
    }
    else if (MODBUS_FUNC_WRITE_SINGLE_REG == function)
    {
        modbus_slave_handle_write_single_register(g_modbus_frame, g_modbus_frame_len);
    }
    else if (MODBUS_FUNC_WRITE_MULTIPLE_REGS == function)
    {
        modbus_slave_handle_write_multiple_registers(g_modbus_frame, g_modbus_frame_len);
    }
    else
    {
        modbus_slave_send_exception(function, MODBUS_EX_ILLEGAL_FUNCTION);
    }
}

static uint16_t modbus_slave_crc16(const uint8_t *p_data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= p_data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (0U != (crc & 0x0001U))
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static uint16_t modbus_slave_read_be16(const uint8_t *p_data)
{
    return (uint16_t)((uint16_t)p_data[0] << 8U) | (uint16_t)p_data[1];
}

static void modbus_slave_write_be16(uint8_t *p_data, uint16_t value)
{
    p_data[0] = (uint8_t)(value >> 8U);
    p_data[1] = (uint8_t)(value & 0xFFU);
}

static fsp_err_t modbus_slave_send_response(uint8_t *p_data, uint16_t len)
{
    uint16_t crc;

    if ((NULL == p_data) || ((len + 2U) > MODBUS_TX_BUFFER_SIZE))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    crc = modbus_slave_crc16(p_data, len);
    p_data[len] = (uint8_t)(crc & 0xFFU);
    p_data[len + 1U] = (uint8_t)(crc >> 8U);

    return rs485_uart_write(p_data, (uint32_t)(len + 2U));
}

static void modbus_slave_send_exception(uint8_t function, uint8_t exception_code)
{
    uint8_t response[5];

    response[0] = MODBUS_SLAVE_ADDRESS;
    response[1] = (uint8_t)(function | 0x80U);
    response[2] = exception_code;
    (void)modbus_slave_send_response(response, 3U);
    modbus_slave_inc_comm_error();
}

static void modbus_slave_inc_comm_error(void)
{
    if (g_modbus_comm_error_count < 0xFFFFU)
    {
        g_modbus_comm_error_count++;
    }
}

static bool modbus_slave_input_range_is_valid(uint16_t start, uint16_t count)
{
    uint32_t end;
    uint16_t value;

    if ((0U == count) || (count > MODBUS_MAX_REG_COUNT))
    {
        return false;
    }

    end = (uint32_t)start + (uint32_t)count - 1UL;
    if (end > 0xFFFFUL)
    {
        return false;
    }

    for (uint16_t offset = 0; offset < count; offset++)
    {
        if (!modbus_slave_read_input_register((uint16_t)(start + offset), &value))
        {
            return false;
        }
    }

    return true;
}

static bool modbus_slave_read_input_register(uint16_t address, uint16_t *p_value)
{
    arm_action_info_t action_info;
    int16_t axis_value;

    if (NULL == p_value)
    {
        return false;
    }

    if ((address >= MODBUS_INPUT_BASE_START) && (address <= MODBUS_INPUT_BASE_END))
    {
        switch (address)
        {
            case 0x0000U:
                *p_value = MODBUS_DEVICE_ID;
                break;

            case 0x0001U:
                *p_value = MODBUS_FIRMWARE_VERSION;
                break;

            case 0x0002U:
                *p_value = modbus_slave_get_status_flags();
                break;

            case 0x0003U:
                *p_value = 0U;
                break;

            case 0x0004U:
                *p_value = ARM_SERVO_COUNT;
                break;

            case 0x0005U:
                *p_value = g_modbus_heartbeat_counter;
                break;

            case 0x0006U:
                *p_value = g_modbus_comm_error_count;
                break;

            case 0x0007U:
                *p_value = modbus_slave_get_control_mode();
                break;

            case 0x0008U:
                *p_value = MODBUS_PROTOCOL_VERSION;
                break;

            case 0x0009U:
                *p_value = g_modbus_last_command_seq;
                break;

            case 0x000AU:
                *p_value = g_modbus_last_command_code;
                break;

            case 0x000BU:
                *p_value = g_modbus_last_command_result;
                break;

            default:
                *p_value = 0U;
                break;
        }

        return true;
    }

    if ((address >= MODBUS_INPUT_AXIS_CURRENT_START) && (address <= MODBUS_INPUT_AXIS_CURRENT_END))
    {
        if (!modbus_slave_get_axis_value_from_us(MODBUS_INPUT_AXIS_CURRENT_START, address, &axis_value))
        {
            return false;
        }
        *p_value = modbus_slave_i16_to_reg(axis_value);
        return true;
    }

    if ((address >= MODBUS_INPUT_CURRENT_US_START) && (address <= MODBUS_INPUT_CURRENT_US_END))
    {
        *p_value = arm_control_get_current_us((uint8_t)(address - MODBUS_INPUT_CURRENT_US_START));
        return true;
    }

    if ((address >= MODBUS_INPUT_AXIS_TARGET_START) && (address <= MODBUS_INPUT_AXIS_TARGET_END))
    {
        if (!modbus_slave_get_axis_value_from_us(MODBUS_INPUT_AXIS_TARGET_START, address, &axis_value))
        {
            return false;
        }
        *p_value = modbus_slave_i16_to_reg(axis_value);
        return true;
    }

    if ((address >= MODBUS_INPUT_TARGET_US_START) && (address <= MODBUS_INPUT_TARGET_US_END))
    {
        *p_value = arm_control_get_target_us((uint8_t)(address - MODBUS_INPUT_TARGET_US_START));
        return true;
    }

    if ((address >= MODBUS_INPUT_ACTION_START) && (address <= MODBUS_INPUT_ACTION_END))
    {
        arm_action_get_info(&action_info);
        switch (address)
        {
            case 0x0060U:
                *p_value = action_info.slot_index;
                break;

            case 0x0061U:
                *p_value = action_info.step_count;
                break;

            case 0x0062U:
                *p_value = action_info.current_step;
                break;

            case 0x0063U:
                *p_value = (uint16_t)action_info.state;
                break;

            case 0x0064U:
                *p_value = action_info.playback_progress;
                break;

            default:
                return false;
        }

        return true;
    }

    if ((address >= MODBUS_INPUT_BASE_START) && (address <= MODBUS_INPUT_ACTION_END))
    {
        *p_value = 0U;
        return true;
    }

    if (MODBUS_INPUT_LAST_OBJECT_YAW == address)
    {
        *p_value = modbus_slave_i16_to_reg(g_modbus_last_object_yaw_x10);
        return true;
    }

    if (MODBUS_INPUT_LAST_TASK_PARAM_SEQ == address)
    {
        *p_value = g_modbus_last_task_param_seq;
        return true;
    }

    if (MODBUS_INPUT_TASK_PARAM_VALID == address)
    {
        *p_value = g_modbus_task_param_valid;
        return true;
    }

    return false;
}

static void modbus_slave_handle_read_input_registers(const uint8_t *p_frame, uint16_t len)
{
    uint16_t start;
    uint16_t count;
    uint16_t value;
    uint8_t response[MODBUS_TX_BUFFER_SIZE];
    uint16_t pos = 0;

    if (8U != len)
    {
        modbus_slave_send_exception(MODBUS_FUNC_READ_INPUT_REGS, MODBUS_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    start = modbus_slave_read_be16(&p_frame[2]);
    count = modbus_slave_read_be16(&p_frame[4]);

    if (!modbus_slave_input_range_is_valid(start, count) ||
        ((3U + (count * 2U) + 2U) > MODBUS_TX_BUFFER_SIZE))
    {
        modbus_slave_send_exception(MODBUS_FUNC_READ_INPUT_REGS, MODBUS_EX_ILLEGAL_DATA_ADDRESS);
        return;
    }

    response[pos++] = MODBUS_SLAVE_ADDRESS;
    response[pos++] = MODBUS_FUNC_READ_INPUT_REGS;
    response[pos++] = (uint8_t)(count * 2U);

    for (uint16_t i = 0; i < count; i++)
    {
        (void)modbus_slave_read_input_register((uint16_t)(start + i), &value);
        modbus_slave_write_be16(&response[pos], value);
        pos = (uint16_t)(pos + 2U);
    }

    (void)modbus_slave_send_response(response, pos);
}

static void modbus_slave_handle_write_single_register(const uint8_t *p_frame, uint16_t len)
{
    uint16_t address;
    uint16_t value;
    uint16_t result;
    uint8_t response[8];

    if (8U != len)
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_SINGLE_REG, MODBUS_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    address = modbus_slave_read_be16(&p_frame[2]);
    value = modbus_slave_read_be16(&p_frame[4]);

    if (MODBUS_HOLD_CONTROL_COMMAND != address)
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_SINGLE_REG, MODBUS_EX_ILLEGAL_DATA_ADDRESS);
        return;
    }

    g_modbus_control_command = value;
    g_modbus_target_apply = MODBUS_APPLY_NONE;

    if ((MODBUS_COMMAND_ESTOP == value) || (MODBUS_COMMAND_CLEAR_ESTOP == value))
    {
        result = modbus_slave_execute_control_command(value);
        modbus_slave_finish_command(g_modbus_command_seq, value, result);
    }
    else
    {
        modbus_slave_finish_command(g_modbus_command_seq, value, MODBUS_COMMAND_RESULT_INVALID_PARAM);
    }

    g_modbus_control_command = 0U;
    g_modbus_target_apply = 0U;

    memcpy(response, p_frame, 6U);
    (void)modbus_slave_send_response(response, 6U);
}

static void modbus_slave_handle_write_multiple_registers(const uint8_t *p_frame, uint16_t len)
{
    uint16_t start;
    uint16_t count;
    uint8_t byte_count;
    uint16_t values[MODBUS_MAX_REG_COUNT];
    uint8_t response[8];

    if (len < 11U)
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_MULTIPLE_REGS, MODBUS_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    start = modbus_slave_read_be16(&p_frame[2]);
    count = modbus_slave_read_be16(&p_frame[4]);
    byte_count = p_frame[6];

    if ((0U == count) ||
        (count > MODBUS_MAX_REG_COUNT) ||
        (byte_count != (uint8_t)(count * 2U)) ||
        (len != (uint16_t)(9U + byte_count)))
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_MULTIPLE_REGS, MODBUS_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        values[i] = modbus_slave_read_be16(&p_frame[7U + (i * 2U)]);
    }

    if (!modbus_slave_multiple_write_range_is_supported(start, count))
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_MULTIPLE_REGS, MODBUS_EX_ILLEGAL_DATA_ADDRESS);
        return;
    }

    if (!modbus_slave_validate_multiple_write(start, count, values))
    {
        modbus_slave_send_exception(MODBUS_FUNC_WRITE_MULTIPLE_REGS, MODBUS_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    modbus_slave_apply_multiple_write(start, count, values);

    response[0] = MODBUS_SLAVE_ADDRESS;
    response[1] = MODBUS_FUNC_WRITE_MULTIPLE_REGS;
    modbus_slave_write_be16(&response[2], start);
    modbus_slave_write_be16(&response[4], count);
    (void)modbus_slave_send_response(response, 6U);
}

static bool modbus_slave_write_shadow(uint16_t address, uint16_t value)
{
    if (MODBUS_HOLD_CONTROL_COMMAND == address)
    {
        g_modbus_control_command = value;
        return true;
    }

    if (MODBUS_HOLD_TARGET_APPLY == address)
    {
        g_modbus_target_apply = value;
        return true;
    }

    if (MODBUS_HOLD_COMMAND_SEQ == address)
    {
        g_modbus_command_seq = value;
        return true;
    }

    if ((address >= MODBUS_HOLD_AXIS_TARGET_START) && (address <= MODBUS_HOLD_AXIS_TARGET_END))
    {
        g_modbus_axis_target[address - MODBUS_HOLD_AXIS_TARGET_START] = modbus_slave_reg_to_i16(value);
        return true;
    }

    if ((address >= MODBUS_HOLD_RAW_US_START) && (address <= MODBUS_HOLD_RAW_US_END))
    {
        g_modbus_raw_us_target[address - MODBUS_HOLD_RAW_US_START] = value;
        return true;
    }

    if (MODBUS_HOLD_OBJECT_YAW == address)
    {
        g_modbus_object_yaw_x10 = modbus_slave_reg_to_i16(value);
        return true;
    }

    if (MODBUS_HOLD_TASK_PARAM_SEQ == address)
    {
        g_modbus_task_param_seq_shadow = value;
        return true;
    }

    return false;
}

static bool modbus_slave_multiple_write_range_is_supported(uint16_t start, uint16_t count)
{
    if ((MODBUS_HOLD_CONTROL_COMMAND == start) && (3U == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_TARGET_APPLY == start) && (2U == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_AXIS_TARGET_START == start) && (ARM_SERVO_COUNT == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_RAW_US_START == start) && (ARM_SERVO_COUNT == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_OBJECT_YAW == start) && (2U == count))
    {
        return true;
    }

    return false;
}

static bool modbus_slave_validate_multiple_write(uint16_t start, uint16_t count, const uint16_t *p_values)
{
    int16_t axis_shadow[ARM_SERVO_COUNT];
    uint16_t raw_us_shadow[ARM_SERVO_COUNT];
    uint16_t target_us[ARM_SERVO_COUNT];

    if (NULL == p_values)
    {
        return false;
    }

    if ((MODBUS_HOLD_CONTROL_COMMAND == start) && (3U == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_TARGET_APPLY == start) && (2U == count))
    {
        return true;
    }

    if ((MODBUS_HOLD_AXIS_TARGET_START == start) && (ARM_SERVO_COUNT == count))
    {
        for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
        {
            axis_shadow[i] = modbus_slave_reg_to_i16(p_values[i]);
        }

        return modbus_slave_axis_values_to_pwm(axis_shadow, target_us);
    }

    if ((MODBUS_HOLD_RAW_US_START == start) && (ARM_SERVO_COUNT == count))
    {
        for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
        {
            raw_us_shadow[i] = p_values[i];
        }

        return modbus_slave_raw_us_values_are_valid(raw_us_shadow);
    }

    if ((MODBUS_HOLD_OBJECT_YAW == start) && (2U == count))
    {
        return true;   /* yaw 任意 int16、seq 任意 uint16，无需额外校验 */
    }

    return false;
}

static void modbus_slave_apply_multiple_write(uint16_t start, uint16_t count, const uint16_t *p_values)
{
    for (uint16_t i = 0; i < count; i++)
    {
        (void)modbus_slave_write_shadow((uint16_t)(start + i), p_values[i]);
    }

    if (((MODBUS_HOLD_CONTROL_COMMAND == start) && (3U == count)) ||
        ((MODBUS_HOLD_TARGET_APPLY == start) && (2U == count)))
    {
        modbus_slave_process_control_shadow();
    }
}

static void modbus_slave_process_control_shadow(void)
{
    uint16_t command_code;
    uint16_t result;

    command_code = modbus_slave_make_command_code(g_modbus_control_command, g_modbus_target_apply);
    if (0U == command_code)
    {
        if (0U != g_modbus_target_apply)
        {
            modbus_slave_finish_command(g_modbus_command_seq,
                                        g_modbus_target_apply,
                                        MODBUS_COMMAND_RESULT_INVALID_PARAM);
        }

        g_modbus_control_command = 0U;
        g_modbus_target_apply = 0U;
        return;
    }

    if ((0U != g_modbus_control_command) && (0U != g_modbus_target_apply))
    {
        modbus_slave_finish_command(g_modbus_command_seq,
                                    command_code,
                                    MODBUS_COMMAND_RESULT_INVALID_PARAM);
        g_modbus_control_command = 0U;
        g_modbus_target_apply = 0U;
        return;
    }

    if (g_modbus_has_last_command &&
        (g_modbus_command_seq == g_modbus_last_command_seq) &&
        (command_code == g_modbus_last_command_code))
    {
        modbus_slave_finish_command(g_modbus_command_seq,
                                    command_code,
                                    MODBUS_COMMAND_RESULT_DUPLICATE);
        g_modbus_control_command = 0U;
        g_modbus_target_apply = 0U;
        return;
    }

    if (0U != g_modbus_control_command)
    {
        result = modbus_slave_execute_control_command(g_modbus_control_command);
    }
    else
    {
        result = modbus_slave_execute_target_apply(g_modbus_target_apply);
    }

    modbus_slave_finish_command(g_modbus_command_seq, command_code, result);
    g_modbus_control_command = 0U;
    g_modbus_target_apply = 0U;
}

static uint16_t modbus_slave_make_command_code(uint16_t control_command, uint16_t target_apply)
{
    if (0U != control_command)
    {
        return control_command;
    }

    if (MODBUS_APPLY_AXIS_TARGET == target_apply)
    {
        return MODBUS_COMMAND_CODE_APPLY_AXIS;
    }

    if (MODBUS_APPLY_RAW_US == target_apply)
    {
        return MODBUS_COMMAND_CODE_APPLY_RAW_US;
    }

    return 0U;
}

static void modbus_slave_finish_command(uint16_t seq, uint16_t code, uint16_t result)
{
    g_modbus_last_command_seq = seq;
    g_modbus_last_command_code = code;
    g_modbus_last_command_result = result;
    g_modbus_has_last_command = true;

    my_printf(&g_debug_uart,
              "MODBUS cmd seq=%u code=0x%04X result=%u\r\n",
              (unsigned int)seq,
              (unsigned int)code,
              (unsigned int)result);
}

static uint16_t modbus_slave_execute_control_command(uint16_t command)
{
    arm_action_info_t action_info;
    fsp_err_t err;

    if (MODBUS_COMMAND_ESTOP == command)
    {
        g_modbus_emergency_stop = true;
        (void)arm_action_stop();
        (void)arm_manual_set_page_active(false);
        return MODBUS_COMMAND_RESULT_ACCEPTED;
    }

    if (MODBUS_COMMAND_CLEAR_ESTOP == command)
    {
        g_modbus_emergency_stop = false;
        return MODBUS_COMMAND_RESULT_ACCEPTED;
    }

    if (g_modbus_emergency_stop)
    {
        return MODBUS_COMMAND_RESULT_ESTOP;
    }

    if (arm_manual_is_page_active())
    {
        return MODBUS_COMMAND_RESULT_HMI_ACTIVE;
    }

    arm_action_get_info(&action_info);

    if ((MODBUS_COMMAND_STOP_PLAY != command) &&
        action_info.playing)
    {
        return MODBUS_COMMAND_RESULT_BUSY;
    }

    switch (command)
    {
        case MODBUS_COMMAND_RECORD_START:
        case MODBUS_COMMAND_RECORD_STOP:
            return MODBUS_COMMAND_RESULT_UNSUPPORTED;

        case MODBUS_COMMAND_SAVE_ACTION:
            err = arm_action_save_to_flash();
            break;

        case MODBUS_COMMAND_PLAY:
            if (0U == action_info.step_count)
            {
                return MODBUS_COMMAND_RESULT_ACTION_EMPTY;
            }
            {
                /* NG 角度绑定：仅当 task_param_seq 与本次 PLAY 的 seq 一致才锁存角度，
                 * 否则按不补偿(yaw=0)安全降级，杜绝上一次角度残留。 */
                bool param_valid = (g_modbus_task_param_seq_shadow == g_modbus_command_seq);
                int16_t yaw_x10 = param_valid ? g_modbus_object_yaw_x10 : (int16_t)0;

                g_modbus_last_object_yaw_x10 = yaw_x10;
                g_modbus_last_task_param_seq = g_modbus_command_seq;
                g_modbus_task_param_valid = param_valid ? 1U : 0U;

                err = arm_action_play_ng(yaw_x10, param_valid);
            }
            break;

        case MODBUS_COMMAND_STOP_PLAY:
            err = arm_action_stop();
            break;

        case MODBUS_COMMAND_CLEAR_ACTION:
            err = arm_action_clear();
            break;

        case MODBUS_COMMAND_HOME:
            err = arm_action_home(ARM_ACTION_HOME_DURATION_MS, ARM_CONTROL_SOURCE_MODBUS);
            break;

        default:
            return MODBUS_COMMAND_RESULT_INVALID_PARAM;
    }

    return (FSP_SUCCESS == err) ? MODBUS_COMMAND_RESULT_ACCEPTED : MODBUS_COMMAND_RESULT_FAILED;
}

static uint16_t modbus_slave_execute_target_apply(uint16_t target_apply)
{
    arm_action_info_t action_info;
    uint16_t target_us[ARM_SERVO_COUNT];
    fsp_err_t err;

    if (g_modbus_emergency_stop)
    {
        return MODBUS_COMMAND_RESULT_ESTOP;
    }

    if (arm_manual_is_page_active())
    {
        return MODBUS_COMMAND_RESULT_HMI_ACTIVE;
    }

    arm_action_get_info(&action_info);
    if (action_info.playing)
    {
        return MODBUS_COMMAND_RESULT_BUSY;
    }

    if (MODBUS_APPLY_AXIS_TARGET == target_apply)
    {
        if (!modbus_slave_targets_from_axis(target_us))
        {
            return MODBUS_COMMAND_RESULT_INVALID_PARAM;
        }
    }
    else if (MODBUS_APPLY_RAW_US == target_apply)
    {
        if (!modbus_slave_targets_from_raw_us(target_us))
        {
            return MODBUS_COMMAND_RESULT_INVALID_PARAM;
        }
    }
    else
    {
        return MODBUS_COMMAND_RESULT_INVALID_PARAM;
    }

    err = arm_action_move_to_targets(target_us,
                                     ARM_ACTION_MOVE_DURATION_MS,
                                     ARM_CONTROL_SOURCE_MODBUS);

    return (FSP_SUCCESS == err) ? MODBUS_COMMAND_RESULT_ACCEPTED : MODBUS_COMMAND_RESULT_FAILED;
}

static bool modbus_slave_targets_from_axis(uint16_t target_us[ARM_SERVO_COUNT])
{
    return modbus_slave_axis_values_to_pwm(g_modbus_axis_target, target_us);
}

static bool modbus_slave_targets_from_raw_us(uint16_t target_us[ARM_SERVO_COUNT])
{
    if (!modbus_slave_raw_us_values_are_valid(g_modbus_raw_us_target))
    {
        return false;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        target_us[i] = g_modbus_raw_us_target[i];
    }

    return true;
}

static bool modbus_slave_axis_values_to_pwm(const int16_t axis_value[ARM_SERVO_COUNT],
                                            uint16_t target_us[ARM_SERVO_COUNT])
{
    arm_kinematics_joint_t joints;

    if ((NULL == axis_value) || (NULL == target_us))
    {
        return false;
    }

    joints.base_deg = ((float)axis_value[0]) / 10.0f;
    joints.shoulder_deg = ((float)axis_value[1]) / 10.0f;
    joints.elbow_deg = ((float)axis_value[2]) / 10.0f;
    joints.pitch_deg = ((float)axis_value[3]) / 10.0f;
    joints.roll_deg = ((float)axis_value[4]) / 10.0f;
    joints.grip_open_percent = ((float)axis_value[5]) / 10.0f;

    return (FSP_SUCCESS == arm_kinematics_joints_to_pwm(&joints, target_us));
}

static bool modbus_slave_raw_us_values_are_valid(const uint16_t raw_us[ARM_SERVO_COUNT])
{
    if (NULL == raw_us)
    {
        return false;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if ((raw_us[i] < arm_control_get_min_us(i)) ||
            (raw_us[i] > arm_control_get_max_us(i)))
        {
            return false;
        }
    }

    return true;
}

static bool modbus_slave_get_axis_value_from_us(uint16_t base_address,
                                                uint16_t address,
                                                int16_t *p_value)
{
    uint16_t pulse_us[ARM_SERVO_COUNT];
    arm_kinematics_joint_t joints;
    uint8_t index;

    if (NULL == p_value)
    {
        return false;
    }

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (MODBUS_INPUT_AXIS_CURRENT_START == base_address)
        {
            pulse_us[i] = arm_control_get_current_us(i);
        }
        else
        {
            pulse_us[i] = arm_control_get_target_us(i);
        }
    }

    if (FSP_SUCCESS != arm_kinematics_pwm_to_joints(pulse_us, &joints))
    {
        return false;
    }

    index = (uint8_t)(address - base_address);
    switch (index)
    {
        case 0:
            *p_value = modbus_slave_float_to_x10(joints.base_deg);
            break;

        case 1:
            *p_value = modbus_slave_float_to_x10(joints.shoulder_deg);
            break;

        case 2:
            *p_value = modbus_slave_float_to_x10(joints.elbow_deg);
            break;

        case 3:
            *p_value = modbus_slave_float_to_x10(joints.pitch_deg);
            break;

        case 4:
            *p_value = modbus_slave_float_to_x10(joints.roll_deg);
            break;

        case 5:
            *p_value = modbus_slave_float_to_x10(joints.grip_open_percent);
            break;

        default:
            return false;
    }

    return true;
}

static int16_t modbus_slave_float_to_x10(float value)
{
    float scaled = value * 10.0f;

    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }

    if (scaled > 32767.0f)
    {
        return 32767;
    }

    if (scaled < -32768.0f)
    {
        return -32768;
    }

    return (int16_t)scaled;
}

static uint16_t modbus_slave_i16_to_reg(int16_t value)
{
    return (uint16_t)value;
}

static int16_t modbus_slave_reg_to_i16(uint16_t value)
{
    if (0U != (value & 0x8000U))
    {
        return (int16_t)((int32_t)value - 65536L);
    }

    return (int16_t)value;
}

static uint16_t modbus_slave_get_status_flags(void)
{
    uint16_t flags = 0;
    arm_action_info_t action_info;
    arm_manual_info_t manual_info;
    bool moving;
    bool error;

    arm_action_get_info(&action_info);
    arm_manual_get_info(&manual_info);

    moving = action_info.playing || manual_info.moving || modbus_slave_is_target_moving();
    error = (!arm_control_is_ready()) || (ARM_ACTION_STATE_ERROR == action_info.state);

    if (arm_control_is_ready() && !moving && !error && !g_modbus_emergency_stop && !manual_info.page_active)
    {
        flags |= (1U << 0);
    }

    if (arm_control_is_ready())
    {
        flags |= (1U << 1);
    }

    if (moving)
    {
        flags |= (1U << 2);
    }

    if (modbus_slave_is_homed())
    {
        flags |= (1U << 3);
    }

    if (manual_info.page_active || manual_info.moving)
    {
        flags |= (1U << 4);
    }

    if (modbus_slave_is_remote_active())
    {
        flags |= (1U << 5);
    }

    if (error)
    {
        flags |= (1U << 6);
    }

    if (g_modbus_emergency_stop)
    {
        flags |= (1U << 7);
    }

    if (ARM_ACTION_STATE_RECORD == action_info.state)
    {
        flags |= (1U << 8);
    }

    if (action_info.playing)
    {
        flags |= (1U << 9);
    }

    return flags;
}

static uint16_t modbus_slave_get_control_mode(void)
{
    arm_action_info_t action_info;
    arm_manual_info_t manual_info;

    if (g_modbus_emergency_stop)
    {
        return MODBUS_MODE_ESTOP;
    }

    arm_action_get_info(&action_info);
    if (ARM_ACTION_STATE_RECORD == action_info.state)
    {
        return MODBUS_MODE_RECORD;
    }

    if (action_info.playing || (ARM_ACTION_STATE_PLAYING == action_info.state))
    {
        return MODBUS_MODE_PLAYBACK;
    }

    arm_manual_get_info(&manual_info);
    if (manual_info.page_active || manual_info.moving)
    {
        return MODBUS_MODE_MANUAL;
    }

    return MODBUS_MODE_IDLE;
}

static bool modbus_slave_is_remote_active(void)
{
    if (0U == g_modbus_last_activity_tick)
    {
        return false;
    }

    return ((uint32_t)(g_tick - g_modbus_last_activity_tick) <= MODBUS_REMOTE_ACTIVE_TIMEOUT_MS);
}

static bool modbus_slave_is_homed(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (arm_control_get_target_us(i) != arm_control_get_home_us(i))
        {
            return false;
        }
    }

    return true;
}

static bool modbus_slave_is_target_moving(void)
{
    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (arm_control_get_target_us(i) != arm_control_get_current_us(i))
        {
            return true;
        }
    }

    return false;
}

static void modbus_slave_refresh_target_shadow_from_control(void)
{
    int16_t axis_value;

    for (uint8_t i = 0; i < ARM_SERVO_COUNT; i++)
    {
        if (modbus_slave_get_axis_value_from_us(MODBUS_INPUT_AXIS_TARGET_START,
                                                (uint16_t)(MODBUS_INPUT_AXIS_TARGET_START + i),
                                                &axis_value))
        {
            g_modbus_axis_target[i] = axis_value;
        }
        else
        {
            g_modbus_axis_target[i] = 0;
        }

        g_modbus_raw_us_target[i] = arm_control_get_target_us(i);
    }
}

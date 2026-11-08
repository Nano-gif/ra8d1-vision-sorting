#include "storage_internal.h"

static void storage_format_x10(char *buffer, uint32_t buffer_size, uint16_t value_x10)
{
    if ((buffer == RT_NULL) || (buffer_size == 0U))
    {
        return;
    }

    rt_snprintf(buffer, buffer_size, "%u.%u", value_x10 / 10U, value_x10 % 10U);
}

static rt_bool_t storage_csv_field_is_safe(const char *text)
{
    const char *cursor = text;

    if (cursor == RT_NULL)
    {
        return RT_TRUE;
    }

    while (*cursor != '\0')
    {
        if ((*cursor == ',') || (*cursor == '\r') || (*cursor == '\n') || (*cursor == '"'))
        {
            return RT_FALSE;
        }

        cursor++;
    }

    return RT_TRUE;
}

static void storage_copy_field(char *dst, uint32_t dst_size, const char *src, uint32_t src_len)
{
    uint32_t copy_len;

    if ((dst == RT_NULL) || (dst_size == 0U))
    {
        return;
    }

    if (src == RT_NULL)
    {
        dst[0] = '\0';
        return;
    }

    copy_len = src_len;
    if (copy_len >= dst_size)
    {
        copy_len = dst_size - 1U;
    }

    rt_memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static uint16_t storage_parse_x10(const char *text)
{
    uint32_t whole = 0U;
    uint32_t frac = 0U;
    const char *cursor = text;

    if (cursor == RT_NULL)
    {
        return 0U;
    }

    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        whole = whole * 10U + (uint32_t)(*cursor - '0');
        cursor++;
    }

    if (*cursor == '.')
    {
        cursor++;
        if ((*cursor >= '0') && (*cursor <= '9'))
        {
            frac = (uint32_t)(*cursor - '0');
        }
    }

    if (whole > 6553U)
    {
        return 65535U;
    }

    return (uint16_t)(whole * 10U + frac);
}

static uint16_t storage_parse_u16(const char *text)
{
    uint32_t value = 0U;
    const char *cursor = text;

    if (cursor == RT_NULL)
    {
        return 0U;
    }

    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        value = value * 10U + (uint32_t)(*cursor - '0');
        if (value > 65535U)
        {
            return 65535U;
        }
        cursor++;
    }

    return (uint16_t)value;
}

static int storage_parse_detect_record_line(const char *line, storage_detect_record_item_t *record)
{
    const char *field_start;
    const char *cursor;
    char field[STORAGE_PATH_MAX];
    uint8_t field_index = 0U;

    if ((line == RT_NULL) || (record == RT_NULL))
    {
        return -1;
    }

    rt_memset(record, 0, sizeof(*record));

    field_start = line;
    cursor = line;

    while (1)
    {
        if ((*cursor == ',') || (*cursor == '\r') || (*cursor == '\n') || (*cursor == '\0'))
        {
            uint32_t field_len = (uint32_t)(cursor - field_start);
            storage_copy_field(field, sizeof(field), field_start, field_len);

            switch (field_index)
            {
            case 0:
                storage_copy_path(record->timestamp, sizeof(record->timestamp), field);
                break;
            case 1:
                storage_copy_path(record->model, sizeof(record->model), field);
                break;
            case 2:
                record->anomaly = (rt_strcmp(field, "NG") == 0) ? RT_TRUE : RT_FALSE;
                break;
            case 3:
                record->score_x10 = storage_parse_x10(field);
                break;
            case 4:
                record->threshold_x10 = storage_parse_x10(field);
                break;
            case 5:
                record->duration_ms = storage_parse_u16(field);
                break;
            case 6:
                storage_copy_path(record->image_path, sizeof(record->image_path), field);
                break;
            default:
                break;
            }

            field_index++;

            if ((*cursor == '\r') || (*cursor == '\n') || (*cursor == '\0'))
            {
                break;
            }

            field_start = cursor + 1;
        }

        cursor++;
    }

    return (field_index >= 6U) ? 0 : -1;
}

static void storage_recent_records_push(storage_detect_record_item_t *records,
                                        uint16_t max_records,
                                        const storage_detect_record_item_t *item)
{
    uint16_t i;

    if ((records == RT_NULL) || (item == RT_NULL) || (max_records == 0U))
    {
        return;
    }

    for (i = max_records - 1U; i > 0U; i--)
    {
        records[i] = records[i - 1U];
    }

    records[0] = *item;
}

int storage_app_append_detect_record(const storage_detect_record_t *record)
{
    char score[12];
    char threshold[12];
    char line[STORAGE_RECORD_LINE_MAX];
    const char *timestamp;
    const char *model;
    const char *image_path;
    int fd;
    int len;

    if (record == RT_NULL)
    {
        g_storage_last_error = "invalid detect record";
        return -RT_ERROR;
    }

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    timestamp = (record->timestamp != RT_NULL) ? record->timestamp : "";
    model = (record->model != RT_NULL) ? record->model : "";
    image_path = (record->image_path != RT_NULL) ? record->image_path : "";

    if ((storage_csv_field_is_safe(timestamp) == RT_FALSE) ||
        (storage_csv_field_is_safe(model) == RT_FALSE) ||
        (storage_csv_field_is_safe(image_path) == RT_FALSE))
    {
        g_storage_last_error = "csv field contains unsupported char";
        return -RT_ERROR;
    }

    storage_format_x10(score, sizeof(score), record->score_x10);
    storage_format_x10(threshold, sizeof(threshold), record->threshold_x10);

    len = rt_snprintf(line,
                      sizeof(line),
                      "%s,%s,%s,%s,%s,%u,%s\r\n",
                      timestamp,
                      model,
                      (record->anomaly == RT_TRUE) ? "NG" : "OK",
                      score,
                      threshold,
                      (unsigned int)record->duration_ms,
                      image_path);
    if ((len <= 0) || ((uint32_t)len >= sizeof(line)))
    {
        g_storage_last_error = "format detect record failed";
        return -RT_ERROR;
    }

    fd = open(STORAGE_RECORDS_FILE, O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0)
    {
        g_storage_last_error = "open records.csv failed";
        return -RT_ERROR;
    }

    if (storage_write_all(fd, line, (uint32_t)len) != 0)
    {
        close(fd);
        g_storage_last_error = "write records.csv failed";
        return -RT_ERROR;
    }

    close(fd);
    g_storage_last_error = "ok";
    storage_notify_state_changed();
    return RT_EOK;
}

int storage_app_load_recent_detect_records(storage_detect_record_item_t *records,
                                           uint16_t max_records,
                                           uint16_t *out_count)
{
    int fd;
    int read_len;
    uint16_t count = 0U;
    char ch;
    char line[STORAGE_RECORD_LINE_MAX];
    uint16_t line_len = 0U;
    rt_bool_t header_skipped = RT_FALSE;

    if (out_count != RT_NULL)
    {
        *out_count = 0U;
    }

    if ((records == RT_NULL) || (max_records == 0U))
    {
        g_storage_last_error = "invalid recent record buffer";
        return -RT_ERROR;
    }

    rt_memset(records, 0, sizeof(storage_detect_record_item_t) * max_records);

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    fd = open(STORAGE_RECORDS_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        g_storage_last_error = "open records.csv failed";
        return -RT_ERROR;
    }

    while ((read_len = read(fd, &ch, 1U)) == 1)
    {
        if ((ch == '\n') || (line_len >= (sizeof(line) - 1U)))
        {
            line[line_len] = '\0';
            if (header_skipped == RT_FALSE)
            {
                header_skipped = RT_TRUE;
            }
            else if (line_len > 0U)
            {
                storage_detect_record_item_t item;
                if (storage_parse_detect_record_line(line, &item) == 0)
                {
                    storage_recent_records_push(records, max_records, &item);
                    if (count < max_records)
                    {
                        count++;
                    }
                }
            }

            line_len = 0U;
        }
        else if (ch != '\r')
        {
            line[line_len++] = ch;
        }
    }

    if (line_len > 0U)
    {
        line[line_len] = '\0';
        if (header_skipped == RT_TRUE)
        {
            storage_detect_record_item_t item;
            if (storage_parse_detect_record_line(line, &item) == 0)
            {
                storage_recent_records_push(records, max_records, &item);
                if (count < max_records)
                {
                    count++;
                }
            }
        }
    }

    close(fd);

    if (out_count != RT_NULL)
    {
        *out_count = count;
    }

    g_storage_last_error = "ok";
    return RT_EOK;
}

int storage_app_export_records_csv(char *out_path, uint32_t out_path_size)
{
    char export_path[STORAGE_PATH_MAX];

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    if (storage_mkdir_if_missing(STORAGE_EXPORT_DIR) != 0)
    {
        g_storage_last_error = "create export dir failed";
        return -RT_ERROR;
    }

    for (uint16_t index = 1U; index <= STORAGE_EXPORT_INDEX_MAX; index++)
    {
        int len = rt_snprintf(export_path,
                              sizeof(export_path),
                              "%s/records_%03u.csv",
                              STORAGE_EXPORT_DIR,
                              (unsigned int)index);
        if ((len <= 0) || ((uint32_t)len >= sizeof(export_path)))
        {
            g_storage_last_error = "format export path failed";
            return -RT_ERROR;
        }

        if (storage_path_exists(export_path))
        {
            continue;
        }

        if (storage_copy_file(STORAGE_RECORDS_FILE, export_path) != 0)
        {
            (void)unlink(export_path);
            g_storage_last_error = "copy records export failed";
            return -RT_ERROR;
        }

        storage_copy_path(out_path, out_path_size, export_path);
        g_storage_last_error = "ok";
        storage_notify_state_changed();
        return RT_EOK;
    }

    g_storage_last_error = "too many exported records";
    return -RT_ERROR;
}

int storage_app_clear_detect_records(void)
{
    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    if (storage_write_text_file_replace(STORAGE_RECORDS_FILE, g_records_header) != 0)
    {
        g_storage_last_error = "clear records.csv failed";
        return -RT_ERROR;
    }

    g_storage_last_error = "ok";
    storage_notify_state_changed();
    return RT_EOK;
}

int storage_app_reset_project_storage(void)
{
    g_storage_ready = RT_FALSE;
    storage_collect_reset_state();

    if (rt_device_find("sd") == RT_NULL)
    {
        g_storage_last_error = "sd block device not found";
        storage_notify_state_changed();
        return -RT_ERROR;
    }

    if (storage_delete_tree(STORAGE_ROOT_DIR, RT_TRUE) != 0)
    {
        g_storage_last_error = "delete /vision failed";
        storage_notify_state_changed();
        return -RT_ERROR;
    }

    if (storage_app_init() != RT_EOK)
    {
        storage_notify_state_changed();
        return -RT_ERROR;
    }

    g_storage_last_error = "ok";
    storage_notify_state_changed();
    return RT_EOK;
}

int storage_app_get_record_stats(storage_app_record_stats_t *stats)
{
    int fd;
    int read_len;
    char ch;
    char line[STORAGE_RECORD_LINE_MAX];
    uint16_t line_len = 0U;
    rt_bool_t header_skipped = RT_FALSE;

    if (stats == RT_NULL)
    {
        g_storage_last_error = "invalid stats buffer";
        return -RT_ERROR;
    }

    rt_memset(stats, 0, sizeof(*stats));

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    stats->sample_count = storage_count_collect_samples();

    fd = open(STORAGE_RECORDS_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        g_storage_last_error = "open records.csv failed";
        return -RT_ERROR;
    }

    while ((read_len = read(fd, &ch, 1U)) == 1)
    {
        if ((ch == '\n') || (line_len >= (sizeof(line) - 1U)))
        {
            line[line_len] = '\0';
            if (header_skipped == RT_FALSE)
            {
                header_skipped = RT_TRUE;
            }
            else if (line_len > 0U)
            {
                storage_detect_record_item_t item;
                if (storage_parse_detect_record_line(line, &item) == 0)
                {
                    if (stats->total_records < 65535U)
                    {
                        stats->total_records++;
                    }

                    if ((item.anomaly == RT_TRUE) && (stats->ng_count < 65535U))
                    {
                        stats->ng_count++;
                    }

                    if (stats->trend_count < (sizeof(stats->trend_score_x10) / sizeof(stats->trend_score_x10[0])))
                    {
                        for (uint16_t i = stats->trend_count; i > 0U; i--)
                        {
                            stats->trend_score_x10[i] = stats->trend_score_x10[i - 1U];
                        }
                        stats->trend_score_x10[0] = item.score_x10;
                        stats->trend_count++;
                    }
                    else
                    {
                        for (uint16_t i = stats->trend_count - 1U; i > 0U; i--)
                        {
                            stats->trend_score_x10[i] = stats->trend_score_x10[i - 1U];
                        }
                        stats->trend_score_x10[0] = item.score_x10;
                    }
                }
            }

            line_len = 0U;
        }
        else if (ch != '\r')
        {
            line[line_len++] = ch;
        }
    }

    if (line_len > 0U)
    {
        line[line_len] = '\0';
        if (header_skipped == RT_TRUE)
        {
            storage_detect_record_item_t item;
            if (storage_parse_detect_record_line(line, &item) == 0)
            {
                if (stats->total_records < 65535U)
                {
                    stats->total_records++;
                }

                if ((item.anomaly == RT_TRUE) && (stats->ng_count < 65535U))
                {
                    stats->ng_count++;
                }

                if (stats->trend_count < (sizeof(stats->trend_score_x10) / sizeof(stats->trend_score_x10[0])))
                {
                    for (uint16_t i = stats->trend_count; i > 0U; i--)
                    {
                        stats->trend_score_x10[i] = stats->trend_score_x10[i - 1U];
                    }
                    stats->trend_score_x10[0] = item.score_x10;
                    stats->trend_count++;
                }
                else
                {
                    for (uint16_t i = stats->trend_count - 1U; i > 0U; i--)
                    {
                        stats->trend_score_x10[i] = stats->trend_score_x10[i - 1U];
                    }
                    stats->trend_score_x10[0] = item.score_x10;
                }
            }
        }
    }

    close(fd);
    g_storage_last_error = "ok";
    return RT_EOK;
}

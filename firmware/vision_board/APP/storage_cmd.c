#include "storage_internal.h"

#ifdef FINSH_USING_MSH
/*
 * MSH 命令: storage_init_cmd
 *
 * 形参:
 * - argc: MSH 传入的参数数量，未使用。
 * - argv: MSH 传入的参数字符串数组，未使用。
 *
 * 返回值:
 * - 0: 初始化成功。
 * - -1: 初始化失败。
 *
 * 使用方式:
 * 在 msh 中输入 storage_init_cmd。
 */
static int storage_init_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (storage_app_init() == RT_EOK)
    {
        rt_kprintf("[storage] init ok\r\n");
        return 0;
    }

    rt_kprintf("[storage] init failed: %s\r\n", storage_app_last_error());
    return -1;
}

/*
 * MSH 命令: storage_test_cmd
 *
 * 形参:
 * - argc: MSH 传入的参数数量，未使用。
 * - argv: MSH 传入的参数字符串数组，未使用。
 *
 * 返回值:
 * - 0: 测试文件写入成功。
 * - -1: 初始化失败或写入失败。
 *
 * 使用方式:
 * 在 msh 中输入 storage_test_cmd。
 */
static int storage_test_cmd(int argc, char **argv)
{
    static const char test_text[] = "storage write test ok\r\n";
    int fd;
    int ret;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (!storage_app_is_ready())
    {
        ret = storage_app_init();
        if (ret != RT_EOK)
        {
            rt_kprintf("[storage] init failed: %s\r\n", storage_app_last_error());
            return -1;
        }
    }

    fd = open(STORAGE_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        rt_kprintf("[storage] open test file failed\r\n");
        return -1;
    }

    ret = write(fd, test_text, rt_strlen(test_text));
    close(fd);

    if (ret == (int)rt_strlen(test_text))
    {
        rt_kprintf("[storage] write test ok: %s\r\n", STORAGE_TEST_FILE);
        return 0;
    }

    rt_kprintf("[storage] write test failed\r\n");
    return -1;
}

/*
 * MSH 命令: storage_collect_begin_cmd
 *
 * 形参:
 * - argc: MSH 参数个数，未使用。
 * - argv: MSH 参数数组，未使用。
 *
 * 返回值:
 * - 0: 会话已创建或已处于活动状态。
 * - -1: 会话创建失败。
 */
static int storage_collect_begin_cmd(int argc, char **argv)
{
    char session_dir[STORAGE_PATH_MAX];

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (storage_app_collect_begin_session(session_dir, sizeof(session_dir)) == RT_EOK)
    {
        rt_kprintf("[storage] collect session: %s\r\n", session_dir);
        return 0;
    }

    rt_kprintf("[storage] collect begin failed: %s\r\n", storage_app_last_error());
    return -1;
}

/*
 * MSH 命令: storage_collect_end_cmd
 *
 * 形参:
 * - argc: MSH 参数个数，未使用。
 * - argv: MSH 参数数组，未使用。
 *
 * 返回值:
 * - 0: 会话状态已清除。
 */
static int storage_collect_end_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    storage_app_collect_end_session();
    rt_kprintf("[storage] collect session ended\r\n");
    return 0;
}

static int storage_detect_record_test_cmd(int argc, char **argv)
{
    static uint32_t s_test_index = 1U;
    storage_detect_record_t record;
    char timestamp[16];

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_snprintf(timestamp, sizeof(timestamp), "T%06lu", (unsigned long)s_test_index);

    record.timestamp = timestamp;
    record.model = "cap_v8_96";
    record.anomaly = (s_test_index & 1U) ? RT_TRUE : RT_FALSE;
    record.score_x10 = record.anomaly ? 87U : 32U;
    record.threshold_x10 = 59U;
    record.duration_ms = 52U;
    record.image_path = "";

    if (storage_app_append_detect_record(&record) != RT_EOK)
    {
        rt_kprintf("[storage] append detect record failed: %s\r\n", storage_app_last_error());
        return -1;
    }

    rt_kprintf("[storage] append detect record ok: %s %s\r\n",
               timestamp,
               record.anomaly ? "NG" : "OK");
    s_test_index++;
    return 0;
}

static int storage_export_records_cmd(int argc, char **argv)
{
    char path[STORAGE_PATH_MAX];

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (storage_app_export_records_csv(path, sizeof(path)) != RT_EOK)
    {
        rt_kprintf("[storage] export records failed: %s\r\n", storage_app_last_error());
        return -1;
    }

    rt_kprintf("[storage] export records ok: %s\r\n", path);
    return 0;
}

static int storage_clear_records_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (storage_app_clear_detect_records() != RT_EOK)
    {
        rt_kprintf("[storage] clear records failed: %s\r\n", storage_app_last_error());
        return -1;
    }

    rt_kprintf("[storage] clear records ok\r\n");
    return 0;
}

static int storage_reset_project_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (storage_app_reset_project_storage() != RT_EOK)
    {
        rt_kprintf("[storage] reset project failed: %s\r\n", storage_app_last_error());
        return -1;
    }

    rt_kprintf("[storage] reset project ok\r\n");
    return 0;
}

MSH_CMD_EXPORT(storage_init_cmd, initialize vision storage directories);
MSH_CMD_EXPORT(storage_test_cmd, write a test file to TF card);
MSH_CMD_EXPORT(storage_collect_begin_cmd, begin a collect photo session);
MSH_CMD_EXPORT(storage_collect_end_cmd, end the current collect photo session);
MSH_CMD_EXPORT(storage_detect_record_test_cmd, append one fake detect record to CSV);
MSH_CMD_EXPORT(storage_export_records_cmd, export records.csv snapshot);
MSH_CMD_EXPORT(storage_clear_records_cmd, clear internal detect records);
MSH_CMD_EXPORT(storage_reset_project_cmd, reset /vision project storage);
#endif

#include "storage_internal.h"

const char g_records_header[] =
    "timestamp,model,result,score,threshold,duration_ms,image_path\r\n";

rt_bool_t g_storage_ready = RT_FALSE;
const char *g_storage_last_error = "not initialized";
static rt_bool_t s_collect_session_active = RT_FALSE;
static char s_collect_session_dir[STORAGE_PATH_MAX] = {0};
static uint16_t s_collect_next_image_index = 1U;
static storage_app_state_changed_cb_t s_storage_state_changed_cb = RT_NULL;
static void *s_storage_state_changed_user_data = RT_NULL;

void storage_notify_state_changed(void)
{
    if (s_storage_state_changed_cb != RT_NULL)
    {
        s_storage_state_changed_cb(s_storage_state_changed_user_data);
    }
}

void storage_collect_reset_state(void)
{
    s_collect_session_active = RT_FALSE;
    s_collect_session_dir[0] = '\0';
    s_collect_next_image_index = 1U;
}

/*
 * 按小端字节序写入 16 位数值。
 *
 * 形参:
 * - dst: 目标缓冲区，至少 2 字节可写。
 * - value: 待编码的 16 位数值。
 *
 * 返回值:
 * - 无。
 */
static void storage_put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/*
 * 按小端字节序写入 32 位数值。
 *
 * 形参:
 * - dst: 目标缓冲区，至少 4 字节可写。
 * - value: 待编码的 32 位数值。
 *
 * 返回值:
 * - 无。
 */
static void storage_put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/*
 * 把缓冲区全部写入文件描述符。
 *
 * 形参:
 * - fd: open() 返回的文件描述符。
 * - data: 待写入的数据。
 * - size: 待写入的字节数。
 *
 * 返回值:
 * - 0: 全部写入成功。
 * - -1: write() 失败或写入字节数不足。
 */
int storage_write_all(int fd, const void *data, uint32_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;
    uint32_t remain = size;

    while (remain > 0U)
    {
        int written = write(fd, cursor, remain);
        if (written <= 0)
        {
            return -1;
        }

        cursor += written;
        remain -= (uint32_t)written;
    }

    return 0;
}

/*
 * 生成 RGB565 像素数据对应的 BMP 文件头。
 *
 * 形参:
 * - header: 目标缓冲区，长度必须为 BMP_RGB565_HEADER_SIZE。
 * - width: 图像宽度（像素）。
 * - height: 图像高度（像素）。
 * - image_bytes: RGB565 像素数据总字节数。
 *
 * 返回值:
 * - 无。
 */
static void storage_build_rgb565_bmp_header(uint8_t *header,
                                            uint16_t width,
                                            uint16_t height,
                                            uint32_t image_bytes)
{
    uint32_t file_size = BMP_RGB565_HEADER_SIZE + image_bytes;

    rt_memset(header, 0, BMP_RGB565_HEADER_SIZE);

    header[0] = 'B';
    header[1] = 'M';
    storage_put_le32(&header[2], file_size);
    storage_put_le32(&header[10], BMP_RGB565_HEADER_SIZE);

    storage_put_le32(&header[14], BMP_INFO_HEADER_SIZE);
    storage_put_le32(&header[18], width);
    storage_put_le32(&header[22], height);
    storage_put_le16(&header[26], 1U);
    storage_put_le16(&header[28], STORAGE_BMP_RGB565_BPP);
    storage_put_le32(&header[30], BMP_RGB565_COMPRESSION_BITFIELDS);
    storage_put_le32(&header[34], image_bytes);

    /* RGB565 位掩码：红 0xF800、绿 0x07E0、蓝 0x001F。 */
    storage_put_le32(&header[54], 0x0000F800UL);
    storage_put_le32(&header[58], 0x000007E0UL);
    storage_put_le32(&header[62], 0x0000001FUL);
}

/*
 * 判断文件或目录是否已经存在。
 *
 * 形参:
 * - path: 需要检查的文件或目录路径，例如 "/vision/config.ini"。
 *
 * 返回值:
 * - RT_TRUE: 路径存在。
 * - RT_FALSE: 路径不存在，或者 stat() 查询失败。
 *
 * 说明:
 * stat() 是 POSIX 风格接口，经过 DFS 转到具体文件系统。
 */
rt_bool_t storage_path_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? RT_TRUE : RT_FALSE;
}

static rt_bool_t storage_name_ends_with(const char *name, const char *suffix)
{
    uint32_t name_len;
    uint32_t suffix_len;

    if ((name == RT_NULL) || (suffix == RT_NULL))
    {
        return RT_FALSE;
    }

    name_len = (uint32_t)rt_strlen(name);
    suffix_len = (uint32_t)rt_strlen(suffix);
    if ((suffix_len == 0U) || (name_len < suffix_len))
    {
        return RT_FALSE;
    }

    return (rt_strcmp(name + name_len - suffix_len, suffix) == 0) ? RT_TRUE : RT_FALSE;
}

/*
 * 删除目录下的全部文件与子目录。
 *
 * 形参:
 * - dir_path: 待清空的目录。
 * - remove_self: 为 RT_TRUE 时清空后同时删除 dir_path 本身。
 *
 * 返回值:
 * - 0: 删除成功。
 * - -1: 有子项删除失败。
 *
 * 说明:
 * - 仅用于本项目自有的 /vision 目录树。
 */
int storage_delete_tree(const char *dir_path, rt_bool_t remove_self)
{
    DIR *dir;
    struct dirent *entry;
    char path[STORAGE_PATH_MAX];

    if (dir_path == RT_NULL)
    {
        return -1;
    }

    dir = opendir(dir_path);
    if (dir == RT_NULL)
    {
        return storage_path_exists(dir_path) ? -1 : 0;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        int len;

        if ((rt_strcmp(entry->d_name, ".") == 0) || (rt_strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }

        len = rt_snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if ((len <= 0) || ((uint32_t)len >= sizeof(path)))
        {
            closedir(dir);
            return -1;
        }

        if (entry->d_type == DT_DIR)
        {
            if (storage_delete_tree(path, RT_TRUE) != 0)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            if (unlink(path) != 0)
            {
                if (storage_delete_tree(path, RT_TRUE) != 0)
                {
                    closedir(dir);
                    return -1;
                }
            }
        }
    }

    closedir(dir);

    if ((remove_self == RT_TRUE) && (rmdir(dir_path) != 0))
    {
        return -1;
    }

    return 0;
}

static uint16_t storage_count_bmp_files_in_dir(const char *dir_path)
{
    DIR *dir;
    struct dirent *entry;
    uint32_t count = 0U;

    dir = opendir(dir_path);
    if (dir == RT_NULL)
    {
        return 0U;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) &&
            (storage_name_ends_with(entry->d_name, ".bmp") == RT_TRUE))
        {
            if (count < 65535U)
            {
                count++;
            }
        }
    }

    closedir(dir);
    return (uint16_t)count;
}

uint16_t storage_count_collect_samples(void)
{
    DIR *root;
    struct dirent *entry;
    uint32_t count = 0U;
    char path[STORAGE_PATH_MAX];

    root = opendir(STORAGE_COLLECT_PHOTOS_DIR);
    if (root == RT_NULL)
    {
        return 0U;
    }

    while ((entry = readdir(root)) != RT_NULL)
    {
        int len;

        if ((rt_strcmp(entry->d_name, ".") == 0) || (rt_strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }

        if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) &&
            (storage_name_ends_with(entry->d_name, ".bmp") == RT_TRUE))
        {
            if (count < 65535U)
            {
                count++;
            }
        }

        if ((entry->d_type != DT_DIR) && (entry->d_type != DT_UNKNOWN))
        {
            continue;
        }

        len = rt_snprintf(path, sizeof(path), "%s/%s", STORAGE_COLLECT_PHOTOS_DIR, entry->d_name);
        if ((len <= 0) || ((uint32_t)len >= sizeof(path)))
        {
            continue;
        }

        count += storage_count_bmp_files_in_dir(path);
        if (count > 65535U)
        {
            count = 65535U;
            break;
        }
    }

    closedir(root);
    return (uint16_t)count;
}

/*
 * 如果目录不存在，就创建目录。
 *
 * 形参:
 * - path: 要创建的目录路径，例如 "/vision/models"。
 *
 * 返回值:
 * - 0: 目录已经存在，或者创建成功。
 * - -1: 创建失败。
 *
 * 说明:
 * mkdir() 的第二个参数是权限 mode。RT-Thread/ElmFat 下权限意义不大，
 * 这里传 0 即可。
 */
int storage_mkdir_if_missing(const char *path)
{
    if (storage_path_exists(path))
    {
        return 0;
    }

    if (mkdir(path, 0) == 0)
    {
        return 0;
    }

    /*
     * 有些情况下目录可能被其他线程刚好创建，所以失败后再查一次。
     * 如果路径已经存在，也认为结果可用。
     */
    return storage_path_exists(path) ? 0 : -1;
}

/*
 * 把字符串复制到定长缓冲区。
 *
 * 形参:
 * - dst: 目标缓冲区，dst_size 为 0 时可为 NULL。
 * - dst_size: dst 的字节数。
 * - src: 源字符串。
 *
 * 返回值:
 * - 无。
 */
void storage_copy_path(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t copy_len;

    if ((dst == RT_NULL) || (dst_size == 0U) || (src == RT_NULL))
    {
        return;
    }

    copy_len = (uint32_t)rt_strlen(src);
    if (copy_len >= dst_size)
    {
        copy_len = dst_size - 1U;
    }

    rt_memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

/*
 * 生成采集会话目录路径。
 *
 * 形参:
 * - buffer: 输出路径缓冲区。
 * - buffer_size: buffer 的字节数。
 * - session_index: 会话序号，例如 1 对应 session_001。
 *
 * 返回值:
 * - 0: 生成成功。
 * - -1: 缓冲区无效或长度不足。
 */
static int storage_format_session_dir(char *buffer, uint32_t buffer_size, uint16_t session_index)
{
    int len;

    if ((buffer == RT_NULL) || (buffer_size == 0U))
    {
        return -1;
    }

    len = rt_snprintf(buffer,
                      buffer_size,
                      STORAGE_COLLECT_PHOTOS_DIR "/session_%03u",
                      (unsigned int)session_index);
    return ((len > 0) && ((uint32_t)len < buffer_size)) ? 0 : -1;
}

/*
 * 在当前采集会话目录下生成图片 BMP 路径。
 *
 * 形参:
 * - buffer: 输出路径缓冲区。
 * - buffer_size: buffer 的字节数。
 * - image_index: 图片序号，例如 1 对应 0001.bmp。
 *
 * 返回值:
 * - 0: 生成成功。
 * - -1: 缓冲区无效、长度不足或没有活动会话。
 */
static int storage_format_collect_image_path(char *buffer, uint32_t buffer_size, uint16_t image_index)
{
    int len;

    if ((buffer == RT_NULL) ||
        (buffer_size == 0U) ||
        (s_collect_session_active == RT_FALSE))
    {
        return -1;
    }

    len = rt_snprintf(buffer,
                      buffer_size,
                      "%s/%04u.bmp",
                      s_collect_session_dir,
                      (unsigned int)image_index);
    return ((len > 0) && ((uint32_t)len < buffer_size)) ? 0 : -1;
}

/*
 * 如果文件不存在，就写入一份默认内容。
 *
 * 形参:
 * - path: 要写入的文件路径，例如 "/vision/config.ini"。
 * - content: 默认文件内容，必须是以 '\0' 结尾的字符串。
 *
 * 返回值:
 * - 0: 文件已经存在，或者新文件写入成功。
 * - -1: open()/write() 失败，或者写入长度不完整。
 *
 * 说明:
 * - O_CREAT 表示文件不存在时创建。
 * - O_TRUNC 表示创建新文件后从空文件开始写。
 * - 如果文件已经存在，本函数不会覆盖，避免擦掉用户配置。
 */
static int storage_write_file_if_missing(const char *path, const char *content)
{
    int fd;
    int len;
    int written;

    if (storage_path_exists(path))
    {
        return 0;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return -1;
    }

    len = (int)rt_strlen(content);
    written = write(fd, content, len);
    close(fd);

    return written == len ? 0 : -1;
}

int storage_write_text_file_replace(const char *path, const char *content)
{
    int fd;
    uint32_t len;
    int ret;

    if ((path == RT_NULL) || (content == RT_NULL))
    {
        return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return -1;
    }

    len = (uint32_t)rt_strlen(content);
    ret = storage_write_all(fd, content, len);
    close(fd);

    return ret;
}

/*
 * 使用栈上小缓冲区复制单个文件。
 *
 * 形参:
 * - src_path: 源文件路径。
 * - dst_path: 目标文件路径。
 *
 * 返回值:
 * - 0: 复制成功。
 * - -1: open/read/write/close 失败。
 *
 * 说明:
 * - 运行在线程栈上，缓冲区刻意保持较小。
 */
int storage_copy_file(const char *src_path, const char *dst_path)
{
    int src_fd;
    int dst_fd;
    int read_len;
    int ret = 0;
    uint8_t buffer[STORAGE_COPY_BUFFER_SIZE];

    if ((src_path == RT_NULL) || (dst_path == RT_NULL))
    {
        return -1;
    }

    src_fd = open(src_path, O_RDONLY, 0);
    if (src_fd < 0)
    {
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (dst_fd < 0)
    {
        close(src_fd);
        return -1;
    }

    while ((read_len = read(src_fd, buffer, sizeof(buffer))) > 0)
    {
        if (storage_write_all(dst_fd, buffer, (uint32_t)read_len) != 0)
        {
            ret = -1;
            break;
        }
    }

    if (read_len < 0)
    {
        ret = -1;
    }

    if (close(src_fd) != 0)
    {
        ret = -1;
    }

    if (close(dst_fd) != 0)
    {
        ret = -1;
    }

    return ret;
}

static rt_bool_t storage_file_starts_with(const char *path, const char *prefix)
{
    int fd;
    int read_len;
    uint32_t prefix_len;
    char buffer[96];

    if ((path == RT_NULL) || (prefix == RT_NULL))
    {
        return RT_FALSE;
    }

    prefix_len = (uint32_t)rt_strlen(prefix);
    if ((prefix_len == 0U) || (prefix_len >= sizeof(buffer)))
    {
        return RT_FALSE;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return RT_FALSE;
    }

    read_len = read(fd, buffer, prefix_len);
    close(fd);

    if (read_len != (int)prefix_len)
    {
        return RT_FALSE;
    }

    return (rt_memcmp(buffer, prefix, prefix_len) == 0) ? RT_TRUE : RT_FALSE;
}

/*
 * 创建默认配置文件。
 *
 * 形参: 无。
 * 返回值:
 * - 0: config.ini 已存在或创建成功。
 * - -1: 创建失败。
 *
 * 说明:
 * 第一版先用简单 ini 文本，后面亮度、语言、默认模型等设置都可以放这里。
 */
static int storage_create_default_config(void)
{
    static const char default_config[] =
        "# Vision Board storage config\r\n"
        "model=cap_v8_96\r\n"
        "language=zh\r\n"
        "brightness=80\r\n"
        "save_detect_image=0\r\n";

    return storage_write_file_if_missing(STORAGE_CONFIG_FILE, default_config);
}

static int storage_create_default_model_ini(void)
{
    static const char default_model_ini[] =
        "# Vision Board model package\r\n"
        "name=cap_v8_96\r\n"
        "profile=cap_v8_96\r\n"
        "input=96x96\r\n"
        "grid=5x5\r\n"
        "threshold=5.9\r\n"
        "main=model.tflite\r\n"
        "anomaly=anomaly_model.tflite\r\n";

    return storage_write_file_if_missing(STORAGE_DEFAULT_MODEL_INI, default_model_ini);
}

/*
 * 创建检测记录 CSV 文件。
 *
 * 形参: 无。
 * 返回值:
 * - 0: records.csv 已存在或创建成功。
 * - -1: 创建失败。
 *
 * 说明:
 * 第一版只放表头。后续检测完成后追加一行结果即可。
 */
static int storage_create_records_file(void)
{
    if (storage_path_exists(STORAGE_RECORDS_FILE))
    {
        if (storage_file_starts_with(STORAGE_RECORDS_FILE, g_records_header) == RT_TRUE)
        {
            return 0;
        }

        /*
         * 旧版本 CSV 表头字段较少，记录为空时直接重建，
         * 保证数据页解析始终面对同一种格式。
         */
        return storage_write_text_file_replace(STORAGE_RECORDS_FILE, g_records_header);
    }

    return storage_write_file_if_missing(STORAGE_RECORDS_FILE, g_records_header);
}

int storage_app_init(void)
{
    g_storage_ready = RT_FALSE;
    g_storage_last_error = "init start";

    /*
     * rt_device_find("sd") 只检查 SD 块设备是否已经注册出来。
     * 真正的 FAT 文件系统挂载由 board/ports/mnt.c 里的 dfs_mount() 完成。
     */
    if (rt_device_find("sd") == RT_NULL)
    {
        g_storage_last_error = "sd block device not found";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_ROOT_DIR) != 0)
    {
        g_storage_last_error = "create /vision failed";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_MODELS_DIR) != 0)
    {
        g_storage_last_error = "create /vision/models failed";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_DEFAULT_MODEL_DIR) != 0)
    {
        g_storage_last_error = "create default model dir failed";
        return -RT_ERROR;
    }

    if (storage_create_default_model_ini() != 0)
    {
        g_storage_last_error = "create default model.ini failed";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_COLLECT_PHOTOS_DIR) != 0)
    {
        g_storage_last_error = "create collect photos dir failed";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_DETECT_RECORDS_DIR) != 0)
    {
        g_storage_last_error = "create detect records dir failed";
        return -RT_ERROR;
    }

    if (storage_mkdir_if_missing(STORAGE_EXPORT_DIR) != 0)
    {
        g_storage_last_error = "create export dir failed";
        return -RT_ERROR;
    }

    if (storage_create_default_config() != 0)
    {
        g_storage_last_error = "create config.ini failed";
        return -RT_ERROR;
    }

    if (storage_create_records_file() != 0)
    {
        g_storage_last_error = "create records.csv failed";
        return -RT_ERROR;
    }

    g_storage_ready = RT_TRUE;
    g_storage_last_error = "ok";
    storage_notify_state_changed();
    return RT_EOK;
}

rt_bool_t storage_app_is_ready(void)
{
    return g_storage_ready;
}

rt_bool_t storage_app_filesystem_is_mounted(void)
{
    struct statfs fs;

    if (rt_device_find("sd") == RT_NULL)
    {
        return RT_FALSE;
    }

    return (statfs("/", &fs) == 0) ? RT_TRUE : RT_FALSE;
}

void storage_app_on_card_removed(void)
{
    g_storage_ready = RT_FALSE;
    g_storage_last_error = "sd card removed";
    config_app_mark_unloaded();
    storage_collect_reset_state();
    storage_notify_state_changed();
}

void storage_app_on_card_mounted(void)
{
    g_storage_last_error = "sd card mounted";
    config_app_mark_unloaded();
    storage_notify_state_changed();
}

void storage_app_set_state_changed_callback(storage_app_state_changed_cb_t cb,
                                            void *user_data)
{
    s_storage_state_changed_cb = cb;
    s_storage_state_changed_user_data = user_data;
}

const char *storage_app_last_error(void)
{
    return g_storage_last_error;
}

int storage_app_get_status(storage_app_status_t *status)
{
    struct statfs fs;
    uint64_t total_bytes;
    uint64_t free_bytes;

    if (status == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_memset(status, 0, sizeof(*status));
    status->block_device_present = (rt_device_find("sd") != RT_NULL) ? RT_TRUE : RT_FALSE;
    status->filesystem_ready = RT_FALSE;
    status->last_error = g_storage_last_error;

    if (status->block_device_present == RT_FALSE)
    {
        status->last_error = "sd block device not found";
        return RT_EOK;
    }

    if (statfs("/", &fs) != 0)
    {
        status->last_error = "statfs failed";
        return RT_EOK;
    }

    total_bytes = (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize;
    free_bytes = (uint64_t)fs.f_bfree * (uint64_t)fs.f_bsize;

    status->filesystem_ready = RT_TRUE;
    status->total_mb = (uint32_t)(total_bytes / (1024ULL * 1024ULL));
    status->free_mb = (uint32_t)(free_bytes / (1024ULL * 1024ULL));
    status->used_mb = (status->total_mb > status->free_mb) ?
                      (status->total_mb - status->free_mb) : 0U;
    status->last_error = g_storage_last_error;

    return RT_EOK;
}

int storage_app_save_rgb565_bmp(const char *path,
                                const uint16_t *pixels,
                                uint16_t width,
                                uint16_t height)
{
    uint8_t header[BMP_RGB565_HEADER_SIZE];
    static const uint8_t row_padding[4] = {0U, 0U, 0U, 0U};
    uint32_t row_bytes;
    uint32_t row_stride;
    uint32_t padding_bytes;
    uint32_t image_bytes;
    int fd;
    int y;

    if ((path == RT_NULL) || (pixels == RT_NULL) || (width == 0U) || (height == 0U))
    {
        g_storage_last_error = "invalid bmp parameter";
        return -RT_ERROR;
    }

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    row_bytes = (uint32_t)width * sizeof(uint16_t);
    row_stride = (row_bytes + 3U) & ~3UL;
    padding_bytes = row_stride - row_bytes;
    image_bytes = row_stride * (uint32_t)height;
    storage_build_rgb565_bmp_header(header, width, height, image_bytes);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        g_storage_last_error = "open bmp file failed";
        return -RT_ERROR;
    }

    if (storage_write_all(fd, header, sizeof(header)) != 0)
    {
        close(fd);
        g_storage_last_error = "write bmp header failed";
        return -RT_ERROR;
    }

    /*
     * 标准 BMP 按自下而上的顺序存储行。
     * 每行需按 4 字节对齐；QVGA RGB565 本身已对齐，
     * 保留补齐逻辑以支持任意尺寸。
     */
    for (y = (int)height - 1; y >= 0; y--)
    {
        const uint16_t *row = pixels + ((uint32_t)y * width);
        if (storage_write_all(fd, row, row_bytes) != 0)
        {
            close(fd);
            g_storage_last_error = "write bmp pixels failed";
            return -RT_ERROR;
        }

        if ((padding_bytes > 0U) &&
            (storage_write_all(fd, row_padding, padding_bytes) != 0))
        {
            close(fd);
            g_storage_last_error = "write bmp padding failed";
            return -RT_ERROR;
        }
    }

    close(fd);
    g_storage_last_error = "ok";
    return RT_EOK;
}

int storage_app_collect_begin_session(char *out_dir, uint32_t out_dir_size)
{
    char candidate[STORAGE_PATH_MAX];
    uint16_t session_index;

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    if (s_collect_session_active == RT_TRUE)
    {
        storage_copy_path(out_dir, out_dir_size, s_collect_session_dir);
        return RT_EOK;
    }

    for (session_index = 1U; session_index <= STORAGE_COLLECT_SESSION_MAX; session_index++)
    {
        if (storage_format_session_dir(candidate, sizeof(candidate), session_index) != 0)
        {
            g_storage_last_error = "format session dir failed";
            return -RT_ERROR;
        }

        if (storage_path_exists(candidate))
        {
            continue;
        }

        if (mkdir(candidate, 0) != 0)
        {
            g_storage_last_error = "create session dir failed";
            return -RT_ERROR;
        }

        storage_copy_path(s_collect_session_dir, sizeof(s_collect_session_dir), candidate);
        storage_copy_path(out_dir, out_dir_size, candidate);
        s_collect_next_image_index = 1U;
        s_collect_session_active = RT_TRUE;
        g_storage_last_error = "ok";
        return RT_EOK;
    }

    g_storage_last_error = "no free collect session";
    return -RT_ERROR;
}

void storage_app_collect_end_session(void)
{
    storage_collect_reset_state();
}

rt_bool_t storage_app_collect_is_active(void)
{
    return s_collect_session_active;
}

int storage_app_collect_clear_current_session(void)
{
    char path[STORAGE_PATH_MAX];
    uint16_t image_index;

    if (s_collect_session_active == RT_FALSE)
    {
        s_collect_next_image_index = 1U;
        return RT_EOK;
    }

    /*
     * 文件命名规则由本模块决定，可按序号直接删除；
     * 遇到第一个缺失文件即停止，因为序号只在写入成功后递增，
     * 已保存的文件一定是连续的。
     */
    for (image_index = 1U; image_index < s_collect_next_image_index; image_index++)
    {
        if (storage_format_collect_image_path(path, sizeof(path), image_index) != 0)
        {
            g_storage_last_error = "format clear path failed";
            return -RT_ERROR;
        }

        if (storage_path_exists(path))
        {
            if (unlink(path) != 0)
            {
                g_storage_last_error = "delete collect image failed";
                return -RT_ERROR;
            }
        }
    }

    s_collect_next_image_index = 1U;
    g_storage_last_error = "ok";
    storage_notify_state_changed();
    return RT_EOK;
}

int storage_app_collect_save_rgb565_bmp(const uint16_t *pixels,
                                        uint16_t width,
                                        uint16_t height,
                                        char *out_path,
                                        uint32_t out_path_size)
{
    char path[STORAGE_PATH_MAX];
    int ret;

    if (s_collect_session_active == RT_FALSE)
    {
        if (storage_app_collect_begin_session(RT_NULL, 0U) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    if (storage_format_collect_image_path(path,
                                          sizeof(path),
                                          s_collect_next_image_index) != 0)
    {
        g_storage_last_error = "format image path failed";
        return -RT_ERROR;
    }

    ret = storage_app_save_rgb565_bmp(path, pixels, width, height);
    if (ret != RT_EOK)
    {
        return ret;
    }

    storage_copy_path(out_path, out_path_size, path);
    if (s_collect_next_image_index < 9999U)
    {
        s_collect_next_image_index++;
    }

    storage_notify_state_changed();
    return RT_EOK;
}

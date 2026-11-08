#ifndef STORAGE_INTERNAL_H
#define STORAGE_INTERNAL_H

#include "storage_app.h"

#include "config_app.h"

#include <dfs_file.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#ifdef FINSH_USING_MSH
#include <finsh.h>
#endif

#define STORAGE_TEST_FILE "/vision/detect_records/storage_test.txt"
#define STORAGE_DEFAULT_MODEL_INI "/vision/models/cap_v8_96/model.ini"
#define BMP_FILE_HEADER_SIZE 14U
#define BMP_INFO_HEADER_SIZE 40U
#define BMP_COLOR_MASK_SIZE 12U
#define BMP_RGB565_HEADER_SIZE (BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE + BMP_COLOR_MASK_SIZE)
#define BMP_RGB565_COMPRESSION_BITFIELDS 3U
#define STORAGE_COLLECT_SESSION_MAX 999U
#define STORAGE_RECORD_LINE_MAX 256U
#define STORAGE_EXPORT_INDEX_MAX 999U
#define STORAGE_COPY_BUFFER_SIZE 256U

/* 模块内部共享接口，仅供本模块的拆分源文件使用。 */
extern const char g_records_header[];
extern rt_bool_t g_storage_ready;
extern const char *g_storage_last_error;
void storage_notify_state_changed(void);
void storage_collect_reset_state(void);
int storage_write_all(int fd, const void *data, uint32_t size);
rt_bool_t storage_path_exists(const char *path);
int storage_delete_tree(const char *dir_path, rt_bool_t remove_self);
uint16_t storage_count_collect_samples(void);
int storage_mkdir_if_missing(const char *path);
void storage_copy_path(char *dst, uint32_t dst_size, const char *src);
int storage_write_text_file_replace(const char *path, const char *content);
int storage_copy_file(const char *src_path, const char *dst_path);

#endif /* STORAGE_INTERNAL_H */

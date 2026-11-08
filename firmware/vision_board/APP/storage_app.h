#ifndef STORAGE_APP_H
#define STORAGE_APP_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TF 卡中的第一版目录规划。
 *
 * 这里统一放路径宏，后面采集图片保存、检测记录保存、模型加载都从这里取路径，
 * 避免多个模块里到处散落 "/vision/xxx" 字符串。
 */
#define STORAGE_ROOT_DIR              "/vision"
#define STORAGE_MODELS_DIR            "/vision/models"
#define STORAGE_DEFAULT_MODEL_DIR     "/vision/models/cap_v8_96"
#define STORAGE_COLLECT_PHOTOS_DIR    "/vision/collect_photos"
#define STORAGE_DETECT_RECORDS_DIR    "/vision/detect_records"
#define STORAGE_EXPORT_DIR            "/vision/export"
#define STORAGE_CONFIG_FILE           "/vision/config.ini"
#define STORAGE_GATE_CONFIG_FILE      "/vision/gate.ini"
#define STORAGE_RECORDS_FILE          "/vision/detect_records/records.csv"
#define STORAGE_BMP_RGB565_BPP        16U
#define STORAGE_PATH_MAX              128U

typedef struct
{
    const char *timestamp;
    const char *model;
    rt_bool_t anomaly;
    uint16_t score_x10;
    uint16_t threshold_x10;
    uint16_t duration_ms;
    const char *image_path;
} storage_detect_record_t;

typedef struct
{
    char timestamp[16];
    char model[24];
    rt_bool_t anomaly;
    uint16_t score_x10;
    uint16_t threshold_x10;
    uint16_t duration_ms;
    char image_path[STORAGE_PATH_MAX];
} storage_detect_record_item_t;

typedef struct
{
    rt_bool_t block_device_present;
    rt_bool_t filesystem_ready;
    uint32_t total_mb;
    uint32_t used_mb;
    uint32_t free_mb;
    const char *last_error;
} storage_app_status_t;

typedef struct
{
    uint16_t total_records;
    uint16_t ng_count;
    uint16_t sample_count;
    uint16_t trend_count;
    uint16_t trend_score_x10[16];
} storage_app_record_stats_t;

typedef void (*storage_app_state_changed_cb_t)(void *user_data);

/*
 * 初始化 TF 卡上的项目目录。
 *
 * 形参: 无。
 * 返回值:
 * - RT_EOK: 目录和基础文件已经准备好。
 * - -RT_ERROR: TF 卡未挂载，或者创建目录/文件失败。
 *
 * 作用:
 * - 检查 SD 块设备是否存在。
 * - 创建 /vision 目录结构。
 * - 创建默认 config.ini。
 * - 创建 detect_records/records.csv 表头。
 */
int storage_app_init(void);

/*
 * 查询 storage_app_init() 是否已经成功执行。
 *
 * 形参: 无。
 * 返回值:
 * - RT_TRUE: 存储目录已经准备好。
 * - RT_FALSE: 存储目录还没准备好，或者初始化失败。
 */
rt_bool_t storage_app_is_ready(void);

/*
 * Check whether the root filesystem is mounted before creating /vision.
 *
 * Return:
 * - RT_TRUE: The SD block device exists and statfs("/") succeeds.
 * - RT_FALSE: The card or filesystem is not ready yet.
 *
 * Note:
 * - This is read-only. It intentionally does not create directories or files.
 */
rt_bool_t storage_app_filesystem_is_mounted(void);

/*
 * Notify storage_app that the TF card has been removed or unmounted.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - None.
 *
 * Note:
 * - This only resets RAM state. It does not touch files on the card.
 */
void storage_app_on_card_removed(void);

/*
 * Notify storage_app that the TF card has been mounted again.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - None.
 *
 * Note:
 * - This notifies UI/listeners only. Directory creation is still owned by
 *   storage_app_init().
 */
void storage_app_on_card_mounted(void);

/*
 * Register a callback for TF-card/storage state changes.
 *
 * Parameters:
 * - cb: Callback invoked after mount/unmount/init state changes. Pass NULL to
 *   unregister.
 * - user_data: Opaque pointer passed back to cb.
 *
 * Return:
 * - None.
 *
 * Note:
 * - The callback runs in the caller's thread. UI code must switch to the LVGL
 *   thread before touching LVGL objects.
 */
void storage_app_set_state_changed_callback(storage_app_state_changed_cb_t cb,
                                            void *user_data);

/*
 * 获取最近一次存储初始化失败的原因。
 *
 * 形参: 无。
 * 返回值:
 * - 指向静态字符串的指针，用于串口打印调试。
 */
const char *storage_app_last_error(void);

/*
 * Read current TF-card/filesystem status without creating directories or files.
 *
 * Parameters:
 * - status: Output status buffer. It receives block-device state, filesystem
 *   accessibility and capacity in MB.
 *
 * Return:
 * - RT_EOK: Status was filled. Missing card is still reported through fields.
 * - -RT_ERROR: status is NULL.
 *
 * Note:
 * - This API is read-only and is safe for UI periodic refresh.
 */
int storage_app_get_status(storage_app_status_t *status);

/*
 * Build lightweight statistics from records.csv and collected BMP files.
 *
 * Parameters:
 * - stats: Output statistics buffer.
 *
 * Return:
 * - RT_EOK: Statistics were loaded from TF card.
 * - -RT_ERROR: Storage is not ready or files cannot be scanned.
 *
 * Note:
 * - trend_score_x10 keeps the newest scores first. UI can reverse the order
 *   when drawing a left-to-right history chart.
 */
int storage_app_get_record_stats(storage_app_record_stats_t *stats);

/*
 * Save one RGB565 frame as a BMP file.
 *
 * Parameters:
 * - path: Output BMP path, for example "/vision/collect_photos/session_001/0001.bmp".
 * - pixels: Source image buffer. It must point to width * height RGB565 pixels.
 * - width: Image width in pixels.
 * - height: Image height in pixels.
 *
 * Return:
 * - RT_EOK: BMP file was written successfully.
 * - -RT_ERROR: Parameter error, storage is not ready, or file write failed.
 *
 * Note:
 * - The BMP uses BITMAPV3INFOHEADER with RGB565 bit masks.
 * - Rows are written bottom-up because standard BMP stores the last image row first.
 */
int storage_app_save_rgb565_bmp(const char *path,
                                const uint16_t *pixels,
                                uint16_t width,
                                uint16_t height);

/*
 * Start a collect-photo session.
 *
 * Parameters:
 * - out_dir: Optional output buffer used to receive the created session path.
 * - out_dir_size: Size of out_dir in bytes. Pass 0 when out_dir is NULL.
 *
 * Return:
 * - RT_EOK: Session is active.
 * - -RT_ERROR: Storage is not ready or no session directory can be created.
 *
 * Note:
 * - The first missing directory is used, for example session_001, session_002.
 * - If a session is already active, this function returns the current session.
 */
int storage_app_collect_begin_session(char *out_dir, uint32_t out_dir_size);

/*
 * End the current collect-photo session.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - None.
 *
 * Note:
 * - Ending a session only clears RAM state. It does not delete files.
 */
void storage_app_collect_end_session(void);

/*
 * Query whether a collect-photo session is active.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_TRUE: A collect session is active.
 * - RT_FALSE: No collect session is active.
 */
rt_bool_t storage_app_collect_is_active(void);

/*
 * Clear BMP files saved in the active collect-photo session.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_EOK: No active session exists, or active session files were removed.
 * - -RT_ERROR: A saved BMP file could not be deleted.
 *
 * Note:
 * - This keeps the current session directory and resets the next file name to
 *   0001.bmp. It is used when the current collection is not complete and the
 *   user presses Reset/Clear to restart the same session.
 */
int storage_app_collect_clear_current_session(void);

/*
 * Save one RGB565 frame into the active collect-photo session.
 *
 * Parameters:
 * - pixels: Source image buffer. It must point to width * height RGB565 pixels.
 * - width: Image width in pixels.
 * - height: Image height in pixels.
 * - out_path: Optional output buffer used to receive the saved BMP path.
 * - out_path_size: Size of out_path in bytes. Pass 0 when out_path is NULL.
 *
 * Return:
 * - RT_EOK: Frame was saved successfully.
 * - -RT_ERROR: Storage/session initialization failed or BMP write failed.
 *
 * Note:
 * - If no session is active, a new session is created automatically.
 * - File names are generated as 0001.bmp, 0002.bmp, ...
 */
int storage_app_collect_save_rgb565_bmp(const uint16_t *pixels,
                                        uint16_t width,
                                        uint16_t height,
                                        char *out_path,
                                        uint32_t out_path_size);

/*
 * Append one inspection result to /vision/detect_records/records.csv.
 *
 * Parameters:
 * - record: Result fields to write. Strings can be NULL; empty fields are
 *   written in that case.
 *
 * Return:
 * - RT_EOK: The row was appended.
 * - -RT_ERROR: Storage is not ready, parameters are invalid, or file write
 *   failed.
 *
 * Note:
 * - score_x10 and threshold_x10 are fixed-point values: 87 means 8.7.
 * - This is the first storage boundary for future Edge Impulse results.
 */
int storage_app_append_detect_record(const storage_detect_record_t *record);

/*
 * Export the internal detect record CSV as a user-visible snapshot.
 *
 * Parameters:
 * - out_path: Optional output buffer used to receive the exported CSV path.
 * - out_path_size: Size of out_path in bytes. Pass 0 when out_path is NULL.
 *
 * Return:
 * - RT_EOK: records.csv was copied to /vision/export/records_xxx.csv.
 * - -RT_ERROR: Storage is not ready, export directory cannot be created, or
 *   file copy failed.
 *
 * Note:
 * - /vision/detect_records/records.csv is still the live internal record.
 * - This function only creates a snapshot for PC viewing after the user taps
 *   Export CSV on the Data page.
 */
int storage_app_export_records_csv(char *out_path, uint32_t out_path_size);

/*
 * Clear internal detect records while keeping the CSV header.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_EOK: /vision/detect_records/records.csv now contains only the header.
 * - -RT_ERROR: Storage is not ready or the file cannot be rewritten.
 *
 * Note:
 * - Exported CSV snapshots in /vision/export are not deleted.
 */
int storage_app_clear_detect_records(void);

/*
 * Reset the project storage area on the TF card.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - RT_EOK: /vision was removed and rebuilt with the basic directory layout.
 * - -RT_ERROR: TF card/filesystem is not ready or deletion/rebuild failed.
 *
 * Note:
 * - This removes only the project-owned /vision tree. It intentionally does
 *   not delete FAT system folders such as /System Volume Information.
 */
int storage_app_reset_project_storage(void);

/*
 * Load recent inspection records from /vision/detect_records/records.csv.
 *
 * Parameters:
 * - records: Output array. The newest record is returned at index 0.
 * - max_records: Capacity of records.
 * - out_count: Output record count. It can be NULL.
 *
 * Return:
 * - RT_EOK: Records were loaded. out_count can be 0.
 * - -RT_ERROR: Storage is not ready, parameters are invalid, or file read
 *   failed.
 */
int storage_app_load_recent_detect_records(storage_detect_record_item_t *records,
                                           uint16_t max_records,
                                           uint16_t *out_count);

#ifdef __cplusplus
}
#endif

#endif

#include "ui_data.h"
#include "ui_fake_data.h"
#include "camera_app.h"
#include "detect_app.h"
#include "storage_app.h"

#define UI_DATA_REAL_RECORD_MAX 16U

static ui_detect_command_cb_t s_detect_command_cb = 0;
static void *s_detect_command_user_data = 0;
static ui_data_command_cb_t s_data_command_cb = 0;
static void *s_data_command_user_data = 0;
static storage_detect_record_item_t s_real_storage_records[UI_DATA_REAL_RECORD_MAX];
static ui_detect_record_t s_real_ui_records[UI_DATA_REAL_RECORD_MAX];
static ui_storage_info_t s_real_storage_info;
static ui_data_summary_t s_real_data_summary;
static storage_app_record_stats_t s_real_record_stats;
static uint16_t s_real_trend_scores_x10[16];
static uint16_t s_real_ui_record_count = 0U;
static uint8_t s_real_ui_records_valid = 0U;
static ui_detect_snapshot_t s_detect_snapshot_cache;

static void ui_data_clear_real_summary(void)
{
    s_real_data_summary.total_records = 0U;
    s_real_data_summary.total_samples = 0U;
    s_real_data_summary.ng_count = 0U;
    s_real_data_summary.ng_rate_x10 = 0U;
    s_real_data_summary.trend_count = 0U;
    s_real_data_summary.trend_score_x10 = s_real_trend_scores_x10;
}

static ui_storage_state_t ui_data_map_storage_state(const storage_app_status_t *status)
{
    if ((status == 0) || (status->block_device_present == RT_FALSE)) {
        return UI_STORAGE_MISSING;
    }

    if (status->filesystem_ready == RT_FALSE) {
        return UI_STORAGE_ERROR;
    }

    if ((status->total_mb > 0U) && (status->free_mb < 8U)) {
        return UI_STORAGE_FULL;
    }

    return UI_STORAGE_OK;
}

static void ui_data_sync_real_storage_info(void)
{
    storage_app_status_t status;

    if (storage_app_get_status(&status) != RT_EOK) {
        s_real_storage_info.state = UI_STORAGE_ERROR;
        s_real_storage_info.total_mb = 0U;
        s_real_storage_info.used_mb = 0U;
        s_real_storage_info.free_mb = 0U;
        s_real_storage_info.record_dir = STORAGE_DETECT_RECORDS_DIR;
        s_real_storage_info.sample_dir = STORAGE_COLLECT_PHOTOS_DIR;
        s_real_storage_info.export_dir = STORAGE_EXPORT_DIR;
        return;
    }

    s_real_storage_info.state = ui_data_map_storage_state(&status);
    s_real_storage_info.total_mb = status.total_mb;
    s_real_storage_info.used_mb = status.used_mb;
    s_real_storage_info.free_mb = status.free_mb;
    s_real_storage_info.record_dir = STORAGE_DETECT_RECORDS_DIR;
    s_real_storage_info.sample_dir = STORAGE_COLLECT_PHOTOS_DIR;
    s_real_storage_info.export_dir = STORAGE_EXPORT_DIR;
}

static void ui_data_sync_real_summary(void)
{
    if (storage_app_get_record_stats(&s_real_record_stats) != RT_EOK) {
        ui_data_clear_real_summary();
        return;
    }

    s_real_data_summary.total_records = s_real_record_stats.total_records;
    s_real_data_summary.total_samples = s_real_record_stats.sample_count;
    s_real_data_summary.ng_count = s_real_record_stats.ng_count;
    s_real_data_summary.ng_rate_x10 = (s_real_record_stats.total_records > 0U) ?
                                      (uint16_t)(((uint32_t)s_real_record_stats.ng_count * 1000U) /
                                                 s_real_record_stats.total_records) : 0U;

    if (s_real_record_stats.trend_count > 0U) {
        uint16_t count = s_real_record_stats.trend_count;
        if (count > (sizeof(s_real_trend_scores_x10) / sizeof(s_real_trend_scores_x10[0]))) {
            count = (uint16_t)(sizeof(s_real_trend_scores_x10) / sizeof(s_real_trend_scores_x10[0]));
        }

        for (uint16_t i = 0; i < count; i++) {
            s_real_trend_scores_x10[i] = s_real_record_stats.trend_score_x10[count - 1U - i];
        }

        s_real_data_summary.trend_count = count;
        s_real_data_summary.trend_score_x10 = s_real_trend_scores_x10;
    } else {
        s_real_data_summary.trend_count = 0U;
        s_real_data_summary.trend_score_x10 = s_real_trend_scores_x10;
    }
}

static void ui_data_sync_real_records(void)
{
    uint16_t count = 0U;

    if (storage_app_load_recent_detect_records(s_real_storage_records,
                                               UI_DATA_REAL_RECORD_MAX,
                                               &count) != RT_EOK) {
        s_real_ui_record_count = 0U;
        s_real_ui_records_valid = 1U;
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        s_real_ui_records[i].time = s_real_storage_records[i].timestamp;
        s_real_ui_records[i].anomaly = (s_real_storage_records[i].anomaly == RT_TRUE);
        s_real_ui_records[i].score_x10 = s_real_storage_records[i].score_x10;
        s_real_ui_records[i].threshold_x10 = s_real_storage_records[i].threshold_x10;
        s_real_ui_records[i].model_name = s_real_storage_records[i].model;
        s_real_ui_records[i].image_path = s_real_storage_records[i].image_path;
    }

    s_real_ui_record_count = count;
    s_real_ui_records_valid = 1U;
}

/* 返回检测页当前使用的数据快照；当前转发到 ui_fake_data.c，后续可替换成真实检测缓存。 */
const ui_detect_snapshot_t *ui_data_get_detect_snapshot(void)
{
    ui_detect_snapshot_t *snapshot = ui_fake_data_get_detect_snapshot();

    if ((detect_app_is_ready() == RT_TRUE) &&
        (detect_app_get_snapshot(&s_detect_snapshot_cache) == RT_EOK)) {
        s_detect_snapshot_cache.ai.fps_x10 = camera_app_get_lcd_preview_fps_x10();
        return &s_detect_snapshot_cache;
    }

    if (snapshot != 0) {
        snapshot->ai.fps_x10 = camera_app_get_lcd_preview_fps_x10();
    }

    return snapshot;
}

/* 返回检测页当前使用的热力图；当前转发到 ui_fake_data.c，真实系统中这里可改成 AI 推理结果缓存。 */
const ui_ai_heatmap_t *ui_data_get_detect_heatmap(void)
{
    if (detect_app_is_ready() == RT_TRUE) {
        return detect_app_get_heatmap();
    }

    return ui_fake_data_get_detect_heatmap();
}

/* 返回数据页统计摘要；当前转发到 PC 假数据，真实系统可改成记录模块缓存。 */
const ui_data_summary_t *ui_data_get_data_summary(void)
{
    ui_data_sync_real_summary();
    return &s_real_data_summary;
}

/* 返回 SD 卡/文件系统状态；当前转发到 PC 假数据。 */
const ui_storage_info_t *ui_data_get_storage_info(void)
{
    ui_data_sync_real_storage_info();
    return &s_real_storage_info;
}

/* 返回最近检测记录；当前转发到 PC 假数据。 */
const ui_detect_record_t *ui_data_get_recent_records(uint16_t *count)
{
    ui_data_sync_real_records();

    if (count != 0) {
        *count = s_real_ui_record_count;
    }

    return s_real_ui_records;
}

/* 注册检测命令回调；后续真实检测任务可以通过这里接收 Start/Stop/Refresh。 */
void ui_data_set_detect_command_handler(ui_detect_command_cb_t cb, void *user_data)
{
    s_detect_command_cb = cb;
    s_detect_command_user_data = user_data;
}

/* 注册数据页命令回调；真实系统中用于导出 CSV、清理日志等文件系统操作。 */
void ui_data_set_data_command_handler(ui_data_command_cb_t cb, void *user_data)
{
    s_data_command_cb = cb;
    s_data_command_user_data = user_data;
}

/* 发出检测命令；当前只模拟运行状态，真实系统会通过回调转发到底层任务。 */
void ui_data_emit_detect_command(ui_detect_command_t cmd)
{
    /*
     * PC 假运行状态。
     * 后续真实系统接入后，可以保留这里作为 UI 命令出口，由回调转发给检测任务。
     */
    if (detect_app_is_ready() == RT_FALSE) {
        ui_fake_data_emit_detect_command(cmd);
    }

    if (s_detect_command_cb) {
        s_detect_command_cb(cmd, s_detect_command_user_data);
    }
}

/* 发出数据页命令；当前只保留出口，真实系统中由回调转给文件系统任务。 */
void ui_data_emit_data_command(ui_data_command_t cmd)
{
    if (s_data_command_cb) {
        s_data_command_cb(cmd, s_data_command_user_data);
    }
}

/* 推进检测页流程灯显示状态；PC 阶段转发到假数据动画，真实系统中可以改成读取检测事件队列。 */
void ui_data_tick_detect_pipeline(uint32_t now_ms)
{
    if (detect_app_is_ready() == RT_FALSE) {
        ui_fake_data_tick_detect_pipeline(now_ms);
    } else {
        (void)now_ms;
    }
}

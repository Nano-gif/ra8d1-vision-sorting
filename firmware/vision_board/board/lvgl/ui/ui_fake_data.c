#include "ui_fake_data.h"

/*
 * 下面这一段是“正常样本”和“异常样本”的差异配置。
 *
 * 想看正常 UI：在 ui_fake_data.h 里把 UI_FAKE_DETECT_SCENE 改成 UI_FAKE_DETECT_SCENE_NORMAL。
 * 想看异常 UI：在 ui_fake_data.h 里把 UI_FAKE_DETECT_SCENE 改成 UI_FAKE_DETECT_SCENE_ANOMALY。
 *
 * 分数标准说明：
 * - UI_FAKE_SCORE_X10 是异常分数 * 10，32 表示 3.2，87 表示 8.7。
 * - threshold_x10 固定为 59，表示异常阈值 5.9。
 * - 3.2 <= 5.9，所以正常样本显示 NORMAL。
 * - 8.7 >  5.9，所以异常样本显示 ANOMALY。
 *
 * 这不是“概率 87%”，也不是“置信度 87%”。
 * 真机接入后，这个值应该来自 AI 后处理层输出的异常强度分数。
 */
#if UI_FAKE_DETECT_SCENE == UI_FAKE_DETECT_SCENE_NORMAL
#define UI_FAKE_IS_ANOMALY     false
#define UI_FAKE_SCORE_X10      32  /* 当前异常分数 3.2，小于阈值 5.9，所以判为正常。 */
#define UI_FAKE_MEAN_X10       12  /* 平均异常分数 1.2，表示整体没有明显异常区域。 */
#define UI_FAKE_HOT_CELLS      0
#define UI_FAKE_AI_TIME_MS     48
#define UI_FAKE_FPS_X10        168
#define UI_FAKE_YIELD_X10      992
static const uint8_t s_fake_heatmap_scores[49] = {
    0, 0, 0,  0,  0, 0, 0,
    0, 0, 0,  8,  0, 0, 0,
    0, 0, 6, 14, 10, 0, 0,
    0, 0, 0, 18, 12, 0, 0,
    0, 0, 0, 10, 16, 0, 0,
    0, 0, 0,  0,  8, 0, 0,
    0, 0, 0,  0,  0, 0, 0,
};
#else
#define UI_FAKE_IS_ANOMALY     true
#define UI_FAKE_SCORE_X10      87  /* 当前异常分数 8.7，大于阈值 5.9，所以判为异常。 */
#define UI_FAKE_MEAN_X10       31  /* 平均异常分数 3.1，用来辅助展示整体异常程度。 */
#define UI_FAKE_HOT_CELLS      5
#define UI_FAKE_AI_TIME_MS     52
#define UI_FAKE_FPS_X10        154
#define UI_FAKE_YIELD_X10      978
static const uint8_t s_fake_heatmap_scores[49] = {
    0,  0,  0,   0,   0,  0,  0,
    0,  0, 26,  48,  34,  0,  0,
    0, 18, 58,  96,  70, 22,  0,
    0,  0, 74, 184, 128, 36,  0,
    0,  0, 38, 118, 232, 84,  0,
    0,  0,  0,  52, 142, 64,  0,
    0,  0,  0,   0,  32,  0,  0,
};
#endif

/*
 * PC 模拟用的热力图描述。
 * ROI 使用 320x240 源图坐标；当前 ROI 是 x=40,y=0,w=240,h=240。
 */
static const ui_ai_heatmap_t s_fake_heatmap = {
    .grid_w = 7,
    .grid_h = 7,
    .roi_x = 40,
    .roi_y = 0,
    .roi_w = 240,
    .roi_h = 240,
    .score_map = s_fake_heatmap_scores,
    .score_count = 49,
};

/*
 * PC 模拟器检测页快照。
 * 这些字段已经按后续真实接口设计，页面只读这个快照，不直接知道数据来自假数据还是真机。
 */
static ui_detect_snapshot_t s_detect_snapshot = {
    .run_state = UI_DETECT_RUN_STOPPED,
    .stage = UI_DETECT_STAGE_IDLE,
    .model = {
        .name = "cap_120_v1",
        .postprocess = UI_AI_POSTPROCESS_VISUAL_AD,
        .input_w = 120,
        .input_h = 120,
        .grid_w = 7,
        .grid_h = 7,
        .threshold_x10 = 59,
        .tflite_count = 2,
        .arena_size = 0,
    },
    .camera = {
        .pixel_format = "RGB565",
        .frame_size_name = "QVGA",
        .frame_w = 320,
        .frame_h = 240,
        .frame_id = 18492,
        .roi_x = 40,
        .roi_y = 0,
        .roi_size = 240,
    },
    .ai = {
        .anomaly = UI_FAKE_IS_ANOMALY,
        .score_x10 = UI_FAKE_SCORE_X10,
        .threshold_x10 = 59,
        .mean_score_x10 = UI_FAKE_MEAN_X10,
        .hot_cells = UI_FAKE_HOT_CELLS,
        .ai_time_ms = UI_FAKE_AI_TIME_MS,
        .fps_x10 = UI_FAKE_FPS_X10,
        .yield_x10 = UI_FAKE_YIELD_X10,
    },
};

/* 最近 10 次异常分数趋势；单位是 score * 10，供 Data 页曲线图使用。 */
static const uint16_t s_fake_trend_scores_x10[] = {
    28, 36, 51, 74, 43, 32, 58, 87, 69, 44,
};

/*
 * PC 模拟数据页摘要。
 * 真实系统中这些数值应来自检测记录索引、采集任务记录和 SD 卡文件统计。
 */
static const ui_data_summary_t s_fake_data_summary = {
    .total_records = 1280,
    .total_samples = 320,
    .ng_count = 79,
    .ng_rate_x10 = 62,
    .trend_count = 10,
    .trend_score_x10 = s_fake_trend_scores_x10,
};

/* PC 模拟 SD 卡状态；真实接入时由 DFS/FATFS 查询容量后填充。 */
static const ui_storage_info_t s_fake_storage_info = {
    .state = UI_STORAGE_OK,
    .total_mb = 30412,
    .used_mb = 18620,
    .free_mb = 11792,
    .record_dir = "/sdcard/record",
    .sample_dir = "/sdcard/sample",
    .export_dir = "/sdcard/export",
};

/* PC 模拟最近检测记录；真实系统中由环形记录缓存或日志索引提供。 */
static const ui_detect_record_t s_fake_recent_records[] = {
    {"14:26:31", true,  87, 59, "cap_120_v1", "/record/ng_001.jpg"},
    {"14:25:58", false, 42, 59, "cap_120_v1", "/record/ok_248.jpg"},
    {"14:24:13", false, 35, 59, "cap_120_v1", "/record/ok_247.jpg"},
    {"14:22:49", true,  74, 59, "cap_120_v1", "/record/ng_000.jpg"},
    {"14:21:10", false, 28, 59, "cap_120_v1", "/record/ok_246.jpg"},
    {"14:19:42", false, 31, 59, "cap_120_v1", "/record/ok_245.jpg"},
    {"14:18:06", false, 44, 59, "cap_120_v1", "/record/ok_244.jpg"},
    {"14:16:27", true,  69, 59, "cap_120_v1", "/record/ng_999.jpg"},
    {"14:15:02", false, 36, 59, "cap_120_v1", "/record/ok_243.jpg"},
    {"14:13:48", false, 51, 59, "cap_120_v1", "/record/ok_242.jpg"},
};

/* 返回检测页当前使用的 PC 模拟快照。 */
ui_detect_snapshot_t *ui_fake_data_get_detect_snapshot(void)
{
    return &s_detect_snapshot;
}

/* 返回检测页当前使用的 PC 模拟热力图。 */
const ui_ai_heatmap_t *ui_fake_data_get_detect_heatmap(void)
{
    return &s_fake_heatmap;
}

/* 返回数据页 PC 假统计摘要。 */
const ui_data_summary_t *ui_fake_data_get_data_summary(void)
{
    return &s_fake_data_summary;
}

/* 返回数据页 PC 假 SD 卡状态。 */
const ui_storage_info_t *ui_fake_data_get_storage_info(void)
{
    return &s_fake_storage_info;
}

/* 返回数据页 PC 假最近检测记录。 */
const ui_detect_record_t *ui_fake_data_get_recent_records(uint16_t *count)
{
    if (count != 0) {
        *count = (uint16_t)(sizeof(s_fake_recent_records) / sizeof(s_fake_recent_records[0]));
    }

    return s_fake_recent_records;
}

/* 处理 PC 模拟阶段的按钮命令。 */
void ui_fake_data_emit_detect_command(ui_detect_command_t cmd)
{
    if (cmd == UI_DETECT_CMD_START) {
        s_detect_snapshot.run_state = UI_DETECT_RUN_RUNNING;
        s_detect_snapshot.stage = UI_DETECT_STAGE_TRIG;
    } else if (cmd == UI_DETECT_CMD_STOP) {
        s_detect_snapshot.run_state = UI_DETECT_RUN_STOPPED;
        s_detect_snapshot.stage = UI_DETECT_STAGE_IDLE;
    } else if (cmd == UI_DETECT_CMD_REFRESH) {
        s_detect_snapshot.camera.frame_id++;
        s_detect_snapshot.stage = UI_DETECT_STAGE_TRIG;
    }
}

/* 推进“最近一次检测流水线动画”，让快速检测过程在 UI 上变得可感知。 */
void ui_fake_data_tick_detect_pipeline(uint32_t now_ms)
{
#if UI_FAKE_DETECT_PIPELINE_ANIM
    static ui_detect_stage_t s_last_stage = UI_DETECT_STAGE_IDLE;
    ui_detect_stage_t next_stage = UI_DETECT_STAGE_IDLE;
    uint32_t cycle_ms = 1780;
    uint32_t t = now_ms % cycle_ms;

    if (s_detect_snapshot.run_state != UI_DETECT_RUN_RUNNING) {
        s_detect_snapshot.stage = UI_DETECT_STAGE_IDLE;
        s_last_stage = UI_DETECT_STAGE_IDLE;
        return;
    }

    /*
     * 这里不是底层真实耗时，而是 UI 最小显示时间：
     * 等待产品 -> TRIG -> CAP -> AI -> OUT 保持结果。
     */
    if (t < 520) {
        next_stage = UI_DETECT_STAGE_IDLE;
    } else if (t < 680) {
        next_stage = UI_DETECT_STAGE_TRIG;
    } else if (t < 840) {
        next_stage = UI_DETECT_STAGE_CAPTURE;
    } else if (t < 1060) {
        next_stage = UI_DETECT_STAGE_AI;
    } else {
        next_stage = UI_DETECT_STAGE_OUTPUT;
    }

    if ((s_last_stage != next_stage) && (next_stage == UI_DETECT_STAGE_CAPTURE)) {
        s_detect_snapshot.camera.frame_id++;
    }

    s_detect_snapshot.stage = next_stage;
    s_last_stage = next_stage;
#else
    (void)now_ms;
#endif
}

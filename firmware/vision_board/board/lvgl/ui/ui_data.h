#ifndef UI_DATA_H
#define UI_DATA_H

#include <stdbool.h>
#include <stdint.h>

/*
 * UI 数据接口层。
 *
 * 页面代码只从这里取数据、发命令，不直接依赖摄像头、AI、PWM、Modbus 等底层模块。
 * 现在 ui_data.c 里是 PC 假数据；后续接真实系统时，优先替换这里的数据来源。
 */

typedef enum {
    UI_DETECT_RUN_STOPPED = 0,
    UI_DETECT_RUN_RUNNING,
} ui_detect_run_state_t;

/*
 * 单次检测流程阶段。
 * 真机中建议由检测线程更新：触发 -> 采图 -> AI 推理 -> 输出结果。
 * UI 只读取这个状态点亮流程灯，不直接控制底层硬件。
 */
typedef enum {
    UI_DETECT_STAGE_IDLE = 0,
    UI_DETECT_STAGE_TRIG,
    UI_DETECT_STAGE_CAPTURE,
    UI_DETECT_STAGE_AI,
    UI_DETECT_STAGE_OUTPUT,
} ui_detect_stage_t;

typedef enum {
    UI_DETECT_CMD_START = 0,
    UI_DETECT_CMD_STOP,
    UI_DETECT_CMD_REFRESH,
} ui_detect_command_t;

typedef void (*ui_detect_command_cb_t)(ui_detect_command_t cmd, void *user_data);

typedef enum {
    UI_STORAGE_OK = 0,
    UI_STORAGE_MISSING,
    UI_STORAGE_FULL,
    UI_STORAGE_ERROR,
} ui_storage_state_t;

typedef enum {
    UI_DATA_CMD_EXPORT_CSV = 0,
    UI_DATA_CMD_CLEAR_LOGS,
    UI_DATA_CMD_INIT_STORAGE,
} ui_data_command_t;

typedef void (*ui_data_command_cb_t)(ui_data_command_t cmd, void *user_data);

/*
 * AI 后处理类型。
 * 后续 SD 卡中可能有不同模型：Visual AD、FOMO 或自定义输出。
 * UI 只读取这个枚举做显示和调试提示，不直接依赖具体 SDK 函数。
 */
typedef enum {
    UI_AI_POSTPROCESS_NONE = 0,
    UI_AI_POSTPROCESS_VISUAL_AD,
    UI_AI_POSTPROCESS_FOMO,
    UI_AI_POSTPROCESS_CUSTOM,
} ui_ai_postprocess_t;

/*
 * 当前加载模型的公开信息。
 * 这些字段来自模型 manifest、Edge Impulse 元数据或 TFLite 输出维度。
 * 注意：grid_w/grid_h 表示真实模型输出或后处理得到的网格，不在 UI 中写死。
 */
typedef struct {
    const char *name;
    ui_ai_postprocess_t postprocess;
    uint16_t input_w;
    uint16_t input_h;
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t threshold_x10;
    uint8_t tflite_count;
    uint32_t arena_size;
} ui_ai_model_info_t;

/*
 * AI 异常热力图。
 * score_map 是按行优先排列的 0..255 显示强度图：
 * - 0 表示不显示
 * - 255 表示最强显示
 *
 * 注意：这个 0..255 不是 Edge Impulse 的原始输出。
 * Edge Impulse Visual AD 的原始格子分数是 float，公开结果在
 * result.visual_ad_grid_cells[i].value，完整网格可来自 raw_output_mtx->buffer。
 * 后续真实接入时，由 ui_ai_adapter.c 把 float 分数按阈值压缩成 0..255。
 */
typedef struct {
    uint16_t grid_w;/// 网格宽度
    uint16_t grid_h;/// 网格高度
    uint16_t roi_x; /// ROI 起始 X 坐标
    uint16_t roi_y; /// ROI 起始 Y 坐标
    uint16_t roi_w; /// ROI 宽度
    uint16_t roi_h; /// ROI 高度
    const uint8_t *score_map;/// 异常强度图
    uint16_t score_count;/// 异常强度图中的像素点数量
} ui_ai_heatmap_t;

typedef struct {
    const char *pixel_format;
    const char *frame_size_name;
    uint16_t frame_w;
    uint16_t frame_h;
    uint32_t frame_id;

    /*
     * ROI 用源图坐标描述。
     * OV5640 输出 320x240，当前 ROI 固定为 240x240，只允许 roi_x 在 0..80 内左右移动。
     */
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_size;
} ui_camera_data_t;

typedef struct {
    /*
     * anomaly 是最终判定结果：
     * - false：正常
     * - true ：异常
     *
     * 当前建议判定规则：
     * score_x10 > threshold_x10 时判为异常。
     */
    bool anomaly;

    /*
     * 异常分数，放大 10 倍保存，避免在 MCU UI 层使用 float。
     * 例如：
     * - 32 表示 3.2
     * - 87 表示 8.7
     *
     * 注意：这不是概率，也不是准确率。
     * 对 Visual AD/FOMO-AD 来说，它更像“异常强度分数”，只适合和同一模型的 threshold 比较。
     */
    uint16_t score_x10;

    /*
     * 异常判定阈值，同样放大 10 倍保存。
     * 例如 59 表示阈值 5.9。
     */
    uint16_t threshold_x10;

    /*
     * 平均异常分数，放大 10 倍保存。
     * 用来辅助展示整体异常程度，不直接替代最终判定。
     */
    uint16_t mean_score_x10;

    /* 热力图中超过显示阈值的格子数量，用来表示异常区域范围。 */
    uint8_t hot_cells;

    /* 单次 AI 推理耗时，单位 ms。 */
    uint16_t ai_time_ms;

    /* 实时帧率，放大 10 倍保存；168 表示 16.8 FPS。 */
    uint16_t fps_x10;

    /* 今日良率，放大 10 倍保存；992 表示 99.2%。 */
    uint16_t yield_x10;
} ui_ai_result_data_t;

typedef struct {
    ui_detect_run_state_t run_state;
    ui_detect_stage_t stage;
    ui_ai_model_info_t model;
    ui_camera_data_t camera;
    ui_ai_result_data_t ai;
} ui_detect_snapshot_t;

/*
 * 数据页顶部统计摘要。
 * 真机中建议由检测记录模块和采集记录模块定期更新，UI 只读取这个快照。
 */
typedef struct {
    uint16_t total_records;
    uint16_t total_samples;
    uint16_t ng_count;
    uint16_t ng_rate_x10;
    uint16_t trend_count;
    const uint16_t *trend_score_x10;
} ui_data_summary_t;

/*
 * SD 卡/文件系统状态。
 * 单位用 MB，避免 UI 层处理浮点数，也方便 RT-Thread 文件系统模块填充。
 */
typedef struct {
    ui_storage_state_t state;
    uint32_t total_mb;
    uint32_t used_mb;
    uint32_t free_mb;
    const char *record_dir;
    const char *sample_dir;
    const char *export_dir;
} ui_storage_info_t;

/*
 * 最近检测记录。
 * image_path 可以为空字符串；真实系统中建议记录到 CSV，图片文件只保存路径。
 */
typedef struct {
    const char *time;
    bool anomaly;
    uint16_t score_x10;
    uint16_t threshold_x10;
    const char *model_name;
    const char *image_path;
} ui_detect_record_t;

const ui_detect_snapshot_t *ui_data_get_detect_snapshot(void);

/*
 * 返回当前 AI 异常热力图。
 * PC 阶段返回假数据；真实系统中建议返回 AI 推理线程写好的结果缓存。
 */
const ui_ai_heatmap_t *ui_data_get_detect_heatmap(void);

/* 返回数据页统计摘要；当前是 PC 假数据，真机中可替换为记录模块缓存。 */
const ui_data_summary_t *ui_data_get_data_summary(void);

/* 返回 SD 卡/文件系统状态；真实系统中由文件系统模块填充。 */
const ui_storage_info_t *ui_data_get_storage_info(void);

/* 返回最近检测记录数组；count 返回记录条数。 */
const ui_detect_record_t *ui_data_get_recent_records(uint16_t *count);

/*
 * 设置真实业务回调。
 * 后续接入 RT-Thread/真实检测线程时，可以在初始化时注册这个回调。
 */
void ui_data_set_detect_command_handler(ui_detect_command_cb_t cb, void *user_data);

/*
 * 设置数据页命令回调。
 * 后续 Export CSV / Clear Logs 会通过这里交给真实文件系统任务处理。
 */
void ui_data_set_data_command_handler(ui_data_command_cb_t cb, void *user_data);

/*
 * UI 按钮调用这个函数发命令。
 * 当前 PC 假数据会在这里模拟状态变化；真实系统可在回调里处理硬件动作。
 */
void ui_data_emit_detect_command(ui_detect_command_t cmd);

/* 数据页按钮调用这个函数发命令；页面不直接操作 SD 卡或文件系统。 */
void ui_data_emit_data_command(ui_data_command_t cmd);

/*
 * 推进检测流程灯的显示状态。
 *
 * PC 模拟阶段：这个函数会播放“最近一次检测流水线动画”。
 * 真实设备阶段：可以改成读取真实检测事件时间戳，再做 UI 最小显示时间处理。
 */
void ui_data_tick_detect_pipeline(uint32_t now_ms);

#endif /* UI_DATA_H */

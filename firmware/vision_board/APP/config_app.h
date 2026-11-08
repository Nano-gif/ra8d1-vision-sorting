#ifndef CONFIG_APP_H
#define CONFIG_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_APP_MODEL_NAME_MAX 24U

typedef enum
{
    CONFIG_APP_LANGUAGE_ZH = 0,
    CONFIG_APP_LANGUAGE_EN = 1,
} config_app_language_t;

typedef struct
{
    char model[CONFIG_APP_MODEL_NAME_MAX];
    config_app_language_t language;
    uint8_t brightness_percent;
    uint8_t light_enabled;
    uint8_t light_level;
    uint8_t save_detect_image;
} config_app_t;

/*
 * 返回 /vision/config.ini 在内存中的副本（只读，调用方不要直接修改）。
 */
const config_app_t *config_app_get(void);

/*
 * 查询 config.ini 是否已从 TF 卡加载过。
 *
 * 返回：
 * - RT_TRUE：当前内存配置来自 config.ini；
 * - RT_FALSE：当前内存配置仅为默认值或 UI 修改后的缓存。
 */
rt_bool_t config_app_is_loaded(void);

/*
 * 清除“已加载”标志，使下次 UI/设置页加载时重新读取 config.ini。
 */
void config_app_mark_unloaded(void);

/*
 * 将 /vision/config.ini 加载到内存。
 *
 * 返回：
 * - RT_EOK：加载成功，缺失的键保留默认值；
 * - -RT_ERROR：TF 卡/存储未就绪或读取失败。
 */
int config_app_load(void);

/*
 * 将当前内存配置保存到 /vision/config.ini。
 *
 * 返回：
 * - RT_EOK：写入成功；
 * - -RT_ERROR：TF 卡/存储未就绪或写入失败。
 */
int config_app_save(void);

/*
 * 更新缓存中的默认模型目录名，例如 "cap_v8_96"。
 */
void config_app_set_model(const char *model);

/*
 * 更新缓存中的界面语言：CONFIG_APP_LANGUAGE_ZH 或 CONFIG_APP_LANGUAGE_EN。
 */
void config_app_set_language(config_app_language_t language);

/*
 * 更新缓存中的背光亮度百分比，超过 100 时按 100 处理。
 */
void config_app_set_brightness(uint8_t percent);

/*
 * 更新缓存中的光源使能状态：0 关闭，非 0 开启。
 */
void config_app_set_light_enabled(uint8_t enabled);

/*
 * 更新缓存中的光源亮度等级 0..10，超过 10 时按 10 处理。
 */
void config_app_set_light_level(uint8_t level);

/*
 * 更新缓存中的检测图像保存模式：0 表示不保存。
 */
void config_app_set_save_detect_image(uint8_t mode);

/*
 * 将语言枚举转换为 config.ini 中的字符串："zh" 或 "en"。
 */
const char *config_app_language_string(config_app_language_t language);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_APP_H */

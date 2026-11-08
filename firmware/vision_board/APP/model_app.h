#ifndef MODEL_APP_H
#define MODEL_APP_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_APP_PATH_MAX 128U
#define MODEL_APP_NAME_MAX 32U
#define MODEL_APP_PROFILE_MAX 24U
#define MODEL_APP_TYPE_MAX 24U
#define MODEL_APP_FILE_MAX 48U

typedef enum
{
    MODEL_APP_SOURCE_BUILTIN = 0,
    MODEL_APP_SOURCE_SDCARD,
} model_app_source_t;

typedef struct
{
    char name[MODEL_APP_NAME_MAX];
    char profile[MODEL_APP_PROFILE_MAX];
    char type[MODEL_APP_TYPE_MAX];
    char main_file[MODEL_APP_FILE_MAX];
    char anomaly_file[MODEL_APP_FILE_MAX];
    uint16_t input_w;
    uint16_t input_h;
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t threshold_x10;
    uint8_t tflite_count;
    uint32_t arena_size;
    uint32_t arena0_size;
    uint32_t arena1_size;
    rt_bool_t files_present;
    rt_bool_t runtime_supported;
} model_app_package_info_t;

typedef struct
{
    const char *name;
    const char *profile;
    const char *type;
    const char *package_dir;
    const char *main_tflite_path;
    const char *anomaly_tflite_path;
    uint16_t input_w;
    uint16_t input_h;
    uint16_t grid_w;
    uint16_t grid_h;
    uint16_t threshold_x10;
    uint8_t tflite_count;
    uint32_t main_tflite_size;
    uint32_t anomaly_tflite_size;
    uint32_t arena0_size;
    uint32_t arena1_size;
    model_app_source_t source;
    rt_bool_t files_present;
    rt_bool_t metadata_loaded;
    rt_bool_t sdram_loaded;
    rt_bool_t runtime_supported;
} model_app_state_t;

/*
 * Load one model package selected by the Settings page.
 *
 * Parameters:
 * - package_dir: Model package folder, for example "/vision/models/cap_v15_96/".
 *
 * Return:
 * - RT_EOK: Model metadata was applied to detect_app. If .tflite files are not
 *   present yet, files_present is false but the profile is still usable by the
 *   current fake backend.
 * - -RT_ERROR: package_dir is invalid or the profile cannot be applied.
 *
 * Note:
 * - The real Edge Impulse migration should add SDRAM allocation and file reads
 *   inside this module, not inside the LVGL page code.
 */
int model_app_load_package(const char *package_dir);

/*
 * Read model package metadata from one folder.
 *
 * Parameters:
 * - package_dir: Model package folder, for example "/vision/models/product_a".
 * - out_info: Output metadata. The folder name is used as the display name
 *   when model.ini does not provide a name field.
 *
 * Return:
 * - RT_EOK: Metadata was filled. Missing model.ini falls back to cap_v15_96.
 * - -RT_ERROR: Invalid parameters.
 *
 * Expected model.ini keys for the first version:
 * - name=product_a
 * - profile=cap_v15_96
 * - input=96x96
 * - grid=5x5
 * - threshold=9.3
 * - arena0=183808
 * - arena1=76192
 */
int model_app_read_package_info(const char *package_dir,
                                model_app_package_info_t *out_info);

/*
 * Copy the current model-loader state.
 *
 * Parameters:
 * - out_state: Output buffer filled with the latest loader state.
 *
 * Return:
 * - RT_EOK: State copied.
 * - -RT_ERROR: out_state is NULL.
 */
int model_app_get_state(model_app_state_t *out_state);

/*
 * Return a short text for the last loader error.
 *
 * Parameters:
 * - None.
 *
 * Return:
 * - Static error string.
 */
const char *model_app_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_APP_H */

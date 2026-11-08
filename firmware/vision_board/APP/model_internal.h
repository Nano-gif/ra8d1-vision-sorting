#ifndef MODEL_INTERNAL_H
#define MODEL_INTERNAL_H

#include "model_app.h"

#include "detect_app.h"
#include "ai_app.h"
#include "hal_data.h"
#include "storage_app.h"
#ifdef APP_USING_EDGE_IMPULSE_V15
#include "ei_v15_inference.h"
#endif

#include <dfs_file.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MODEL_APP_MAIN_TFLITE_NAME      "model.tflite"
#define MODEL_APP_ANOMALY_TFLITE_NAME   "anomaly_model.tflite"
#define MODEL_APP_INI_NAME              "model.ini"
#define MODEL_APP_LINE_MAX              96U
#define MODEL_APP_DEFAULT_ARENA0_SIZE   AI_APP_ARENA_SIZE
#define MODEL_APP_DEFAULT_ARENA1_SIZE   76192U
#define MODEL_APP_MAX_GRAPH0_BYTES      (256U * 1024U)
#define MODEL_APP_MAX_GRAPH1_BYTES      (128U * 1024U)

/* 模块内部共享接口，仅供本模块的拆分源文件使用。 */
extern model_app_state_t g_model_state;
extern const char *g_model_last_error;
void model_app_init_builtin_state(void);
void model_app_build_package_path(const char *name_or_path,
                                  char *out_path,
                                  uint32_t out_size);
int model_app_refresh_file_state(const char *package_dir, model_app_package_info_t *info);

#endif /* MODEL_INTERNAL_H */

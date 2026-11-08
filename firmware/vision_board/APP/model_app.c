#include "model_internal.h"

static char s_model_name[32] = "unloaded";
static char s_model_profile[MODEL_APP_PROFILE_MAX] = "unloaded";
static char s_model_type[MODEL_APP_TYPE_MAX] = "none";
static char s_package_dir[MODEL_APP_PATH_MAX] = "";
static char s_main_tflite_path[MODEL_APP_PATH_MAX] = "";
static char s_anomaly_tflite_path[MODEL_APP_PATH_MAX] = "";
model_app_state_t g_model_state;
static rt_bool_t s_model_state_initialized = RT_FALSE;
const char *g_model_last_error = "not loaded";
static uint8_t s_model_graph0_buf[MODEL_APP_MAX_GRAPH0_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");
static uint8_t s_model_graph1_buf[MODEL_APP_MAX_GRAPH1_BYTES]
BSP_ALIGN_VARIABLE(64)
BSP_PLACE_IN_SECTION(".sdram");

static int model_app_set_paths(const char *package_dir,
                               const model_app_package_info_t *info);
static void model_app_select_builtin_state(const char *last_error);
static rt_bool_t model_app_directory_exists(const char *path);
static void model_app_get_folder_name(const char *package_dir, char *out_name, uint32_t out_size);
static void model_app_fill_default_info(const char *package_dir, model_app_package_info_t *out_info);
static void model_app_trim(char *text);
static int model_app_parse_x10(const char *text, uint16_t *out_value);
static int model_app_parse_u32(const char *text, uint32_t *out_value);
static int model_app_parse_pair_u16(const char *text,
                                    char separator,
                                    uint16_t *out_a,
                                    uint16_t *out_b);
static void model_app_apply_ini_key(model_app_package_info_t *info,
                                    const char *key,
                                    const char *value);
static int model_app_get_file_size(const char *path, uint32_t *out_size);
static int model_app_read_file_exact(const char *path, uint8_t *dst, uint32_t expected_size);
static rt_bool_t model_app_is_runtime_supported(const model_app_package_info_t *info);
static int model_app_validate_package(const model_app_package_info_t *info,
                                      uint32_t main_size,
                                      uint32_t anomaly_size);
static int model_app_load_tflite_files(const model_app_package_info_t *info,
                                       uint32_t main_size,
                                       uint32_t anomaly_size);

int model_app_load_package(const char *package_dir)
{
    uint32_t main_size = 0U;
    uint32_t anomaly_size = 0U;
    int main_ret;
    int anomaly_ret;
    int tflite_ret = -RT_ERROR;
    model_app_package_info_t package_info;

    model_app_init_builtin_state();

    if ((package_dir == RT_NULL) || (package_dir[0] == '\0'))
    {
        g_model_last_error = "invalid model path";
        return -RT_ERROR;
    }

    if (model_app_directory_exists(package_dir) != RT_TRUE)
    {
        g_model_last_error = "model directory missing";
        return -RT_ERROR;
    }

    if (model_app_read_package_info(package_dir, &package_info) != RT_EOK)
    {
        g_model_last_error = "read metadata failed";
        return -RT_ERROR;
    }

    if (package_info.runtime_supported != RT_TRUE)
    {
        g_model_last_error = "runtime profile unsupported";
        return -RT_ERROR;
    }

    {
        char candidate_main_path[MODEL_APP_PATH_MAX];
        char candidate_anomaly_path[MODEL_APP_PATH_MAX];
        int ret;

        ret = rt_snprintf(candidate_main_path,
                          sizeof(candidate_main_path),
                          "%s/%s",
                          package_dir,
                          package_info.main_file);
        if ((ret <= 0) || ((uint32_t)ret >= sizeof(candidate_main_path)))
        {
            g_model_last_error = "model path too long";
            return -RT_ERROR;
        }

        ret = rt_snprintf(candidate_anomaly_path,
                          sizeof(candidate_anomaly_path),
                          "%s/%s",
                          package_dir,
                          package_info.anomaly_file);
        if ((ret <= 0) || ((uint32_t)ret >= sizeof(candidate_anomaly_path)))
        {
            g_model_last_error = "model path too long";
            return -RT_ERROR;
        }

        main_ret = model_app_get_file_size(candidate_main_path, &main_size);
        anomaly_ret = model_app_get_file_size(candidate_anomaly_path, &anomaly_size);
        if ((main_ret != RT_EOK) || (anomaly_ret != RT_EOK))
        {
            g_model_last_error = "tflite files missing";
            return -RT_ERROR;
        }
    }

    if (detect_app_can_switch_model() != RT_EOK)
    {
        g_model_last_error = "detect busy";
        return -RT_ERROR;
    }

    if (model_app_set_paths(package_dir, &package_info) != RT_EOK)
    {
        g_model_last_error = "model path too long";
        return -RT_ERROR;
    }

    (void)model_app_refresh_file_state(s_package_dir, &package_info);

    rt_strncpy(s_model_name, package_info.name, sizeof(s_model_name) - 1U);
    s_model_name[sizeof(s_model_name) - 1U] = '\0';
    rt_strncpy(s_model_profile, package_info.profile, sizeof(s_model_profile) - 1U);
    s_model_profile[sizeof(s_model_profile) - 1U] = '\0';
    rt_strncpy(s_model_type, package_info.type, sizeof(s_model_type) - 1U);
    s_model_type[sizeof(s_model_type) - 1U] = '\0';

    tflite_ret = model_app_load_tflite_files(&package_info, main_size, anomaly_size);
    if (tflite_ret != RT_EOK)
    {
        model_app_select_builtin_state(g_model_last_error);
        return -RT_ERROR;
    }

    if (detect_app_apply_model_metadata(package_info.name,
                                        package_info.input_w,
                                        package_info.input_h,
                                        package_info.grid_w,
                                        package_info.grid_h,
                                        package_info.threshold_x10,
                                        package_info.tflite_count,
                                        package_info.arena0_size) != RT_EOK)
    {
        model_app_select_builtin_state("apply runtime metadata failed");
        return -RT_ERROR;
    }

    g_model_state.name = s_model_name;
    g_model_state.profile = s_model_profile;
    g_model_state.type = s_model_type;
    g_model_state.package_dir = s_package_dir;
    g_model_state.main_tflite_path = s_main_tflite_path;
    g_model_state.anomaly_tflite_path = s_anomaly_tflite_path;
    g_model_state.input_w = package_info.input_w;
    g_model_state.input_h = package_info.input_h;
    g_model_state.grid_w = package_info.grid_w;
    g_model_state.grid_h = package_info.grid_h;
    g_model_state.threshold_x10 = package_info.threshold_x10;
    g_model_state.tflite_count = package_info.tflite_count;
    g_model_state.main_tflite_size = main_size;
    g_model_state.anomaly_tflite_size = anomaly_size;
    g_model_state.arena0_size = package_info.arena0_size;
    g_model_state.arena1_size = package_info.arena1_size;
    g_model_state.source = (tflite_ret == RT_EOK) ? MODEL_APP_SOURCE_SDCARD : MODEL_APP_SOURCE_BUILTIN;
    g_model_state.files_present = ((main_ret == RT_EOK) && (anomaly_ret == RT_EOK)) ? RT_TRUE : RT_FALSE;
    g_model_state.metadata_loaded = RT_TRUE;
    g_model_state.sdram_loaded = (tflite_ret == RT_EOK) ? RT_TRUE : RT_FALSE;
    g_model_state.runtime_supported = package_info.runtime_supported;

    if (g_model_state.sdram_loaded == RT_TRUE)
    {
        g_model_last_error = "ok";
    }
    else if (g_model_state.runtime_supported != RT_TRUE)
    {
        g_model_last_error = "runtime profile unsupported";
    }
    else if (g_model_state.files_present == RT_TRUE)
    {
        g_model_last_error = "tflite load failed";
    }
    else
    {
        g_model_last_error = "tflite files missing";
    }
    return RT_EOK;
}

int model_app_read_package_info(const char *package_dir,
                                model_app_package_info_t *out_info)
{
    char ini_path[MODEL_APP_PATH_MAX];
    char line[MODEL_APP_LINE_MAX];
    int fd;
    uint32_t pos = 0U;
    char ch;

    if ((package_dir == RT_NULL) || (package_dir[0] == '\0') || (out_info == RT_NULL))
    {
        return -RT_ERROR;
    }

    model_app_fill_default_info(package_dir, out_info);

    rt_snprintf(ini_path, sizeof(ini_path), "%s/%s", package_dir, MODEL_APP_INI_NAME);
    fd = open(ini_path, O_RDONLY, 0);
    if (fd < 0)
    {
        return RT_EOK;
    }

    while (read(fd, &ch, 1) == 1)
    {
        if ((ch == '\n') || (ch == '\r'))
        {
            if (pos > 0U)
            {
                char *eq;

                line[pos] = '\0';
                pos = 0U;
                model_app_trim(line);

                if ((line[0] == '\0') || (line[0] == '#'))
                {
                    continue;
                }

                eq = strchr(line, '=');
                if (eq == RT_NULL)
                {
                    continue;
                }

                *eq = '\0';
                model_app_trim(line);
                model_app_trim(eq + 1);
                model_app_apply_ini_key(out_info, line, eq + 1);
            }
        }
        else if (pos < (sizeof(line) - 1U))
        {
            line[pos++] = ch;
        }
    }

    if (pos > 0U)
    {
        char *eq;

        line[pos] = '\0';
        model_app_trim(line);
        eq = strchr(line, '=');
        if ((line[0] != '\0') && (line[0] != '#') && (eq != RT_NULL))
        {
            *eq = '\0';
            model_app_trim(line);
            model_app_trim(eq + 1);
            model_app_apply_ini_key(out_info, line, eq + 1);
        }
    }

    close(fd);
    return RT_EOK;
}

int model_app_get_state(model_app_state_t *out_state)
{
    if (out_state == RT_NULL)
    {
        return -RT_ERROR;
    }

    model_app_init_builtin_state();
    *out_state = g_model_state;
    return RT_EOK;
}

const char *model_app_last_error(void)
{
    return g_model_last_error;
}

static int model_app_set_paths(const char *package_dir,
                               const model_app_package_info_t *info)
{
    uint32_t len;
    int ret;

    if ((package_dir == RT_NULL) || (info == RT_NULL))
    {
        return -RT_ERROR;
    }

    rt_strncpy(s_package_dir, package_dir, sizeof(s_package_dir) - 1U);
    s_package_dir[sizeof(s_package_dir) - 1U] = '\0';

    len = (uint32_t)strlen(s_package_dir);
    if ((len > 0U) && (s_package_dir[len - 1U] == '/'))
    {
        s_package_dir[len - 1U] = '\0';
    }

    ret = rt_snprintf(s_main_tflite_path,
                      sizeof(s_main_tflite_path),
                      "%s/%s",
                      s_package_dir,
                      info->main_file);
    if ((ret <= 0) || ((uint32_t)ret >= sizeof(s_main_tflite_path)))
    {
        return -RT_ERROR;
    }

    ret = rt_snprintf(s_anomaly_tflite_path,
                      sizeof(s_anomaly_tflite_path),
                      "%s/%s",
                      s_package_dir,
                      info->anomaly_file);
    if ((ret <= 0) || ((uint32_t)ret >= sizeof(s_anomaly_tflite_path)))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

void model_app_init_builtin_state(void)
{
    if (s_model_state_initialized == RT_TRUE)
    {
        return;
    }

    g_model_state.name = s_model_name;
    g_model_state.profile = s_model_profile;
    g_model_state.type = s_model_type;
    g_model_state.package_dir = s_package_dir;
    g_model_state.main_tflite_path = s_main_tflite_path;
    g_model_state.anomaly_tflite_path = s_anomaly_tflite_path;
    g_model_state.input_w = AI_APP_INPUT_W;
    g_model_state.input_h = AI_APP_INPUT_H;
    g_model_state.grid_w = AI_APP_GRID_W;
    g_model_state.grid_h = AI_APP_GRID_H;
    g_model_state.threshold_x10 = AI_APP_THRESHOLD_X10;
    g_model_state.tflite_count = 2U;
    g_model_state.main_tflite_size = 0U;
    g_model_state.anomaly_tflite_size = 0U;
    g_model_state.arena0_size = MODEL_APP_DEFAULT_ARENA0_SIZE;
    g_model_state.arena1_size = MODEL_APP_DEFAULT_ARENA1_SIZE;
    g_model_state.source = MODEL_APP_SOURCE_BUILTIN;
    g_model_state.files_present = RT_FALSE;
    g_model_state.metadata_loaded = RT_FALSE;
    g_model_state.sdram_loaded = RT_FALSE;
    g_model_state.runtime_supported = RT_TRUE;
    s_model_state_initialized = RT_TRUE;
}

static void model_app_select_builtin_state(const char *last_error)
{
    rt_strncpy(s_model_name, "unloaded", sizeof(s_model_name) - 1U);
    s_model_name[sizeof(s_model_name) - 1U] = '\0';
    rt_strncpy(s_model_profile, "unloaded", sizeof(s_model_profile) - 1U);
    s_model_profile[sizeof(s_model_profile) - 1U] = '\0';
    rt_strncpy(s_model_type, "none", sizeof(s_model_type) - 1U);
    s_model_type[sizeof(s_model_type) - 1U] = '\0';
    s_package_dir[0] = '\0';
    s_package_dir[sizeof(s_package_dir) - 1U] = '\0';
    s_main_tflite_path[0] = '\0';
    s_main_tflite_path[sizeof(s_main_tflite_path) - 1U] = '\0';
    s_anomaly_tflite_path[0] = '\0';
    s_anomaly_tflite_path[sizeof(s_anomaly_tflite_path) - 1U] = '\0';

#ifdef APP_USING_EDGE_IMPULSE_V15
    ei_v15_use_builtin_model();
#endif
    (void)detect_app_apply_model_metadata(s_model_name,
                                          AI_APP_INPUT_W,
                                          AI_APP_INPUT_H,
                                          AI_APP_GRID_W,
                                          AI_APP_GRID_H,
                                          AI_APP_THRESHOLD_X10,
                                          2U,
                                          MODEL_APP_DEFAULT_ARENA0_SIZE);

    g_model_state.name = s_model_name;
    g_model_state.profile = s_model_profile;
    g_model_state.type = s_model_type;
    g_model_state.package_dir = s_package_dir;
    g_model_state.main_tflite_path = s_main_tflite_path;
    g_model_state.anomaly_tflite_path = s_anomaly_tflite_path;
    g_model_state.input_w = AI_APP_INPUT_W;
    g_model_state.input_h = AI_APP_INPUT_H;
    g_model_state.grid_w = AI_APP_GRID_W;
    g_model_state.grid_h = AI_APP_GRID_H;
    g_model_state.threshold_x10 = AI_APP_THRESHOLD_X10;
    g_model_state.tflite_count = 2U;
    g_model_state.main_tflite_size = 0U;
    g_model_state.anomaly_tflite_size = 0U;
    g_model_state.arena0_size = MODEL_APP_DEFAULT_ARENA0_SIZE;
    g_model_state.arena1_size = MODEL_APP_DEFAULT_ARENA1_SIZE;
    g_model_state.source = MODEL_APP_SOURCE_BUILTIN;
    g_model_state.files_present = RT_FALSE;
    g_model_state.metadata_loaded = RT_FALSE;
    g_model_state.sdram_loaded = RT_FALSE;
    g_model_state.runtime_supported = RT_TRUE;
    s_model_state_initialized = RT_TRUE;
    g_model_last_error = (last_error != RT_NULL) ? last_error : "builtin";
}

static rt_bool_t model_app_directory_exists(const char *path)
{
    DIR *dir;

    if ((path == RT_NULL) || (path[0] == '\0'))
    {
        return RT_FALSE;
    }

    dir = opendir(path);
    if (dir == RT_NULL)
    {
        return RT_FALSE;
    }

    closedir(dir);
    return RT_TRUE;
}

void model_app_build_package_path(const char *name_or_path,
                                         char *out_path,
                                         uint32_t out_size)
{
    int ret;

    if ((out_path == RT_NULL) || (out_size == 0U))
    {
        return;
    }

    out_path[0] = '\0';
    if ((name_or_path == RT_NULL) || (name_or_path[0] == '\0'))
    {
        rt_strncpy(out_path, "/vision/models/cap_v15_96", out_size - 1U);
        out_path[out_size - 1U] = '\0';
        return;
    }

    if (name_or_path[0] == '/')
    {
        rt_strncpy(out_path, name_or_path, out_size - 1U);
        out_path[out_size - 1U] = '\0';
        return;
    }

    ret = rt_snprintf(out_path, out_size, "%s/%s", STORAGE_MODELS_DIR, name_or_path);
    if ((ret <= 0) || ((uint32_t)ret >= out_size))
    {
        out_path[0] = '\0';
    }
}

static void model_app_get_folder_name(const char *package_dir, char *out_name, uint32_t out_size)
{
    const char *end;
    const char *start;
    uint32_t len;

    if ((out_name == RT_NULL) || (out_size == 0U))
    {
        return;
    }

    out_name[0] = '\0';
    if ((package_dir == RT_NULL) || (package_dir[0] == '\0'))
    {
        rt_strncpy(out_name, "cap_v15_96", out_size - 1U);
        out_name[out_size - 1U] = '\0';
        return;
    }

    end = package_dir + strlen(package_dir);
    while ((end > package_dir) && (*(end - 1) == '/'))
    {
        end--;
    }

    start = end;
    while ((start > package_dir) && (*(start - 1) != '/'))
    {
        start--;
    }

    len = (uint32_t)(end - start);
    if (len == 0U)
    {
        rt_strncpy(out_name, "cap_v15_96", out_size - 1U);
    }
    else
    {
        if (len >= out_size)
        {
            len = out_size - 1U;
        }
        rt_memcpy(out_name, start, len);
        out_name[len] = '\0';
    }
}

static void model_app_fill_default_info(const char *package_dir, model_app_package_info_t *out_info)
{
    rt_memset(out_info, 0, sizeof(*out_info));
    model_app_get_folder_name(package_dir, out_info->name, sizeof(out_info->name));
    rt_strncpy(out_info->profile, "cap_v15_96", sizeof(out_info->profile) - 1U);
    rt_strncpy(out_info->type, "visual_gmm", sizeof(out_info->type) - 1U);
    rt_strncpy(out_info->main_file, MODEL_APP_MAIN_TFLITE_NAME, sizeof(out_info->main_file) - 1U);
    rt_strncpy(out_info->anomaly_file, MODEL_APP_ANOMALY_TFLITE_NAME, sizeof(out_info->anomaly_file) - 1U);
    out_info->input_w = 96U;
    out_info->input_h = 96U;
    out_info->grid_w = 5U;
    out_info->grid_h = 5U;
    out_info->threshold_x10 = AI_APP_THRESHOLD_X10;
    out_info->tflite_count = 2U;
    out_info->arena_size = AI_APP_ARENA_SIZE;
    out_info->arena0_size = MODEL_APP_DEFAULT_ARENA0_SIZE;
    out_info->arena1_size = MODEL_APP_DEFAULT_ARENA1_SIZE;
    out_info->files_present = RT_FALSE;
    out_info->runtime_supported = model_app_is_runtime_supported(out_info);
}

static void model_app_trim(char *text)
{
    char *start;
    char *end;

    if (text == RT_NULL)
    {
        return;
    }

    start = text;
    while ((*start == ' ') || (*start == '\t'))
    {
        start++;
    }

    if (start != text)
    {
        memmove(text, start, strlen(start) + 1U);
    }

    end = text + strlen(text);
    while ((end > text) && ((*(end - 1) == ' ') || (*(end - 1) == '\t')))
    {
        end--;
    }
    *end = '\0';
}

static int model_app_parse_x10(const char *text, uint16_t *out_value)
{
    uint32_t integer = 0U;
    uint32_t decimal = 0U;
    const char *cursor = text;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0'))
    {
        return -RT_ERROR;
    }

    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        integer = integer * 10U + (uint32_t)(*cursor - '0');
        cursor++;
    }

    if (*cursor == '.')
    {
        cursor++;
        if ((*cursor >= '0') && (*cursor <= '9'))
        {
            decimal = (uint32_t)(*cursor - '0');
        }
    }

    *out_value = (uint16_t)(integer * 10U + decimal);
    return RT_EOK;
}

static int model_app_parse_u32(const char *text, uint32_t *out_value)
{
    uint32_t value = 0U;
    const char *cursor = text;

    if ((text == RT_NULL) || (out_value == RT_NULL) || (text[0] == '\0'))
    {
        return -RT_ERROR;
    }

    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        value = value * 10U + (uint32_t)(*cursor - '0');
        cursor++;
    }

    if ((cursor == text) || (*cursor != '\0'))
    {
        return -RT_ERROR;
    }

    *out_value = value;
    return RT_EOK;
}

static int model_app_parse_pair_u16(const char *text,
                                    char separator,
                                    uint16_t *out_a,
                                    uint16_t *out_b)
{
    const char *sep;
    uint32_t a = 0U;
    uint32_t b = 0U;
    const char *cursor;

    if ((text == RT_NULL) || (out_a == RT_NULL) || (out_b == RT_NULL))
    {
        return -RT_ERROR;
    }

    sep = strchr(text, separator);
    if (sep == RT_NULL)
    {
        return -RT_ERROR;
    }

    cursor = text;
    while ((cursor < sep) && (*cursor >= '0') && (*cursor <= '9'))
    {
        a = a * 10U + (uint32_t)(*cursor - '0');
        cursor++;
    }

    cursor = sep + 1;
    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        b = b * 10U + (uint32_t)(*cursor - '0');
        cursor++;
    }

    if ((a == 0U) || (b == 0U))
    {
        return -RT_ERROR;
    }

    *out_a = (uint16_t)a;
    *out_b = (uint16_t)b;
    return RT_EOK;
}

static void model_app_apply_ini_key(model_app_package_info_t *info,
                                    const char *key,
                                    const char *value)
{
    uint16_t a;
    uint16_t b;
    uint16_t x10;

    if ((info == RT_NULL) || (key == RT_NULL) || (value == RT_NULL))
    {
        return;
    }

    if (rt_strcmp(key, "name") == 0)
    {
        rt_strncpy(info->name, value, sizeof(info->name) - 1U);
        info->name[sizeof(info->name) - 1U] = '\0';
    }
    else if (rt_strcmp(key, "profile") == 0)
    {
        rt_strncpy(info->profile, value, sizeof(info->profile) - 1U);
        info->profile[sizeof(info->profile) - 1U] = '\0';
    }
    else if ((rt_strcmp(key, "type") == 0) || (rt_strcmp(key, "model_type") == 0))
    {
        rt_strncpy(info->type, value, sizeof(info->type) - 1U);
        info->type[sizeof(info->type) - 1U] = '\0';
    }
    else if (rt_strcmp(key, "input") == 0)
    {
        if (model_app_parse_pair_u16(value, 'x', &a, &b) == RT_EOK)
        {
            info->input_w = a;
            info->input_h = b;
        }
    }
    else if (rt_strcmp(key, "grid") == 0)
    {
        if (model_app_parse_pair_u16(value, 'x', &a, &b) == RT_EOK)
        {
            info->grid_w = a;
            info->grid_h = b;
        }
    }
    else if (rt_strcmp(key, "threshold") == 0)
    {
        if (model_app_parse_x10(value, &x10) == RT_EOK)
        {
            info->threshold_x10 = x10;
        }
    }
    else if ((rt_strcmp(key, "tflite_count") == 0) || (rt_strcmp(key, "graphs") == 0))
    {
        uint32_t count;

        if (model_app_parse_u32(value, &count) == RT_EOK)
        {
            if (count > 0xFFU)
            {
                count = 0xFFU;
            }
            info->tflite_count = (uint8_t)count;
        }
    }
    else if ((rt_strcmp(key, "arena") == 0) || (rt_strcmp(key, "arena_size") == 0))
    {
        uint32_t arena_size;

        if (model_app_parse_u32(value, &arena_size) == RT_EOK)
        {
            info->arena_size = arena_size;
            info->arena0_size = arena_size;
        }
    }
    else if (rt_strcmp(key, "arena0") == 0)
    {
        uint32_t arena_size;

        if (model_app_parse_u32(value, &arena_size) == RT_EOK)
        {
            info->arena0_size = arena_size;
            info->arena_size = arena_size;
        }
    }
    else if (rt_strcmp(key, "arena1") == 0)
    {
        uint32_t arena_size;

        if (model_app_parse_u32(value, &arena_size) == RT_EOK)
        {
            info->arena1_size = arena_size;
        }
    }
    else if ((rt_strcmp(key, "main") == 0) || (rt_strcmp(key, "main_file") == 0))
    {
        rt_strncpy(info->main_file, value, sizeof(info->main_file) - 1U);
        info->main_file[sizeof(info->main_file) - 1U] = '\0';
    }
    else if ((rt_strcmp(key, "anomaly") == 0) || (rt_strcmp(key, "anomaly_file") == 0))
    {
        rt_strncpy(info->anomaly_file, value, sizeof(info->anomaly_file) - 1U);
        info->anomaly_file[sizeof(info->anomaly_file) - 1U] = '\0';
    }

    info->runtime_supported = model_app_is_runtime_supported(info);
}

static int model_app_get_file_size(const char *path, uint32_t *out_size)
{
    struct stat st;

    if (out_size != RT_NULL)
    {
        *out_size = 0U;
    }

    if ((path == RT_NULL) || (out_size == RT_NULL))
    {
        return -RT_ERROR;
    }

    if (stat(path, &st) != 0)
    {
        return -RT_ERROR;
    }

    if (st.st_size <= 0)
    {
        return -RT_ERROR;
    }

    *out_size = (uint32_t)st.st_size;
    return RT_EOK;
}

int model_app_refresh_file_state(const char *package_dir, model_app_package_info_t *info)
{
    char main_path[MODEL_APP_PATH_MAX];
    char anomaly_path[MODEL_APP_PATH_MAX];
    uint32_t main_size = 0U;
    uint32_t anomaly_size = 0U;
    int ret;

    if ((package_dir == RT_NULL) || (info == RT_NULL))
    {
        return -RT_ERROR;
    }

    ret = rt_snprintf(main_path, sizeof(main_path), "%s/%s", package_dir, info->main_file);
    if ((ret <= 0) || ((uint32_t)ret >= sizeof(main_path)))
    {
        info->files_present = RT_FALSE;
        return -RT_ERROR;
    }

    ret = rt_snprintf(anomaly_path, sizeof(anomaly_path), "%s/%s", package_dir, info->anomaly_file);
    if ((ret <= 0) || ((uint32_t)ret >= sizeof(anomaly_path)))
    {
        info->files_present = RT_FALSE;
        return -RT_ERROR;
    }

    info->files_present =
        ((model_app_get_file_size(main_path, &main_size) == RT_EOK) &&
         (model_app_get_file_size(anomaly_path, &anomaly_size) == RT_EOK)) ? RT_TRUE : RT_FALSE;
    return RT_EOK;
}

static int model_app_read_file_exact(const char *path, uint8_t *dst, uint32_t expected_size)
{
    int fd;
    uint32_t read_total = 0U;

    if ((path == RT_NULL) || (dst == RT_NULL) || (expected_size == 0U))
    {
        return -RT_ERROR;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return -RT_ERROR;
    }

    while (read_total < expected_size)
    {
        int read_size = read(fd,
                             dst + read_total,
                             (size_t)(expected_size - read_total));

        if (read_size <= 0)
        {
            close(fd);
            return -RT_ERROR;
        }

        read_total += (uint32_t)read_size;
    }

    close(fd);
    return RT_EOK;
}

static rt_bool_t model_app_is_runtime_supported(const model_app_package_info_t *info)
{
    if (info == RT_NULL)
    {
        return RT_FALSE;
    }

    if ((rt_strcmp(info->type, "visual_gmm") != 0) &&
        (rt_strcmp(info->type, "visual_ad") != 0) &&
        (rt_strcmp(info->type, "edge_impulse_visual_gmm") != 0))
    {
        return RT_FALSE;
    }

    if ((info->tflite_count != 2U) ||
        (info->input_w != AI_APP_INPUT_W) ||
        (info->input_h != AI_APP_INPUT_H) ||
        (info->grid_w != AI_APP_GRID_W) ||
        (info->grid_h != AI_APP_GRID_H))
    {
        return RT_FALSE;
    }

    if ((rt_strcmp(info->profile, "cap_v8_96") == 0) ||
        (rt_strcmp(info->profile, "cap_v15_96") == 0) ||
        (rt_strcmp(info->profile, "v15") == 0) ||
        (rt_strcmp(info->profile, "v8") == 0) ||
        (rt_strcmp(info->profile, "96") == 0))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

static int model_app_validate_package(const model_app_package_info_t *info,
                                      uint32_t main_size,
                                      uint32_t anomaly_size)
{
    if (info == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (info->runtime_supported != RT_TRUE)
    {
        g_model_last_error = "runtime profile unsupported";
        return -RT_ERROR;
    }

    if ((main_size == 0U) || (main_size > MODEL_APP_MAX_GRAPH0_BYTES) ||
        (anomaly_size == 0U) || (anomaly_size > MODEL_APP_MAX_GRAPH1_BYTES))
    {
        g_model_last_error = "tflite file too large";
        return -RT_ERROR;
    }

    if ((info->arena0_size == 0U) || (info->arena0_size > AI_APP_ARENA_SIZE) ||
        (info->arena1_size == 0U) || (info->arena1_size > AI_APP_ARENA_SIZE))
    {
        g_model_last_error = "arena size unsupported";
        return -RT_ERROR;
    }

    return RT_EOK;
}

static int model_app_load_tflite_files(const model_app_package_info_t *info,
                                       uint32_t main_size,
                                       uint32_t anomaly_size)
{
    if (model_app_validate_package(info, main_size, anomaly_size) != RT_EOK)
    {
#ifdef APP_USING_EDGE_IMPULSE_V15
        ei_v15_use_builtin_model();
#endif
        return -RT_ERROR;
    }

    if (model_app_read_file_exact(s_main_tflite_path, s_model_graph0_buf, main_size) != RT_EOK)
    {
#ifdef APP_USING_EDGE_IMPULSE_V15
        ei_v15_use_builtin_model();
#endif
        g_model_last_error = "read main tflite failed";
        return -RT_ERROR;
    }

    if (model_app_read_file_exact(s_anomaly_tflite_path, s_model_graph1_buf, anomaly_size) != RT_EOK)
    {
#ifdef APP_USING_EDGE_IMPULSE_V15
        ei_v15_use_builtin_model();
#endif
        g_model_last_error = "read anomaly tflite failed";
        return -RT_ERROR;
    }

#ifdef APP_USING_EDGE_IMPULSE_V15
    if (ei_v15_use_external_models(s_model_graph0_buf,
                                   main_size,
                                   info->arena0_size,
                                   s_model_graph1_buf,
                                   anomaly_size,
                                   info->arena1_size,
                                   info->threshold_x10) != RT_EOK)
    {
        ei_v15_use_builtin_model();
        g_model_last_error = "apply external model failed";
        return -RT_ERROR;
    }
#else
    RT_UNUSED(info);
#endif

    return RT_EOK;
}

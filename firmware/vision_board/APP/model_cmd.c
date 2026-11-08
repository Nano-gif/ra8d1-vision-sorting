#include "model_internal.h"

static const char *model_app_source_text(model_app_source_t source)
{
    return (source == MODEL_APP_SOURCE_SDCARD) ? "sdcard" : "builtin";
}

static void model_app_print_info(const model_app_package_info_t *info,
                                 const char *path,
                                 rt_bool_t selected)
{
    if (info == RT_NULL)
    {
        return;
    }

    rt_kprintf("[model] %c name=%s profile=%s type=%s\r\n",
               selected ? '*' : ' ',
               info->name,
               info->profile,
               info->type);
    rt_kprintf("[model]   input=%ux%u grid=%ux%u threshold=%u.%u files=%s runtime=%s\r\n",
               (unsigned int)info->input_w,
               (unsigned int)info->input_h,
               (unsigned int)info->grid_w,
               (unsigned int)info->grid_h,
               (unsigned int)(info->threshold_x10 / 10U),
               (unsigned int)(info->threshold_x10 % 10U),
               info->files_present ? "present" : "missing",
               info->runtime_supported ? "supported" : "unsupported");
    rt_kprintf("[model]   arena0=%u arena1=%u main=%s anomaly=%s\r\n",
               (unsigned int)info->arena0_size,
               (unsigned int)info->arena1_size,
               info->main_file,
               info->anomaly_file);
    rt_kprintf("[model]   path=%s\r\n",
               (path != RT_NULL) ? path : "");
}

static void model_app_print_state(void)
{
    model_app_init_builtin_state();

    rt_kprintf("[model] name=%s source=%s metadata=%s files=%s sdram=%s runtime=%s\r\n",
               (g_model_state.name != RT_NULL) ? g_model_state.name : "",
               model_app_source_text(g_model_state.source),
               g_model_state.metadata_loaded ? "loaded" : "no",
               g_model_state.files_present ? "present" : "missing",
               g_model_state.sdram_loaded ? "loaded" : "no",
               g_model_state.runtime_supported ? "supported" : "unsupported");
    rt_kprintf("[model] profile=%s type=%s input=%ux%u grid=%ux%u threshold=%u.%u tflite=%u\r\n",
               (g_model_state.profile != RT_NULL) ? g_model_state.profile : "",
               (g_model_state.type != RT_NULL) ? g_model_state.type : "",
               (unsigned int)g_model_state.input_w,
               (unsigned int)g_model_state.input_h,
               (unsigned int)g_model_state.grid_w,
               (unsigned int)g_model_state.grid_h,
               (unsigned int)(g_model_state.threshold_x10 / 10U),
               (unsigned int)(g_model_state.threshold_x10 % 10U),
               (unsigned int)g_model_state.tflite_count);
    rt_kprintf("[model] package=%s\r\n",
               (g_model_state.package_dir != RT_NULL) ? g_model_state.package_dir : "");
    rt_kprintf("[model] main=%s size=%u arena0=%u\r\n",
               (g_model_state.main_tflite_path != RT_NULL) ? g_model_state.main_tflite_path : "",
               (unsigned int)g_model_state.main_tflite_size,
               (unsigned int)g_model_state.arena0_size);
    rt_kprintf("[model] anomaly=%s size=%u arena1=%u\r\n",
               (g_model_state.anomaly_tflite_path != RT_NULL) ? g_model_state.anomaly_tflite_path : "",
               (unsigned int)g_model_state.anomaly_tflite_size,
               (unsigned int)g_model_state.arena1_size);
    rt_kprintf("[model] last=%s\r\n", g_model_last_error);
}

static int model_app_load_cmd(int argc, char **argv)
{
    char path[MODEL_APP_PATH_MAX];

    model_app_build_package_path((argc >= 2) ? argv[1] : RT_NULL, path, sizeof(path));
    if (path[0] == '\0')
    {
        rt_kprintf("[model] invalid model path\r\n");
        return -1;
    }

    if (model_app_load_package(path) != RT_EOK)
    {
        rt_kprintf("[model] load failed: %s\r\n", model_app_last_error());
        return -1;
    }

    model_app_print_state();
    if (g_model_state.sdram_loaded != RT_TRUE)
    {
        rt_kprintf("[model] fallback: built-in EI model remains active (%s)\r\n", g_model_last_error);
    }
    return 0;
}

static int model_load(int argc, char **argv)
{
    return model_app_load_cmd(argc, argv);
}

static int model_stat(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    model_app_print_state();
    return 0;
}

static int model_scan(int argc, char **argv)
{
    const char *root = STORAGE_MODELS_DIR;
    DIR *dir;
    struct dirent *entry;
    uint32_t count = 0U;

    if (argc >= 2)
    {
        root = argv[1];
    }

    dir = opendir(root);
    if (dir == RT_NULL)
    {
        rt_kprintf("[model] open failed: %s\r\n", root);
        return -1;
    }

    rt_kprintf("[model] scan root=%s\r\n", root);
    while ((entry = readdir(dir)) != RT_NULL)
    {
        char path[MODEL_APP_PATH_MAX];
        DIR *subdir;
        model_app_package_info_t info;
        int ret;

        if ((rt_strcmp(entry->d_name, ".") == 0) ||
            (rt_strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }

        ret = rt_snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        if ((ret <= 0) || ((uint32_t)ret >= sizeof(path)))
        {
            continue;
        }

        subdir = opendir(path);
        if (subdir == RT_NULL)
        {
            continue;
        }
        closedir(subdir);

        if (model_app_read_package_info(path, &info) != RT_EOK)
        {
            continue;
        }
        (void)model_app_refresh_file_state(path, &info);
        model_app_print_info(&info,
                             path,
                             ((g_model_state.package_dir != RT_NULL) &&
                              (rt_strcmp(path, g_model_state.package_dir) == 0)) ? RT_TRUE : RT_FALSE);
        count++;
    }

    closedir(dir);
    rt_kprintf("[model] packages=%u\r\n", (unsigned int)count);
    return 0;
}

MSH_CMD_EXPORT(model_app_load_cmd, load selected model package metadata);
MSH_CMD_EXPORT(model_load, load model package by name or path);
MSH_CMD_EXPORT(model_stat, show current model loader state);
MSH_CMD_EXPORT(model_scan, scan model packages);

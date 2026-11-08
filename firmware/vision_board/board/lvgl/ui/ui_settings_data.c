#include "ui_settings_data.h"

#include "config_app.h"
#include "detect_app.h"
#include "light_pwm_app.h"
#include "model_app.h"
#include "storage_app.h"

#include <dirent.h>
#include <string.h>

#define UI_SETTINGS_MODEL_MAX       8U
#define UI_SETTINGS_MODEL_NAME_MAX  32U
#define UI_SETTINGS_MODEL_PATH_MAX  96U

static ui_settings_command_cb_t s_command_cb = 0;
static void *s_command_user_data = 0;
static char s_default_model_path[64] = "/vision/models/cap_v8_96";
static uint8_t s_selected_model_index = 0U;
static ui_settings_model_file_t s_model_files[UI_SETTINGS_MODEL_MAX];
static char s_model_names[UI_SETTINGS_MODEL_MAX][UI_SETTINGS_MODEL_NAME_MAX];
static char s_model_paths[UI_SETTINGS_MODEL_MAX][UI_SETTINGS_MODEL_PATH_MAX];
static uint8_t s_model_file_count = 0U;
static rt_bool_t s_model_list_valid = RT_FALSE;

static void ui_settings_data_sync_detect_model(void);
static void ui_settings_data_refresh_model_list(void);
static rt_bool_t ui_settings_data_is_active_model(const char *name, const char *path);
static rt_bool_t ui_settings_data_is_dir_entry(const char *parent, const struct dirent *entry);
static int ui_settings_data_add_model_from_dir(const char *folder_name);
static void ui_settings_data_add_default_model(void);

static ui_settings_snapshot_t s_snapshot = {
    .display = {
        .brightness_percent = 80,
        .light_enabled = false,
        .light_level = 5,
        .language = UI_LANGUAGE_ZH_CN,
        .touch_beep_enabled = true,
    },
    .camera = {
        .preview_format = "RGB565",
        .preview_w = 320,
        .preview_h = 240,
        .roi_x = 40,
        .roi_y = 0,
        .roi_size = 240,
    },
    .active_model = {
        .name = "cap_v8_96",
        .postprocess = UI_AI_POSTPROCESS_VISUAL_AD,
        .input_w = 96,
        .input_h = 96,
        .grid_w = 5,
        .grid_h = 5,
        .threshold_x10 = 59,
        .tflite_count = 2,
        .arena_size = 288332,
    },
    .system = {
        .sd_state_text = "N/A",
        .model_dir = "/vision/models/",
        .default_model_path = "/vision/models/cap_v8_96",
        .config_path = "/vision/config.ini",
        .calibration_state_text = "Reserved",
    },
};

static void ui_settings_data_sync_detect_model(void)
{
    ui_detect_snapshot_t detect_snapshot;
    model_app_state_t model_state;

    if (detect_app_is_ready() != RT_TRUE)
    {
        return;
    }

    if (detect_app_get_snapshot(&detect_snapshot) != RT_EOK)
    {
        return;
    }

    s_snapshot.active_model = detect_snapshot.model;

    if ((model_app_get_state(&model_state) == RT_EOK) &&
        (model_state.metadata_loaded == RT_TRUE) &&
        (model_state.package_dir != RT_NULL))
    {
        s_snapshot.active_model.name = model_state.name;
        rt_strncpy(s_default_model_path, model_state.package_dir, sizeof(s_default_model_path) - 1U);
        s_default_model_path[sizeof(s_default_model_path) - 1U] = '\0';
    }
    else
    {
        rt_snprintf(s_default_model_path,
                    sizeof(s_default_model_path),
                    "/vision/models/%s",
                    detect_snapshot.model.name);
    }

    s_snapshot.system.default_model_path = s_default_model_path;
}

const ui_settings_snapshot_t *ui_settings_data_get_snapshot(void)
{
    ui_settings_data_sync_detect_model();
    return &s_snapshot;
}

static ui_language_t ui_settings_data_language_from_config(config_app_language_t language)
{
    return language == CONFIG_APP_LANGUAGE_EN ? UI_LANGUAGE_EN_US : UI_LANGUAGE_ZH_CN;
}

static config_app_language_t ui_settings_data_language_to_config(ui_language_t language)
{
    return language == UI_LANGUAGE_EN_US ? CONFIG_APP_LANGUAGE_EN : CONFIG_APP_LANGUAGE_ZH;
}

static rt_bool_t ui_settings_data_is_active_model(const char *name, const char *path)
{
    if ((name != RT_NULL) && (s_snapshot.active_model.name != RT_NULL) &&
        (rt_strcmp(name, s_snapshot.active_model.name) == 0))
    {
        return RT_TRUE;
    }

    if ((path != RT_NULL) && (s_snapshot.system.default_model_path != RT_NULL) &&
        (rt_strcmp(path, s_snapshot.system.default_model_path) == 0))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

static rt_bool_t ui_settings_data_is_dir_entry(const char *parent, const struct dirent *entry)
{
    char path[UI_SETTINGS_MODEL_PATH_MAX];
    DIR *dir;

    if ((parent == RT_NULL) || (entry == RT_NULL))
    {
        return RT_FALSE;
    }

    if ((rt_strcmp(entry->d_name, ".") == 0) || (rt_strcmp(entry->d_name, "..") == 0))
    {
        return RT_FALSE;
    }

    if (entry->d_type == DT_DIR)
    {
        return RT_TRUE;
    }

    rt_snprintf(path, sizeof(path), "%s/%s", parent, entry->d_name);
    dir = opendir(path);
    if (dir == RT_NULL)
    {
        return RT_FALSE;
    }

    closedir(dir);
    return RT_TRUE;
}

static int ui_settings_data_add_model_from_dir(const char *folder_name)
{
    model_app_package_info_t info;
    uint8_t index;
    int len;

    if ((folder_name == RT_NULL) || (folder_name[0] == '\0') ||
        (s_model_file_count >= UI_SETTINGS_MODEL_MAX))
    {
        return -RT_ERROR;
    }

    index = s_model_file_count;
    len = rt_snprintf(s_model_paths[index],
                      sizeof(s_model_paths[index]),
                      "%s/%s",
                      STORAGE_MODELS_DIR,
                      folder_name);
    if ((len <= 0) || ((uint32_t)len >= sizeof(s_model_paths[index])))
    {
        return -RT_ERROR;
    }

    if (model_app_read_package_info(s_model_paths[index], &info) != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_strncpy(s_model_names[index], info.name, sizeof(s_model_names[index]) - 1U);
    s_model_names[index][sizeof(s_model_names[index]) - 1U] = '\0';

    s_model_files[index].name = s_model_names[index];
    s_model_files[index].path = s_model_paths[index];
    s_model_files[index].postprocess = UI_AI_POSTPROCESS_VISUAL_AD;
    s_model_files[index].input_w = info.input_w;
    s_model_files[index].input_h = info.input_h;
    s_model_files[index].grid_w = info.grid_w;
    s_model_files[index].grid_h = info.grid_h;
    s_model_files[index].threshold_x10 = info.threshold_x10;
    s_model_files[index].tflite_count = info.tflite_count;
    s_model_files[index].state =
        ui_settings_data_is_active_model(s_model_files[index].name, s_model_files[index].path) ?
        UI_SETTINGS_MODEL_ACTIVE : UI_SETTINGS_MODEL_READY;

    s_model_file_count++;
    return RT_EOK;
}

static void ui_settings_data_add_default_model(void)
{
    (void)ui_settings_data_add_model_from_dir("cap_v8_96");
}

static void ui_settings_data_refresh_model_list(void)
{
    DIR *dir;
    struct dirent *entry;
    uint8_t old_selected = s_selected_model_index;

    ui_settings_data_sync_detect_model();

    s_model_file_count = 0U;
    rt_memset(s_model_files, 0, sizeof(s_model_files));
    rt_memset(s_model_names, 0, sizeof(s_model_names));
    rt_memset(s_model_paths, 0, sizeof(s_model_paths));

    dir = opendir(STORAGE_MODELS_DIR);
    if (dir != RT_NULL)
    {
        while ((entry = readdir(dir)) != RT_NULL)
        {
            if (s_model_file_count >= UI_SETTINGS_MODEL_MAX)
            {
                break;
            }

            if (ui_settings_data_is_dir_entry(STORAGE_MODELS_DIR, entry) == RT_TRUE)
            {
                (void)ui_settings_data_add_model_from_dir(entry->d_name);
            }
        }
        closedir(dir);
    }

    if (s_model_file_count == 0U)
    {
        ui_settings_data_add_default_model();
    }

    if (s_model_file_count == 0U)
    {
        s_selected_model_index = 0U;
    }
    else if (old_selected < s_model_file_count)
    {
        s_selected_model_index = old_selected;
    }
    else
    {
        s_selected_model_index = 0U;
    }

    s_model_list_valid = RT_TRUE;
}

const ui_settings_model_file_t *ui_settings_data_get_model_files(uint8_t *count)
{
    if (s_model_list_valid != RT_TRUE)
    {
        ui_settings_data_refresh_model_list();
    }

    if (count != RT_NULL)
    {
        *count = s_model_file_count;
    }

    return s_model_files;
}

int ui_settings_data_select_model(uint8_t index)
{
    if (s_model_list_valid != RT_TRUE)
    {
        ui_settings_data_refresh_model_list();
    }

    if (index >= s_model_file_count)
    {
        return -RT_ERROR;
    }

    s_selected_model_index = index;
    return RT_EOK;
}

uint8_t ui_settings_data_get_selected_model_index(void)
{
    return s_selected_model_index;
}

const ui_settings_model_file_t *ui_settings_data_get_selected_model(void)
{
    if (s_model_list_valid != RT_TRUE)
    {
        ui_settings_data_refresh_model_list();
    }

    if ((s_model_file_count == 0U) || (s_selected_model_index >= s_model_file_count))
    {
        return RT_NULL;
    }

    return &s_model_files[s_selected_model_index];
}

void ui_settings_data_set_command_handler(ui_settings_command_cb_t cb, void *user_data)
{
    s_command_cb = cb;
    s_command_user_data = user_data;
}

void ui_settings_data_emit_command(ui_settings_command_t cmd, uint16_t value)
{
    if (cmd == UI_SETTINGS_CMD_SET_BRIGHTNESS)
    {
        if (value > 100U)
        {
            value = 100U;
        }
        s_snapshot.display.brightness_percent = (uint8_t)value;
        config_app_set_brightness((uint8_t)value);
    }
    else if (cmd == UI_SETTINGS_CMD_SET_LIGHT_ENABLED)
    {
        s_snapshot.display.light_enabled = (value != 0U);
        config_app_set_light_enabled(s_snapshot.display.light_enabled ? 1U : 0U);
        if (s_snapshot.display.light_enabled)
        {
            if (s_snapshot.display.light_level == 0U)
            {
                s_snapshot.display.light_level = 5U;
                config_app_set_light_level(s_snapshot.display.light_level);
            }
            (void)light_pwm_app_set_level(s_snapshot.display.light_level);
        }
        else
        {
            (void)light_pwm_app_set_enable(RT_FALSE);
        }
    }
    else if (cmd == UI_SETTINGS_CMD_SET_LIGHT_LEVEL)
    {
        if (value > LIGHT_PWM_APP_LEVEL_MAX)
        {
            value = LIGHT_PWM_APP_LEVEL_MAX;
        }
        s_snapshot.display.light_level = (uint8_t)value;
        config_app_set_light_level((uint8_t)value);
        if ((s_snapshot.display.light_enabled == true) && (value > 0U))
        {
            (void)light_pwm_app_set_level((uint8_t)value);
        }
        else if (value == 0U)
        {
            s_snapshot.display.light_enabled = false;
            config_app_set_light_enabled(0U);
            (void)light_pwm_app_set_enable(RT_FALSE);
        }
    }
    else if (cmd == UI_SETTINGS_CMD_SET_LANGUAGE)
    {
        if (value <= (uint16_t)UI_LANGUAGE_EN_US)
        {
            s_snapshot.display.language = (ui_language_t)value;
            config_app_set_language(ui_settings_data_language_to_config((ui_language_t)value));
            ui_language_set((ui_language_t)value);
        }
    }
    else if (cmd == UI_SETTINGS_CMD_REFRESH_MODEL_LIST)
    {
        s_model_list_valid = RT_FALSE;
        ui_settings_data_refresh_model_list();
    }
    else if (cmd == UI_SETTINGS_CMD_LOAD_MODEL)
    {
        (void)ui_settings_data_select_model((uint8_t)value);
    }
    else if (cmd == UI_SETTINGS_CMD_SAVE_CONFIG)
    {
        (void)ui_settings_data_save_config();
    }

    if (s_command_cb != RT_NULL)
    {
        s_command_cb(cmd, value, s_command_user_data);
    }

    if (cmd == UI_SETTINGS_CMD_LOAD_MODEL)
    {
        s_model_list_valid = RT_FALSE;
        ui_settings_data_refresh_model_list();
    }
}

int ui_settings_data_load_config(void)
{
    const config_app_t *config;
    ui_language_t language;

    if (!config_app_is_loaded())
    {
        if (config_app_load() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    config = config_app_get();
    language = ui_settings_data_language_from_config(config->language);

    s_snapshot.display.brightness_percent = config->brightness_percent;
    s_snapshot.display.light_enabled = (config->light_enabled != 0U);
    s_snapshot.display.light_level = config->light_level;
    if (s_snapshot.display.light_level > LIGHT_PWM_APP_LEVEL_MAX)
    {
        s_snapshot.display.light_level = LIGHT_PWM_APP_LEVEL_MAX;
    }
    s_snapshot.display.language = language;
    s_snapshot.system.config_path = "/vision/config.ini";
    s_snapshot.system.model_dir = "/vision/models/";
    ui_settings_data_sync_detect_model();
    config_app_set_model(s_snapshot.active_model.name);
    ui_language_set(language);
    s_model_list_valid = RT_FALSE;

    if (s_command_cb != RT_NULL)
    {
        s_command_cb(UI_SETTINGS_CMD_SET_BRIGHTNESS,
                     s_snapshot.display.brightness_percent,
                     s_command_user_data);
    }

    if (s_snapshot.display.light_enabled == true)
    {
        if (s_snapshot.display.light_level == 0U)
        {
            s_snapshot.display.light_level = 5U;
            config_app_set_light_level(s_snapshot.display.light_level);
        }
        (void)light_pwm_app_set_level(s_snapshot.display.light_level);
    }
    else
    {
        (void)light_pwm_app_set_enable(RT_FALSE);
    }

    return RT_EOK;
}

int ui_settings_data_save_config(void)
{
    config_app_set_brightness(s_snapshot.display.brightness_percent);
    config_app_set_light_enabled(s_snapshot.display.light_enabled ? 1U : 0U);
    config_app_set_light_level(s_snapshot.display.light_level);
    config_app_set_language(ui_settings_data_language_to_config(s_snapshot.display.language));
    ui_settings_data_sync_detect_model();
    config_app_set_model(s_snapshot.active_model.name);
    return config_app_save();
}

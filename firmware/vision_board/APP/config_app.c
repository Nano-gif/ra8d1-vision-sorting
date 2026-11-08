#include "config_app.h"

#include "storage_app.h"

#include <ctype.h>
#include <dfs_file.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef FINSH_USING_MSH
#include <finsh.h>
#endif

#define CONFIG_APP_FILE_BUFFER_SIZE 384U

static config_app_t s_config = {
    .model = "cap_v8_96",
    .language = CONFIG_APP_LANGUAGE_ZH,
    .brightness_percent = 80U,
    .light_enabled = 0U,
    .light_level = 5U,
    .save_detect_image = 0U,
};
static rt_bool_t s_config_loaded = RT_FALSE;
static const char *s_config_last_error = "not loaded";

static void config_app_reset_defaults(void)
{
    rt_strncpy(s_config.model, "cap_v8_96", sizeof(s_config.model) - 1U);
    s_config.model[sizeof(s_config.model) - 1U] = '\0';
    s_config.language = CONFIG_APP_LANGUAGE_ZH;
    s_config.brightness_percent = 80U;
    s_config.light_enabled = 0U;
    s_config.light_level = 5U;
    s_config.save_detect_image = 0U;
}

static char *config_app_trim(char *text)
{
    char *end;

    if (text == RT_NULL)
    {
        return text;
    }

    while ((*text != '\0') && isspace((unsigned char)*text))
    {
        text++;
    }

    end = text + rt_strlen(text);
    while ((end > text) && isspace((unsigned char)*(end - 1)))
    {
        end--;
    }
    *end = '\0';

    return text;
}

static uint8_t config_app_parse_u8(const char *value, uint8_t min_value, uint8_t max_value, uint8_t fallback)
{
    long parsed;
    char *end_ptr;

    if ((value == RT_NULL) || (*value == '\0'))
    {
        return fallback;
    }

    parsed = strtol(value, &end_ptr, 10);
    if (end_ptr == value)
    {
        return fallback;
    }

    if (parsed < (long)min_value)
    {
        parsed = (long)min_value;
    }
    else if (parsed > (long)max_value)
    {
        parsed = (long)max_value;
    }

    return (uint8_t)parsed;
}

static void config_app_parse_pair(char *key, char *value)
{
    key = config_app_trim(key);
    value = config_app_trim(value);

    if ((key == RT_NULL) || (value == RT_NULL) || (*key == '\0'))
    {
        return;
    }

    if (rt_strcmp(key, "model") == 0)
    {
        config_app_set_model(value);
    }
    else if (rt_strcmp(key, "language") == 0)
    {
        if ((rt_strcmp(value, "en") == 0) || (rt_strcmp(value, "EN") == 0))
        {
            config_app_set_language(CONFIG_APP_LANGUAGE_EN);
        }
        else
        {
            config_app_set_language(CONFIG_APP_LANGUAGE_ZH);
        }
    }
    else if (rt_strcmp(key, "brightness") == 0)
    {
        config_app_set_brightness(config_app_parse_u8(value, 0U, 100U, s_config.brightness_percent));
    }
    else if (rt_strcmp(key, "light_enabled") == 0)
    {
        config_app_set_light_enabled(config_app_parse_u8(value, 0U, 1U, s_config.light_enabled));
    }
    else if (rt_strcmp(key, "light_level") == 0)
    {
        config_app_set_light_level(config_app_parse_u8(value, 0U, 10U, s_config.light_level));
    }
    else if (rt_strcmp(key, "save_detect_image") == 0)
    {
        config_app_set_save_detect_image(config_app_parse_u8(value, 0U, 2U, s_config.save_detect_image));
    }
}

static void config_app_parse_text(char *text)
{
    char *line;
    char *next;

    line = text;
    while ((line != RT_NULL) && (*line != '\0'))
    {
        char *eq;

        next = strchr(line, '\n');
        if (next != RT_NULL)
        {
            *next = '\0';
            next++;
        }

        line = config_app_trim(line);
        if ((*line != '\0') && (*line != '#') && (*line != ';'))
        {
            eq = strchr(line, '=');
            if (eq != RT_NULL)
            {
                *eq = '\0';
                config_app_parse_pair(line, eq + 1);
            }
        }

        line = next;
    }
}

const config_app_t *config_app_get(void)
{
    return &s_config;
}

rt_bool_t config_app_is_loaded(void)
{
    return s_config_loaded;
}

void config_app_mark_unloaded(void)
{
    s_config_loaded = RT_FALSE;
    s_config_last_error = "not loaded";
}

int config_app_load(void)
{
    char buffer[CONFIG_APP_FILE_BUFFER_SIZE];
    int fd;
    int bytes_read;

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            s_config_last_error = storage_app_last_error();
            return -RT_ERROR;
        }
    }

    fd = open(STORAGE_CONFIG_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        s_config_last_error = "open config failed";
        return -RT_ERROR;
    }

    bytes_read = read(fd, buffer, sizeof(buffer) - 1U);
    close(fd);

    if (bytes_read < 0)
    {
        s_config_last_error = "read config failed";
        return -RT_ERROR;
    }

    buffer[bytes_read] = '\0';
    config_app_reset_defaults();
    config_app_parse_text(buffer);
    s_config_loaded = RT_TRUE;
    s_config_last_error = "ok";
    return RT_EOK;
}

int config_app_save(void)
{
    char text[CONFIG_APP_FILE_BUFFER_SIZE];
    int fd;
    int len;
    int written;

    if (!storage_app_is_ready())
    {
        if (storage_app_init() != RT_EOK)
        {
            s_config_last_error = storage_app_last_error();
            return -RT_ERROR;
        }
    }

    len = snprintf(text,
                   sizeof(text),
                   "# Vision Board storage config\r\n"
                   "model=%s\r\n"
                   "language=%s\r\n"
                   "brightness=%u\r\n"
                   "light_enabled=%u\r\n"
                   "light_level=%u\r\n"
                   "save_detect_image=%u\r\n",
                   s_config.model,
                   config_app_language_string(s_config.language),
                   (unsigned)s_config.brightness_percent,
                   (unsigned)s_config.light_enabled,
                   (unsigned)s_config.light_level,
                   (unsigned)s_config.save_detect_image);
    if ((len <= 0) || (len >= (int)sizeof(text)))
    {
        s_config_last_error = "format config failed";
        return -RT_ERROR;
    }

    fd = open(STORAGE_CONFIG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        s_config_last_error = "open config write failed";
        return -RT_ERROR;
    }

    written = write(fd, text, len);
    if (written != len)
    {
        close(fd);
        s_config_last_error = "write config failed";
        return -RT_ERROR;
    }

    if (fsync(fd) != 0)
    {
        close(fd);
        s_config_last_error = "sync config failed";
        return -RT_ERROR;
    }

    close(fd);

    s_config_loaded = RT_TRUE;
    s_config_last_error = "ok";
    return RT_EOK;
}

void config_app_set_model(const char *model)
{
    if ((model == RT_NULL) || (model[0] == '\0'))
    {
        return;
    }

    rt_strncpy(s_config.model, model, sizeof(s_config.model) - 1U);
    s_config.model[sizeof(s_config.model) - 1U] = '\0';
}

void config_app_set_language(config_app_language_t language)
{
    if (language > CONFIG_APP_LANGUAGE_EN)
    {
        language = CONFIG_APP_LANGUAGE_ZH;
    }

    s_config.language = language;
}

void config_app_set_brightness(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    s_config.brightness_percent = percent;
}

void config_app_set_light_enabled(uint8_t enabled)
{
    s_config.light_enabled = (enabled != 0U) ? 1U : 0U;
}

void config_app_set_light_level(uint8_t level)
{
    if (level > 10U)
    {
        level = 10U;
    }

    s_config.light_level = level;
}

void config_app_set_save_detect_image(uint8_t mode)
{
    if (mode > 2U)
    {
        mode = 2U;
    }

    s_config.save_detect_image = mode;
}

const char *config_app_language_string(config_app_language_t language)
{
    return language == CONFIG_APP_LANGUAGE_EN ? "en" : "zh";
}

#ifdef FINSH_USING_MSH
static int config_show_cmd(int argc, char **argv)
{
    const config_app_t *config = config_app_get();

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("[config] loaded=%d error=%s\r\n",
               config_app_is_loaded(),
               s_config_last_error);
    rt_kprintf("[config] model=%s\r\n", config->model);
    rt_kprintf("[config] language=%s\r\n", config_app_language_string(config->language));
    rt_kprintf("[config] brightness=%u\r\n", config->brightness_percent);
    rt_kprintf("[config] light_enabled=%u\r\n", config->light_enabled);
    rt_kprintf("[config] light_level=%u\r\n", config->light_level);
    rt_kprintf("[config] save_detect_image=%u\r\n", config->save_detect_image);
    return 0;
}
MSH_CMD_EXPORT(config_show_cmd, show current config cache);

static int config_load_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (config_app_load() == RT_EOK)
    {
        rt_kprintf("[config] load ok\r\n");
        return 0;
    }

    rt_kprintf("[config] load failed: %s\r\n", s_config_last_error);
    return -1;
}
MSH_CMD_EXPORT(config_load_cmd, load /vision/config.ini);

static int config_save_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (config_app_save() == RT_EOK)
    {
        rt_kprintf("[config] save ok: %s\r\n", STORAGE_CONFIG_FILE);
        return 0;
    }

    rt_kprintf("[config] save failed: %s\r\n", s_config_last_error);
    return -1;
}
MSH_CMD_EXPORT(config_save_cmd, save current config cache);
#endif

#include "ui_language.h"

/*
 * 项目专用中文字体。
 *
 * 注意：
 * ui_font_zh_16.c 是按当前中文文案生成的“子集字库”，不是完整中文字库。
 * 后续如果新增中文后出现方块，说明字库里缺字，需要重新生成 ui_font_zh_16.c。
 */
LV_FONT_DECLARE(ui_font_zh_16)

static ui_language_t s_language = UI_LANGUAGE_ZH_CN;
static ui_language_changed_cb_t s_changed_cb = 0;
static void *s_changed_user_data = 0;

/* 返回当前全局语言。 */
ui_language_t ui_language_get(void)
{
    return s_language;
}

/* 设置当前全局语言，并在语言真的变化时通知外层刷新界面。 */
void ui_language_set(ui_language_t language)
{
    if (language > UI_LANGUAGE_EN_US) {
        return;
    }

    if (s_language == language) {
        return;
    }

    s_language = language;

    if (s_changed_cb) {
        s_changed_cb(s_language, s_changed_user_data);
    }
}

/* 判断当前是否为中文界面。 */
bool ui_language_is_chinese(void)
{
    return s_language == UI_LANGUAGE_ZH_CN;
}

/* 根据当前语言返回中文或英文文案。 */
const char *ui_text(const char *zh_cn, const char *en_us)
{
    return ui_language_is_chinese() ? zh_cn : en_us;
}

/* 返回普通文本字体。 */
const lv_font_t *ui_font_body(void)
{
    return ui_language_is_chinese() ? &ui_font_zh_16 : &lv_font_montserrat_12;
}

/* 返回标题文本字体。 */
const lv_font_t *ui_font_title(void)
{
    return ui_language_is_chinese() ? &ui_font_zh_16 : &lv_font_montserrat_20;
}

/* 注册语言变化回调；当前设计为 ui.c 注册，用来刷新顶部标题和底部导航。 */
void ui_language_set_changed_callback(ui_language_changed_cb_t cb, void *user_data)
{
    s_changed_cb = cb;
    s_changed_user_data = user_data;
}

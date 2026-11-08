#ifndef UI_DATA_PAGE_H
#define UI_DATA_PAGE_H

#include "lvgl/lvgl.h"

/* 创建数据中心页面；页面只读取 ui_data 接口，不直接访问 SD 卡或检测线程。 */
lv_obj_t *ui_data_page_create(lv_obj_t *parent);

/* 全局语言切换后刷新数据页文字；由 ui.c 的语言回调统一调用。 */
void ui_data_page_refresh_language(void);

void ui_data_page_refresh_records(void);
void ui_data_page_refresh(void);

/*
 * Update the small action-status text near Export/Clear buttons.
 *
 * Parameters:
 * - text: Status text to display. NULL clears it to Ready.
 *
 * Return:
 * - None.
 */
void ui_data_page_set_status_text(const char *text);

#endif /* UI_DATA_PAGE_H */

#ifndef UI_CONFIG_H
#define UI_CONFIG_H

/*
 * UI 全局配置。
 *
 * 这个文件只放“配置型常量”：屏幕尺寸、区域尺寸、颜色、圆角等。
 * 好处是后续换屏幕、调颜色、调布局时，不需要在每个页面文件里搜索魔法数字。
 */

/* 当前 PC 模拟器按 4.3 寸横屏 800x480 设计。 */
#define UI_SCREEN_W       800
#define UI_SCREEN_H       480
#define UI_TOP_BAR_H      44
#define UI_BOTTOM_BAR_H   52
#define UI_BOTTOM_BAR_Y   (UI_SCREEN_H - UI_BOTTOM_BAR_H)

/* 中间页面内容区：位于顶部状态栏和底部导航栏之间。 */
#define UI_CONTENT_X      20
#define UI_CONTENT_Y      62
#define UI_CONTENT_W      760
#define UI_CONTENT_H      350

/*
 * 摄像头预览区。
 * OV5640 当前配置输出 320x240，UI 预览对象也固定为 320x240。
 * 这样 PC 模拟界面和后续真实摄像头帧是一比一关系，ROI 坐标不用再额外换算显示尺寸。
 */
#define UI_CAMERA_SRC_W   320
#define UI_CAMERA_SRC_H   240
#define UI_CAMERA_VIEW_W  320
#define UI_CAMERA_VIEW_H  240
#define UI_CAMERA_RGB565_FRAME_BYTES (UI_CAMERA_SRC_W * UI_CAMERA_SRC_H * 2)

/*
 * PC 模拟阶段显示一帧假的 RGB565 摄像头画面，方便验证 LVGL 图像显示链路。
 * 后续接入真实 OV5640/DMA 后，把这里改成 0，避免在 MCU 上额外占用一块 150KB 缓冲。
 */
#define UI_DETECT_ENABLE_FAKE_CAMERA 1

/*
 * AI 热力图显示上限。
 * 当前 Edge Impulse Visual AD 导出是 5x5，但 UI 不能把这个值写死。
 * 这里预留到 16x16，后续如果重新训练出 12x12 模型，公开 UI 接口不用改。
 */
#define UI_AI_HEATMAP_MAX_GRID_W 16
#define UI_AI_HEATMAP_MAX_GRID_H 16
#define UI_AI_HEATMAP_MAX_CELLS  (UI_AI_HEATMAP_MAX_GRID_W * UI_AI_HEATMAP_MAX_GRID_H)

/* 当前检测 ROI：源图 240x240，UI 里同样显示为 240x240，只允许在 320x240 中左右移动。 */
#define UI_ROI_SRC_SIZE   240
#define UI_ROI_VIEW_SIZE  ((UI_ROI_SRC_SIZE * UI_CAMERA_VIEW_W) / UI_CAMERA_SRC_W)

/* 深色工业风主题颜色。 */
#define UI_COLOR_BG       0x111317
#define UI_COLOR_TOP      0x15191D
#define UI_COLOR_PANEL    0x191D22
#define UI_COLOR_CARD     0x20262B
#define UI_COLOR_BORDER   0x303942
#define UI_COLOR_TEXT     0xF2F5F4
#define UI_COLOR_TEXT_SUB 0x8C9AA5
#define UI_COLOR_ACCENT   0x49B7FF
#define UI_COLOR_OK       0x28D17C
#define UI_COLOR_WARN     0xF3B34C
#define UI_COLOR_NG       0xFF4D5C
#define UI_COLOR_STOP     0xB8323C

/* 通用圆角。嵌入式工业界面不要太圆，5-6 像素比较稳。 */
#define UI_RADIUS_SMALL   5
#define UI_RADIUS_MEDIUM  6

#endif /* UI_CONFIG_H */

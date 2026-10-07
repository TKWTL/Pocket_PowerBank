/*
 * menu_pages.h - 菜单树定义（页/条目）
 *
 * 第一级菜单：返回、设置、状态、工具、游戏、关于（可能继续增加）
 * 当前文案为英文占位，中文接入后再替换为 UTF-8 字符串。
 */
#ifndef MENU_PAGES_H
#define MENU_PAGES_H

#include "menu.h"

#ifdef __cplusplus
extern "C" {
#endif

extern menu_page_t menu_page_root;
extern menu_page_t menu_page_status;
extern menu_page_t menu_page_status_battery;
extern menu_page_t menu_page_status_accel;
extern menu_page_t menu_page_status_timer;
extern menu_page_t menu_page_time;

/* 状态页实时信息缓冲区（由 menu_status_refresh 定时更新） */
extern char menu_status_bat[24];

/* 刷新状态页全部子页缓冲（ui_task 定时调用；对未初始化芯片输出占位，避免 I2C 空访问） */
void menu_status_refresh(void);

/* 进入时间页时从 load_task 镜像读当前时间，填充时间设置变量（menu_ui 页面切换时调用） */
void menu_time_read(void);

/* 应用当前背光亮度（1~16 → TMR1_CH4 PWM）；开机时调用一次 */
void menu_backlight_apply(void);

/* 自动翻转开关（Settings→Display→Auto Flip）：与背光一样是运行时设置，不持久化。
 * ON=按重力方向自动 180° 翻转（默认）；OFF=固定方向（画面永远正立）。
 * ui_task 的 ui_auto_rotate() 每帧按 (开关 AND 重力方向) 应用旋转。 */
void menu_auto_flip_set(uint8_t on);
uint8_t menu_auto_flip_get(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_PAGES_H */

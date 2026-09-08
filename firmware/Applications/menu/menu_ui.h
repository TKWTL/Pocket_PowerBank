/*
 * menu_ui.h - 菜单的 LVGL 渲染
 *
 * 160x40 长条屏布局（一屏一个条目）：
 *   - 顶行：页标题（左）+ 页码 x/y 或 EDIT（右）
 *   - 中间：当前条目（含 < > 翻页指示）
 * 主题由 menu_theme 提供，每次重绘套用。
 */
#ifndef MENU_UI_H
#define MENU_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建菜单屏（初始隐藏），返回屏对象供 ui_task 切换显示 */
lv_obj_t *menu_ui_create(void);
/* 按当前菜单状态重绘内容并套用主题 */
void menu_ui_redraw(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_UI_H */

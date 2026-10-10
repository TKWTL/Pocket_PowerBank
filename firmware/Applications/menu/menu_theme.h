/*
 * menu_theme.h - 菜单浅色/深色主题与主题色管理
 *
 * 主题 = 基础色板（浅/深）+ 运行时主题色（可在设置页修改）。
 * 主题色修改后调用 menu_notify_changed() 触发重绘即可生效。
 */
#ifndef MENU_THEME_H
#define MENU_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 菜单配色结构 */
typedef struct {
    lv_color_t bg;         /* 页面背景 */
    lv_color_t surface;    /* 条目/顶部栏背景（预留） */
    lv_color_t primary;    /* 主题色（可运行时修改） */
    lv_color_t on_primary; /* 主题色上的文字 */
    lv_color_t text;       /* 主文字 */
    lv_color_t text_sec;   /* 次要文字/页码/提示 */
    lv_color_t border;     /* 分隔线（预留） */
    lv_color_t sel_bg;     /* 选中态背景（= primary） */
    lv_color_t sel_text;   /* 选中态文字（= on_primary） */
} menu_theme_t;

typedef enum {
    MENU_THEME_DARK = 0,
    MENU_THEME_LIGHT,
    MENU_THEME_MAX
} menu_theme_id_t;

/* 可选主题色板（0xRRGGBB），供设置页选择 */
#define MENU_PALETTE_COUNT 7
extern const uint32_t menu_palette[MENU_PALETTE_COUNT];

/* 取当前组合后的主题（内部静态缓冲，勿长期持有指针） */
const menu_theme_t *menu_theme_get(void);
menu_theme_id_t menu_theme_get_id(void);
void menu_theme_set(menu_theme_id_t id);

/* 修改主题色（0xRRGGBB） */
void menu_theme_set_primary(uint32_t rgb);
uint32_t menu_theme_get_primary(void);
/* 当前主题色在色板中的下标，不在色板则返回 0 */
uint8_t menu_theme_primary_index(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_THEME_H */

/*
 * menu_theme.c - 菜单浅色/深色主题与主题色管理
 *
 * 实现说明：
 *  - lv_color_hex() 不是编译期常量，基础色板先以 0xRRGGBB 存储，
 *    取用（menu_theme_get）时再转换为 lv_color_t。
 *  - 主题色独立于浅/深基础色板存储，两套主题共用同一主题色。
 */
#include "menu_theme.h"

/* 可选主题色板（0xRRGGBB），供设置页选择。
 * 索引 0 = 琥珀色，同时是开机默认主题色（见 s_primary_rgb）。
 * 默认色放在索引 0 是刻意的：设置页 s_theme_color_idx 初值为 0，
 * menu_theme_primary_index() 未命中时也返回 0，因此"默认色"与
 * "设置页选中项"天然一致，不需要额外的同步代码。
 * 新增琥珀是【延长色板】（MENU_PALETTE_COUNT 6→7），原有 6 色整体顺延，不淘汰任何颜色。 */
const uint32_t menu_palette[MENU_PALETTE_COUNT] = {
    0xFFB300,   /* 琥珀（默认，新增） */
    0x2A6DF4,   /* 蓝 */
    0x16A34A,   /* 绿 */
    0xF97316,   /* 橙 */
    0xEF4444,   /* 红 */
    0x8B5CF6,   /* 紫 */
    0x06B6D4,   /* 青 */
};

/* 基础色板（不含主题色），0xRRGGBB */
typedef struct {
    uint32_t bg;
    uint32_t surface;
    uint32_t text;
    uint32_t text_sec;
    uint32_t border;
} menu_base_t;

static const menu_base_t s_base_dark = {
    .bg       = 0x0E1116,
    .surface  = 0x1A1F27,
    .text     = 0xEDF1F7,
    .text_sec = 0x8A93A0,
    .border   = 0x2A313B,
};

static const menu_base_t s_base_light = {
    .bg       = 0xFFFFFF,
    .surface  = 0xF2F4F7,
    .text     = 0x1A1D21,
    .text_sec = 0x6B7280,
    .border   = 0xE3E7EC,
};

static menu_theme_id_t s_theme_id = MENU_THEME_DARK;
static uint32_t s_primary_rgb = 0xFFB300;   /* 默认主题色 = 琥珀，须与 menu_palette[0] 一致 */

const menu_theme_t *menu_theme_get(void)
{
    static menu_theme_t cur;
    const menu_base_t *b = (s_theme_id == MENU_THEME_LIGHT) ? &s_base_light : &s_base_dark;

    cur.bg         = lv_color_hex(b->bg);
    cur.surface    = lv_color_hex(b->surface);
    cur.text       = lv_color_hex(b->text);
    cur.text_sec   = lv_color_hex(b->text_sec);
    cur.border     = lv_color_hex(b->border);
    cur.primary    = lv_color_hex(s_primary_rgb);   /* 主题色可运行时修改 */
    cur.on_primary = lv_color_white();
    cur.sel_bg     = cur.primary;
    cur.sel_text   = cur.on_primary;
    return &cur;
}

menu_theme_id_t menu_theme_get_id(void)
{
    return s_theme_id;
}

void menu_theme_set(menu_theme_id_t id)
{
    if (id < MENU_THEME_MAX) {
        s_theme_id = id;
    }
}

void menu_theme_set_primary(uint32_t rgb)
{
    s_primary_rgb = rgb;
}

uint32_t menu_theme_get_primary(void)
{
    return s_primary_rgb;
}

uint8_t menu_theme_primary_index(void)
{
    uint8_t i;
    for (i = 0; i < MENU_PALETTE_COUNT; i++) {
        if (menu_palette[i] == s_primary_rgb) {
            return i;
        }
    }
    return 0;
}

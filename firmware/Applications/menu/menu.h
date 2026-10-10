/*
 * menu.h - 轻量多级菜单状态机
 *
 * 设计：
 *  - 页面(menu_page_t) = 条目(menu_item_t)数组
 *  - 一屏显示一个条目（160x40 长条屏），NEXT/PREV 翻页，ENTER 确认
 *  - 支持返回栈实现多级菜单
 *  - 菜单只负责状态与逻辑，渲染由 menu_ui 完成（通过重绘回调通知）
 *
 * 参考来源：
 *  - 思想参考 MiaoUI（单色 OLED 菜单 UI 框架，C 语言、双向链表、非线性动画）
 *    https://github.com/JFeng-Z/MiaoUI
 *    本项目借用其下列思想，但改用 LVGL 9 渲染、条目仍存数组（非链表）：
 *      1) 双向链表循环滚动        → menu.c 索引按方向取模（环形），见 menu_next/prev
 *      2) UI_Animation 的 PID 动画 → menu_ui.c 的 PID 滑动切换（位置类动画 ki 必须为 0）
 *      3) lastJumpItem 记忆       → menu_page_t.last_index（返回上级恢复原选中项）
 *      4) UI_PAGE_ICON 图标页     → MENU_PAGE_ICON + menu_icon_t（选中 30x30 / 未选中 20x20）
 *      5) UI_ACTION 动作枚举      → app_action_t（按键在 ui_task 统一映射为语义动作）
 *      6) 菜单项即应用            → menu_app_t 生命周期（进入 create / 退出 destroy）
 *  - 图标来源：根页图标由 MiaoUI 的 XBM 图标集（30x30）转换而来，
 *    转换脚本 menu/tools/xbm2lvgl.py，生成物 menu_icons.c。
 */
#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"   /* 仅使用 LV_KEY_PREV/NEXT/ENTER 常量 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MENU_ITEM_BACK,    /* 返回：子页→父页，根页→关闭菜单 */
    MENU_ITEM_PAGE,    /* 进入子页 */
    MENU_ITEM_ACTION,  /* 立即执行动作 */
    MENU_ITEM_TOGGLE,  /* 开关：翻转 *toggle_ptr，翻转后调用 action */
    MENU_ITEM_VALUE,   /* 数值：ENTER 进入编辑，NEXT/PREV 步进，ENTER 确认，步进后调用 action */
    MENU_ITEM_ENUM,    /* 枚举：多选一文本选项（enum_opts 数组），编辑交互同 VALUE 但循环步进 */
    MENU_ITEM_INFO,    /* 纯显示 */
    MENU_ITEM_APP,     /* 应用：进入接管全屏，退出回进入点菜单页 */
} menu_item_type_t;

struct menu_page_t;
typedef struct menu_page_t menu_page_t;
typedef struct menu_item_t menu_item_t;

typedef void (*menu_action_fn)(menu_item_t *it);

/* ---------- 页面类型 ---------- */
typedef enum {
    MENU_PAGE_TEXT = 0,   /* 对角线文本菜单 */
    MENU_PAGE_ICON,       /* 图标菜单 */
    MENU_PAGE_WORD,       /* 独立3行全屏纯文本阅读/确认页 */
} menu_page_type_t;

typedef enum {
    MENU_WORD_INFO = 0,   /* PREV/NEXT有边界滚动，CONF随时返回 */
    MENU_WORD_CONFIRM,    /* 必须翻到末页；末页NEXT退出、CONF执行/退出 */
    MENU_WORD_ACTION      /* INFO按键语义 + 页面驻留期间循环执行hook */
} menu_word_mode_t;

typedef enum {
    MENU_WORD_HOOK_ENTER = 0,
    MENU_WORD_HOOK_TICK,
    MENU_WORD_HOOK_EXIT
} menu_word_hook_event_t;

typedef void (*menu_word_hook_fn)(menu_word_hook_event_t event);

/* 图标页条目：图标 + 标签（与 items 数组一一对应） */
typedef struct {
    const void *icon;       /* 选中态 30x30（lv_image_dsc_t *） */
    const void *icon_small; /* 未选中态 20x20（lv_image_dsc_t *） */
    const char *label;      /* 备用文本 */
} menu_icon_t;

/* 图标页布局常量（menu.c 计算初始滚动位置、menu_ui.c 渲染共用）
 * MENU_ICON_SLOT 现在表示图标中心距（pitch），不再表示固定屏幕槽位。
 * 38px 可在 160px 屏幕上形成 5 个可见图标，最外侧两个仅露出一部分。 */
#define MENU_SCR_W       160
#define MENU_SCR_H       40
#define MENU_ICON_SLOT   38   /* 图标中心距；位置由连续 head_x 决定，不固定到屏幕槽位 */
#define MENU_ICON_SEL    30   /* 选中图标尺寸 */
#define MENU_ICON_NORM   20   /* 未选中图标尺寸 */

struct menu_item_t {
    const char *label;           /* 显示文本（中文接入后可为 UTF-8 字符串） */
    menu_item_type_t type;
    const menu_page_t *target;   /* PAGE / BACK 使用 */
    menu_action_fn action;       /* ACTION:点击；TOGGLE:翻转后；VALUE:步进后 */
    uint8_t *toggle_ptr;         /* TOGGLE */
    const char *toggle_on;       /* TOGGLE 开显示（可 NULL，默认 ON） */
    const char *toggle_off;      /* TOGGLE 关显示（可 NULL，默认 OFF） */
    int32_t *value_ptr;          /* VALUE/ENUM */
    int32_t value_min, value_max, value_step;
    const char *unit;            /* VALUE 单位（可 NULL） */
    const char * const *enum_opts; /* ENUM：选项文本数组（元素经 menu_tr 本地化，可 NULL） */
    uint8_t enum_count;          /* ENUM：选项数量 */
};

struct menu_page_t {
    const char *title;           /* 页标题 */
    const menu_item_t *items;    /* 条目数组 */
    uint8_t item_count;
    uint8_t last_index;          /* 记忆：上次在该页选中的条目（MiaoUI lastJumpItem 特性） */
    uint8_t type;                /* menu_page_type_t */
    int16_t head_x;              /* ICON：滚动偏移 */
    const menu_icon_t *icons;    /* ICON：图标数组 */

    const char * const *word_lines; /* WORD：逻辑行数组 */
    uint8_t word_line_count;        /* WORD：逻辑行数 */
    uint8_t word_top;               /* WORD_INFO：3行窗口首行 */
    uint8_t word_mode;              /* menu_word_mode_t */
    menu_action_fn word_action;     /* WORD_CONFIRM：末页CONF动作，可NULL */
    menu_word_hook_fn word_hook;    /* WORD_ACTION：ENTER/TICK/EXIT hook */
};

/* ==================== 菜单配置宏（集中声明菜单树用） ====================
 * 用法：在单个配置文件（如 menu_pages.c）里：
 *   1) 顶部前向声明所有页面（套娃无顺序限制）；
 *   2) 用 MENU_ITEM_* 一行声明一个条目；
 *   3) 用 MENU_PAGE_* 声明页面（自动按数组长度计数）。
 * 子菜单套娃：条目用 MENU_ITEM_PAGE_(label, &子页)。
 */
#define MENU_ITEM_BACK_(l)          { (l), MENU_ITEM_BACK,   NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, NULL }
#define MENU_ITEM_PAGE_(l, p)       { (l), MENU_ITEM_PAGE,   (const menu_page_t *)(p), NULL, NULL, NULL, NULL, NULL, 0, 0, 0, NULL }
#define MENU_ITEM_ACTION_(l, fn)    { (l), MENU_ITEM_ACTION, NULL, (fn), NULL, NULL, NULL, NULL, 0, 0, 0, NULL }
#define MENU_ITEM_TOGGLE_(l, ptr, on, off, fn) \
    { (l), MENU_ITEM_TOGGLE, NULL, (fn), (ptr), (on), (off), NULL, 0, 0, 0, NULL }
#define MENU_ITEM_VALUE_(l, ptr, mn, mx, st, u, fn) \
    { (l), MENU_ITEM_VALUE, NULL, (fn), NULL, NULL, NULL, (ptr), (mn), (mx), (st), (u) }
/* ENUM：ptr=选项下标(int32_t*)，opts=选项文本数组，cnt=选项数量；步进在 [0, cnt-1] 间循环 */
#define MENU_ITEM_ENUM_(l, ptr, opts, cnt, fn) \
    { (l), MENU_ITEM_ENUM, NULL, (fn), NULL, NULL, NULL, (ptr), 0, (int32_t)((cnt) - 1), 1, NULL, (opts), (uint8_t)(cnt) }
#define MENU_ITEM_INFO_(l)          { (l), MENU_ITEM_INFO,   NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, NULL }
/* 应用：target 存 menu_app_t*（运行时强转），进入接管全屏，退出回进入点 */
#define MENU_ITEM_APP_(l, app)      { (l), MENU_ITEM_APP,    (const menu_page_t *)(app), NULL, NULL, NULL, NULL, NULL, 0, 0, 0, NULL }

/* 文本页定义（items 必须是可 sizeof 的数组） */
#define MENU_PAGE_(title, var, items) \
    menu_page_t var = { (title), (items), \
        (uint8_t)(sizeof(items) / sizeof((items)[0])), 0, MENU_PAGE_TEXT, 0, NULL, \
        NULL, 0, 0, MENU_WORD_INFO, NULL, NULL }
/* 图标页定义 */
#define MENU_PAGE_ICON_(title, var, items, icons) \
    menu_page_t var = { (title), (items), \
        (uint8_t)(sizeof(items) / sizeof((items)[0])), 0, MENU_PAGE_ICON, 0, (icons), \
        NULL, 0, 0, MENU_WORD_INFO, NULL, NULL }
/* WORD页：独立全屏3行文本窗口，不复用TEXT布局/动画。 */
#define MENU_WORD_INFO_(var, lines) \
    menu_page_t var = { NULL, NULL, 0, 0, MENU_PAGE_WORD, 0, NULL, \
        (lines), (uint8_t)(sizeof(lines) / sizeof((lines)[0])), 0, MENU_WORD_INFO, NULL, NULL }
#define MENU_WORD_CONFIRM_(var, lines, fn) \
    menu_page_t var = { NULL, NULL, 0, 0, MENU_PAGE_WORD, 0, NULL, \
        (lines), (uint8_t)(sizeof(lines) / sizeof((lines)[0])), 0, MENU_WORD_CONFIRM, (fn), NULL }
#define MENU_WORD_ACTION_(var, lines, hook) \
    menu_page_t var = { NULL, NULL, 0, 0, MENU_PAGE_WORD, 0, NULL, \
        (lines), (uint8_t)(sizeof(lines) / sizeof((lines)[0])), 0, MENU_WORD_ACTION, NULL, (hook) }

#define MENU_STACK_DEPTH 8

typedef struct {
    menu_page_t *page;                    /* 当前页（可写，用于记忆 last_index） */
    uint8_t index;                        /* 当前条目下标 */
    bool editing;                         /* VALUE 编辑态 */
    menu_page_t *stack[MENU_STACK_DEPTH]; /* 返回栈 */
    uint8_t depth;
    bool active;                          /* 菜单是否打开 */
    int8_t nav_dir;                       /* 最近导航方向：+1 NEXT/前进，-1 PREV/后退（循环滚动时索引比较会判错方向，用它） */
} menu_state_t;

/* 重绘回调（menu_ui 注册），菜单任何状态变化都会触发 */
typedef void (*menu_redraw_cb_t)(void);

void menu_init(void);
bool menu_is_active(void);
void menu_open(void);
void menu_open_last(void);   /* 恢复到菜单关闭前的页面（功能界面退出后回到原菜单页） */
void menu_close(void);
void menu_enter_page(menu_page_t *pg); /* 动作回调动态进入页面（Factory gate等） */
void menu_process(void);                /* 每个UI循环执行WORD_ACTION hook */
/* ---------- 统一按键动作（仿 MiaoUI UI_ACTION） ----------
 * 物理按键在 UI 调度层一次性映射为语义化动作；菜单系统与
 * 功能界面各自解释动作，互不耦合、便于按界面定制。
 * MENU/NEXT 仅单击；CONF 四态：单击/双击/长按（持续）/长按结束
 * （长按结束由 ui_loop 检测 HOLD→NONE 边沿产生，见 ui_task）。 */
typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_UP,          /* MENU：单击；菜单：上一项；主界面：打开菜单 */
    APP_ACTION_DOWN,        /* NEXT：单击；菜单：下一项 */
    APP_ACTION_ENTER,       /* CONF：单击；菜单：确认 */
    APP_ACTION_ENTER_DBL,   /* CONF：双击；主界面：开关 WLED */
    APP_ACTION_ENTER_HOLD,  /* CONF：长按（持续）；主界面：调光 */
    APP_ACTION_ENTER_HOLD_END, /* CONF：长按结束（HOLD→NONE 边沿）；主界面：切换调光方向 */
} app_action_t;

/* ==================== 应用（全屏功能界面）生命周期 ====================
 * 菜单固化的统一应用模型：主界面、工具、游戏等都是"应用"。
 * 原则：哪里进入就退出到哪里；进入 create、退出 destroy（释放内存），
 * 主界面不特殊（也是应用）。menu.c 的 menu_app_enter/exit 统一管理。 */
typedef struct menu_app_t menu_app_t;
typedef void (*menu_app_create_fn)(void);
typedef void (*menu_app_activate_fn)(void);
typedef void (*menu_app_run_fn)(app_action_t action);
typedef void (*menu_app_destroy_fn)(void);

struct menu_app_t {
    const char *label;
    menu_app_create_fn   create;    /* 创建 screen（幂等重建） */
    menu_app_activate_fn activate;  /* lv_screen_load + invalidate（redraw 时调用） */
    menu_app_run_fn      run;       /* 应用态按键处理 */
    menu_app_destroy_fn  destroy;   /* 销毁 screen 释放内存 */
};

/* 内置应用（定义在各自 functions 目录的 .c 文件中） */
extern const menu_app_t menu_app_main;        /* 主界面（待机信息屏） */
extern const menu_app_t menu_app_screen_test; /* 屏幕测试 */

/* 应用生命周期（固化在菜单系统）：
 * enter：先 create→注册 run→menu_close(记录进入点)→redraw 触发 activate
 * exit ：destroy 当前应用→恢复菜单(进入点) */
void menu_app_enter(const menu_app_t *app);
void menu_app_exit(void);
const menu_app_t *menu_app_active(void);

/* 输入按键：LV_KEY_PREV / LV_KEY_NEXT / LV_KEY_ENTER */
void menu_handle_key(uint32_t lv_key);
/* 按键动作处理（菜单激活时由 UI 调度调用）：
 * UP=PREV、DOWN=NEXT、ENTER=确认；菜单未激活时不响应。 */
void menu_handle(app_action_t action);

const menu_state_t *menu_get_state(void);
const menu_page_t  *menu_current_page(void);
const menu_item_t  *menu_current_item(void);
const menu_page_t  *menu_root_page(void);
bool menu_has_prev(void);
bool menu_has_next(void);

void menu_set_redraw_cb(menu_redraw_cb_t cb);
/* 供设置页等外部主动请求重绘（如主题色/主题切换后） */
void menu_notify_changed(void);
/* UI 层滚动图标页时更新 head_x（随页面保存，进入下一级记忆、退出菜单重置） */
void menu_page_set_head_x(int16_t x);

/* ==================== 国际化（i18n） ====================
 * 菜单文案采用“英文文本即键”策略：menu_pages.c 里的 label/title/toggle 值
 * 保持英文（同时就是字符串键），menu_tr(key) 按当前语言返回显示文本：
 *  - 英文态直接查表返回 en（en 即 key 的英文）；
 *  - 中文态查表返回 zh，未配置的键回退 key 本身。
 * 中文字库（14/12px 部分字符集）待全部文案确认后生成，生成前切中文缺字形。 */
typedef struct {
    const char *key;   /* 字符串键，同时就是英文显示文本 */
    const char *zh;    /* 中文显示文本 */
} menu_tr_t;

typedef enum {
    MENU_LANG_EN = 0,  /* English */
    MENU_LANG_ZH,      /* 中文 */
} menu_lang_t;

void menu_lang_set(menu_lang_t lang);
menu_lang_t menu_lang_get(void);
/* 供设置页 Language 条目调用：在 EN <-> ZH 之间切换（切换后请 menu_notify_changed() 重绘）。
 * 语言状态、i18n 表与查表实现都在 menu_ui.c，menu_pages.c 只声明菜单树。 */
void menu_lang_toggle(void);
const char *menu_tr(const char *key);   /* 返回当前语言的显示文本 */
/* 按当前语言返回字体（主界面 14px / 次要 12px；中文字库生成后中文态返回中文字体） */
const lv_font_t *menu_font_main(void);
const lv_font_t *menu_font_small(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_H */

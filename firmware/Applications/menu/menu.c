/*
 * menu.c - 轻量多级菜单状态机实现
 *
 * 按键语义（与 lv_port_indev 的 keypad 映射一致）：
 *  - LV_KEY_NEXT  ：下一项 / 编辑时增加值
 *  - LV_KEY_PREV  ：上一项 / 编辑时减少值
 *  - LV_KEY_ENTER ：进入子页 / 触发动作 / 翻转开关 / 进入&退出数值编辑
 *
 * 参考来源：菜单思想参考 MiaoUI（https://github.com/JFeng-Z/MiaoUI），
 * 具体借用点（循环滚动 / lastJumpItem 记忆 / 应用模型）见 menu.h 文件头"参考来源"。
 */
#include "menu.h"
#include "menu_pages.h"
#include "functions.h"   /* ui_app_register（应用进入/退出时接管应用态按键） */

static menu_state_t s_menu;
static menu_redraw_cb_t s_redraw_cb;
static const menu_app_t *s_app;   /* 当前激活应用（NULL=无/菜单态） */

void menu_set_redraw_cb(menu_redraw_cb_t cb)
{
    s_redraw_cb = cb;
}

static void request_redraw(void)
{
    if (s_redraw_cb) {
        s_redraw_cb();
    }
}

void menu_init(void)
{
    /* 根图标页冷启动默认选中 Settings（index 1）。
     * 后续仍由 last_index 记忆用户最后停留的图标。 */
    menu_page_root.last_index = (menu_page_root.item_count > 1U) ? 1U : 0U;

    s_menu.page    = &menu_page_root;
    s_menu.index   = menu_page_root.last_index;
    s_menu.editing = false;
    s_menu.depth   = 0;
    s_menu.active  = false;
    s_menu.nav_dir = 0;
}

bool menu_is_active(void)
{
    return s_menu.active;
}

/* 进入页面时的默认位置：
 * TEXT恢复last_index且首次通常跳过Return；ICON完整保留；WORD没有条目索引。 */
static uint8_t menu_entry_index(const menu_page_t *pg)
{
    uint8_t idx;

    if (!pg || pg->type == MENU_PAGE_WORD) return 0U;

    idx = pg->last_index;
    if (pg->type != MENU_PAGE_ICON) {
        if (idx == 0U) {
            idx = 1U;
            if (idx >= pg->item_count) idx = 0U;
        }
    } else if (idx >= pg->item_count) {
        idx = 0U;
    }
    return idx;
}

/* 图标页：head_x 定义为 index 0 图标的“中心坐标”。
 * 打开菜单时把当前选中图标放在屏幕正中（x=80）；之后 UI 只在选中图标
 * 越过可视限制时做最小量滚动，因此高亮图标不再总与屏幕中心对齐。 */
static int16_t icon_head_center(uint8_t idx)
{
    return (int16_t)(MENU_SCR_W / 2)
           - (int16_t)idx * MENU_ICON_SLOT;
}

void menu_open(void)
{
    if (s_menu.active && s_menu.page && s_menu.page->type == MENU_PAGE_WORD &&
        s_menu.page->word_mode == MENU_WORD_ACTION && s_menu.page->word_hook) {
        s_menu.page->word_hook(MENU_WORD_HOOK_EXIT);
    }
    s_menu.page    = &menu_page_root;
    s_menu.index   = menu_entry_index(&menu_page_root);  /* 进入菜单：保留上次选中的图标 */
    s_menu.editing = false;
    s_menu.depth   = 0;
    s_menu.active  = true;
    s_menu.nav_dir = 0;
    s_menu.page->last_index = s_menu.index;
    /* 图标页：退出菜单不保留位置状态（head_x 重置为选中项居中），
       但保留选中图标（last_index 上面已恢复） */
    if (s_menu.page->type == MENU_PAGE_ICON) {
        s_menu.page->head_x = icon_head_center(s_menu.index);
    }
    request_redraw();
}

/* 恢复到菜单关闭前的页面（功能界面退出后回到进入前所在的菜单页）。
 * 与 menu_open() 不同：menu_open 总是回根页并清空页面栈；本函数保留
 * menu_close 时的 page/stack/depth/index（menu_close 只清 active，不清状态）。
 * 例：从 Tools 页进入功能界面，menu_close 后状态仍停在 Tools 页，
 * 退出功能界面调用本函数即回到 Tools 页。 */
void menu_open_last(void)
{
    if (!s_menu.page) {
        menu_open();   /* 从未打开过菜单：兜底回根页 */
        return;
    }
    s_menu.editing = false;
    s_menu.nav_dir = 0;
    s_menu.active  = true;
    if (s_menu.page->type == MENU_PAGE_ICON) {
        s_menu.page->head_x = icon_head_center(s_menu.index);
    }
    request_redraw();
}

/* ==================== 应用生命周期（固化在菜单系统） ====================
 * 主界面、工具、游戏等都是应用（menu_app_t）。原则：哪里进入就退出到哪里；
 * 进入 create、退出 destroy（释放内存），主界面不特殊（也是应用）。 */

const menu_app_t *menu_app_active(void)
{
    return s_app;
}

/* 进入应用：先建屏 → 注册应用态按键 → 关闭菜单（记录进入点）。
 * menu_close 触发 redraw handler，非菜单态下它调用 app->activate 加载应用屏。 */
void menu_app_enter(const menu_app_t *app)
{
    if (!app) return;
    s_app = app;
    if (app->create) app->create();
    if (app->run) ui_app_register(app->run);
    menu_close();
}

/* 退出应用：销毁当前应用屏释放内存 → 恢复菜单到进入点。
 * 同时注册主界面应用（菜单关闭后按键回主界面处理）。 */
void menu_app_exit(void)
{
    if (s_app && s_app->destroy) s_app->destroy();
    s_app = NULL;
    ui_app_register(menu_app_main.run);
    menu_open_last();   /* 回到进入点菜单页 */
}

void menu_close(void)
{
    if (s_menu.active && s_menu.page && s_menu.page->type == MENU_PAGE_WORD &&
        s_menu.page->word_mode == MENU_WORD_ACTION && s_menu.page->word_hook) {
        s_menu.page->word_hook(MENU_WORD_HOOK_EXIT);
    }
    s_menu.active  = false;
    s_menu.editing = false;
    s_menu.nav_dir = -1;
    request_redraw();
}

const menu_state_t *menu_get_state(void)
{
    return &s_menu;
}

const menu_page_t *menu_current_page(void)
{
    return s_menu.page;
}

const menu_page_t *menu_root_page(void)
{
    return &menu_page_root;
}

/* 循环滚动（MiaoUI 环形链表特性）：只要条目数 > 1 即可双向循环 */
bool menu_has_prev(void)
{
    if (!s_menu.page) return false;
    if (s_menu.page->type == MENU_PAGE_WORD) {
        return s_menu.page->word_top > 0U;
    }
    return s_menu.page->item_count > 1U;
}

bool menu_has_next(void)
{
    if (!s_menu.page) return false;
    if (s_menu.page->type == MENU_PAGE_WORD) {
        if (s_menu.page->word_mode == MENU_WORD_CONFIRM) return true; /* 末页NEXT=Exit */
        return (uint16_t)s_menu.page->word_top + 3U < s_menu.page->word_line_count;
    }
    return s_menu.page->item_count > 1U;
}

const menu_item_t *menu_current_item(void)
{
    if (!s_menu.page || s_menu.page->type == MENU_PAGE_WORD) return NULL;
    if (s_menu.index >= s_menu.page->item_count) return NULL;
    return &s_menu.page->items[s_menu.index];
}

static void push_page(menu_page_t *pg)
{
    if (!pg) return;
    if (s_menu.depth < MENU_STACK_DEPTH) {
        s_menu.stack[s_menu.depth++] = s_menu.page;
    }
    /* 记忆当前页选中位置（MiaoUI：lastJumpItem）；
       图标页的滚动位置 head_x 就存在 page 结构里，进入下一级即随页面保存 */
    s_menu.page->last_index = s_menu.index;
    s_menu.page    = pg;
    s_menu.index   = menu_entry_index(pg);
    pg->last_index = s_menu.index;
    if (pg->type == MENU_PAGE_WORD) {
        pg->word_top = 0U;
        if (pg->word_mode == MENU_WORD_ACTION && pg->word_hook) {
            pg->word_hook(MENU_WORD_HOOK_ENTER);
        }
    }
    s_menu.editing = false;
    s_menu.nav_dir = 1;                /* 前进 */
    /* 子页若是图标页，重置滚动位置为选中项居中 */
    if (pg->type == MENU_PAGE_ICON) {
        pg->head_x = icon_head_center(s_menu.index);
    }
}

static void pop_page(void)
{
    if (s_menu.page && s_menu.page->type == MENU_PAGE_WORD &&
        s_menu.page->word_mode == MENU_WORD_ACTION && s_menu.page->word_hook) {
        s_menu.page->word_hook(MENU_WORD_HOOK_EXIT);
    }
    if (s_menu.depth > 0) {
        menu_page_t *parent = s_menu.stack[--s_menu.depth];
        s_menu.page  = parent;
        s_menu.index = parent->last_index;   /* 返回上一级：恢复其选中项（MiaoUI lastJumpItem 特性） */
        /* 图标页：head_x 在 page 结构里，push 时已随页面保存，此处自动恢复 */
    }
    s_menu.editing = false;
    s_menu.nav_dir = -1;               /* 后退 */
}

void menu_enter_page(menu_page_t *pg)
{
    if (!s_menu.active || !pg) return;
    push_page(pg);
    request_redraw();
}

void menu_process(void)
{
    menu_page_t *pg = s_menu.page;
    if (s_menu.active && pg && pg->type == MENU_PAGE_WORD &&
        pg->word_mode == MENU_WORD_ACTION && pg->word_hook) {
        pg->word_hook(MENU_WORD_HOOK_TICK);
    }
}

/* UI 层滚动图标页时更新 head_x（供记忆/恢复使用） */
void menu_page_set_head_x(int16_t x)
{
    if (s_menu.page) {
        s_menu.page->head_x = x;
    }
}

void menu_handle_key(uint32_t lv_key)
{
    const menu_item_t *it;

    if (!s_menu.active) {
        return;
    }

    /* --- WORD独立按键语义：完全绕过TEXT条目导航 --- */
    if (s_menu.page && s_menu.page->type == MENU_PAGE_WORD) {
        menu_page_t *pg = s_menu.page;
        uint8_t at_end = ((uint16_t)pg->word_top + 3U >= pg->word_line_count) ? 1U : 0U;

        if (pg->word_mode == MENU_WORD_CONFIRM) {
            /* 确认页必须读到最后一屏。到末屏前 CONF 无效，只允许 PREV/NEXT 翻页；
             * 到末屏后 NEXT=Exit，CONF=退出页面后执行动作（NULL则仅退出）。 */
            if (lv_key == LV_KEY_NEXT) {
                if (at_end) {
                    pop_page();
                } else {
                    pg->word_top++;
                }
            } else if (lv_key == LV_KEY_PREV) {
                if (pg->word_top > 0U) pg->word_top--;
            } else if (lv_key == LV_KEY_ENTER && at_end) {
                menu_action_fn action = pg->word_action;
                pop_page();
                if (action) action(NULL);
            }
        } else {
            /* INFO/ACTION：有边界滚动；CONF 随时返回。ACTION 的后台hook由 menu_process() 驱动。 */
            if (lv_key == LV_KEY_NEXT) {
                if (!at_end) pg->word_top++;
            } else if (lv_key == LV_KEY_PREV) {
                if (pg->word_top > 0U) pg->word_top--;
            } else if (lv_key == LV_KEY_ENTER) {
                pop_page();
            }
        }
        request_redraw();
        return;
    }

    it = menu_current_item();

    /* --- VALUE/ENUM 编辑态：NEXT/PREV 步进，ENTER 确认 --- */
    if (s_menu.editing && it && (it->type == MENU_ITEM_VALUE || it->type == MENU_ITEM_ENUM)) {
        if (lv_key == LV_KEY_NEXT) {
            int32_t v = *it->value_ptr + it->value_step;
            if (it->type == MENU_ITEM_ENUM) {
                if (v > it->value_max) v = it->value_min;   /* ENUM 循环：末项 → 首项 */
            } else if (v > it->value_max) {
                v = it->value_max;
            }
            *it->value_ptr = v;
            if (it->action) it->action((menu_item_t *)it);
        } else if (lv_key == LV_KEY_PREV) {
            int32_t v = *it->value_ptr - it->value_step;
            if (it->type == MENU_ITEM_ENUM) {
                if (v < it->value_min) v = it->value_max;   /* ENUM 循环：首项 → 末项 */
            } else if (v < it->value_min) {
                v = it->value_min;
            }
            *it->value_ptr = v;
            if (it->action) it->action((menu_item_t *)it);
        } else if (lv_key == LV_KEY_ENTER) {
            s_menu.editing = false;   /* 确认退出编辑 */
        }
        request_redraw();
        return;
    }

    /* --- 普通导航（循环滚动） --- */
    if (lv_key == LV_KEY_NEXT) {
        if (s_menu.page->item_count > 0) {
            s_menu.index++;
            if (s_menu.index >= s_menu.page->item_count) {
                s_menu.index = 0;        /* 循环：末项 → 首项 */
            }
            s_menu.page->last_index = s_menu.index;
            s_menu.nav_dir = 1;
        }
        request_redraw();
        return;
    }
    if (lv_key == LV_KEY_PREV) {
        if (s_menu.page->item_count > 0) {
            if (s_menu.index == 0) {
                s_menu.index = s_menu.page->item_count - 1;  /* 循环：首项 → 末项 */
            } else {
                s_menu.index--;
            }
            s_menu.page->last_index = s_menu.index;
            s_menu.nav_dir = -1;
        }
        request_redraw();
        return;
    }
    if (lv_key == LV_KEY_ENTER) {
        if (!it) {
            return;
        }
        switch (it->type) {
        case MENU_ITEM_BACK:
            if (s_menu.depth > 0) {
                pop_page();
            } else {
                menu_close();
            }
            break;
        case MENU_ITEM_PAGE:
            if (it->target) {
                push_page((menu_page_t *)it->target);
            }
            break;
        case MENU_ITEM_ACTION:
            if (it->action) {
                it->action((menu_item_t *)it);
            }
            break;
        case MENU_ITEM_APP:
            if (it->target) {
                menu_app_enter((const menu_app_t *)it->target);
            }
            break;
        case MENU_ITEM_TOGGLE:
            if (it->toggle_ptr) {
                *it->toggle_ptr = (*it->toggle_ptr) ? 0 : 1;
            }
            if (it->action) {
                it->action((menu_item_t *)it);
            }
            break;
        case MENU_ITEM_VALUE:
        case MENU_ITEM_ENUM:
            s_menu.editing = true;
            break;
        case MENU_ITEM_INFO:
        default:
            break;
        }
        request_redraw();
        return;
    }
}

void menu_notify_changed(void)
{
    request_redraw();
}

/* 按键动作处理：菜单激活时由 UI 调度（ui_loop）调用。
 * 动作已语义化（UP/DOWN/ENTER），与物理按键解耦：
 *  - UP → PREV、DOWN → NEXT、ENTER → 确认（按条目类型路由到单次函数等）。
 * 菜单未激活时不响应（打开菜单由主界面应用处理）。 */
void menu_handle(app_action_t action)
{
    if (!s_menu.active) {
        return;
    }
    switch (action) {
    case APP_ACTION_UP:    menu_handle_key(LV_KEY_PREV);  break;
    case APP_ACTION_DOWN:  menu_handle_key(LV_KEY_NEXT);  break;
    case APP_ACTION_ENTER: menu_handle_key(LV_KEY_ENTER); break;
    default: break;
    }
}

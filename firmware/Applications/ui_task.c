#include "applications.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "menu.h"
#include "menu_ui.h"
#include "menu_pages.h"
#include "functions.h"   /* 主界面创建/显示 + 一次性动作函数 */

static lv_obj_t *s_menu_scr;   /* 菜单屏（主界面/应用屏由菜单应用模型统一管理） */
static uint8_t s_orientation_lock;
static uint8_t s_rotate_applied_valid;
static uint8_t s_rotate_applied_flipped;

/* 500ms UI 数据刷新：
 *  - Time 设置页非编辑态：持续从 SD3078 镜像同步年月日时分秒，秒数正常走动；
 *  - Time 编辑态：冻结页面变量，避免 RTC 周期刷新覆盖用户正在修改的值；
 *  - Status 及子页：刷新状态字符串。 */
static void status_timer_cb(lv_timer_t *t)
{
    const menu_page_t *pg;
    (void)t;

    if (!menu_is_active()) {
        return;
    }

    pg = menu_current_page();

    if (pg == &menu_page_time) {
        if (!menu_get_state()->editing) {
            menu_time_read();
            menu_ui_redraw();
        }
        return;
    }

    if (pg != &menu_page_status &&
        pg != &menu_page_status_battery &&
        pg != &menu_page_status_accel &&
        pg != &menu_page_status_timer) {
        return;
    }

    menu_status_refresh();
    menu_ui_redraw();
}

/* 菜单状态变化回调：切换屏幕并重绘。
 * 菜单激活 → 菜单屏；非菜单态 → 当前应用（menu_app_active）activate 加载其屏。
 * 主界面也是应用（menu_app_main），不特殊。
 * 切屏后强制整屏重绘：GC9D01 部分刷新下从未被重绘的区域可能残留
 * 上电垃圾/白点，全屏失效可一次覆盖清除。 */
static void menu_redraw_handler(void)
{
    const menu_app_t *app = menu_app_active();
    if (menu_is_active()) {
        lv_screen_load(s_menu_scr);
    } else if (app && app->activate) {
        app->activate();   /* 非菜单态：当前应用加载自己的 screen */
    }
    lv_obj_invalidate(s_menu_scr);
    menu_ui_redraw();
}

/* 屏幕方向 = 重力方向 AND Auto Flip 开关。
 * Auto Flip（Settings→Display→Auto Flip）默认 ON：跟随重力自动翻转；
 * 关掉则固定方向（画面永远正立，不随摆放转动）。
 * 方向未知时保持上一次的旋转，避免上电瞬间闪一下。 */
void ui_orientation_lock_set(uint8_t lock)
{
    s_orientation_lock = (lock != 0U) ? 1U : 0U;
    s_rotate_applied_valid = 0U;

    if (s_orientation_lock != 0U) {
        /* 校准/水平仪等方向敏感工具统一固定为正常方向。 */
        GC9D01_rotation(1U);
        s_rotate_applied_flipped = 0U;
        s_rotate_applied_valid = 1U;
        lv_obj_invalidate(lv_screen_active());
    }
}

static void ui_auto_rotate(void)
{
    sc7a20_orientation_t orientation;
    uint8_t flipped;

    if (s_orientation_lock != 0U) return;

    orientation = SC7A20_AlgoGetOrientation();
    if (orientation == SC7A20_ORIENT_UNKNOWN) return;

    /* 只有开关为 ON 时才跟随重力；OFF 时恒为正立方向 */
    flipped = (uint8_t)((menu_auto_flip_get() != 0U) &&
                        (orientation == SC7A20_ORIENT_FLIPPED));

    if (s_rotate_applied_valid != 0U && flipped == s_rotate_applied_flipped) return;

    /* GC9D01 的 1/3 模式均保持 160x40（横竖分辨率不变，只翻转 180°） */
    GC9D01_rotation(flipped ? 3U : 1U);
    s_rotate_applied_flipped = flipped;
    s_rotate_applied_valid = 1U;
    lv_obj_invalidate(lv_screen_active());
}

/* ==================== UI 调度（仿 MiaoUI ui_loop） ====================
 * 按键一次扫描→语义化动作→状态机路由：菜单态→菜单系统；
 * 应用态→当前激活应用（主界面）自包含处理（绘制与按键同文件）。 */

/* 当前激活应用（非菜单界面）的按键自包含处理；NULL = 无应用 */
static void (*s_app_run)(app_action_t) = NULL;

void ui_app_register(void (*run)(app_action_t))
{
    s_app_run = run;
}

/* 按键扫描 → 语义化动作（一次转换，仿 MiaoUI indevScan）：
 * MENU/NEXT→单击 UP/DOWN；CONF→单击/双击/长按。
 * CONF 长按结束不在此上报，由 ui_loop 检测 HOLD→NONE 边沿产生 ENTER_HOLD_END。 */

/* 单键事件检测（CONF）：单击/双击/长按 */
static app_action_t key_event(KeyIndex_t k, app_action_t single, app_action_t dbl, app_action_t hold)
{
    if (KEY_GetClickTimes(k, 2)) { KEY_ClearEdge(k); return dbl; }
    if (Key_EdgeDetect(k) == KeyEdge_Holding) { KEY_ClearEdge(k); return hold; }
    if (KEY_GetState(k) == KeyState_LongPress) { return hold; }
    if (KEY_GetDASClick(k)) { KEY_ClearEdge(k); return single; }
    return APP_ACTION_NONE;
}

static app_action_t ui_scan_action(void)
{
    /* 菜单态：仅单击（导航/确认），双击/长按不进菜单。
     * Auto Flip 开着时画面会跟随重力倒过来，此时按键在视觉上左右对调：
     * 菜单里把 PREV/NEXT 互换，让"屏幕上左/上那个键"仍然是上一项。
     * 只在"当前画面确实倒着"时换 —— 关闭 Auto Flip 时固定正立，无需互换。
     * 应用态（主界面等）的 MENU/NEXT 语义是 HOME/LED，与屏幕方向无关，一律不换。 */
    if (menu_is_active()) {
        uint8_t flip = (s_orientation_lock == 0U) &&
                       (menu_auto_flip_get() != 0U) &&
                       (SC7A20_AlgoGetOrientation() == SC7A20_ORIENT_FLIPPED);
        if (KEY_GetDASClick(KeyIndex_MENU)) {
            KEY_ClearEdge(KeyIndex_MENU);
            return flip ? APP_ACTION_DOWN : APP_ACTION_UP;
        }
        if (KEY_GetDASClick(KeyIndex_CONF)) { KEY_ClearEdge(KeyIndex_CONF); return APP_ACTION_ENTER; }
        if (KEY_GetDASClick(KeyIndex_NEXT)) {
            KEY_ClearEdge(KeyIndex_NEXT);
            return flip ? APP_ACTION_UP : APP_ACTION_DOWN;
        }
        KEY_ClearEdge(KeyIndex_MENU);
        KEY_ClearEdge(KeyIndex_CONF);
        KEY_ClearEdge(KeyIndex_NEXT);
        return APP_ACTION_NONE;
    }

    /* 应用态：MENU/NEXT 仅单击；CONF 区分单击/双击/长按 */
    if (KEY_GetDASClick(KeyIndex_MENU)) { KEY_ClearEdge(KeyIndex_MENU); return APP_ACTION_UP; }
    if (KEY_GetDASClick(KeyIndex_NEXT)) { KEY_ClearEdge(KeyIndex_NEXT); return APP_ACTION_DOWN; }
    return key_event(KeyIndex_CONF, APP_ACTION_ENTER, APP_ACTION_ENTER_DBL, APP_ACTION_ENTER_HOLD);
}

/* 统一 UI 调度：状态机路由——菜单态→菜单系统；应用态→当前应用自包含处理 */
static app_action_t s_last_action = APP_ACTION_NONE;   /* CONF 长按结束边沿检测用 */

static void ui_loop(void)
{
    app_action_t action;

    /* WORD_ACTION 页面驻留期间每个UI循环执行后台hook，与按键事件无关。 */
    menu_process();
    action = ui_scan_action();

    /* CONF 长按结束边沿：上次 ENTER_HOLD、本次非 ENTER_HOLD（HOLD→NONE）→ ENTER_HOLD_END */
    if (!menu_is_active() && s_app_run) {
        if (s_last_action == APP_ACTION_ENTER_HOLD && action != APP_ACTION_ENTER_HOLD) {
            s_app_run(APP_ACTION_ENTER_HOLD_END);
        }
    }
    s_last_action = action;

    if (action == APP_ACTION_NONE) {
        return;
    }
    if (menu_is_active()) {
        menu_handle(action);
    } else if (s_app_run) {
        s_app_run(action);
    }
}

/* 按键扫描节拍（10ms）：空回调 LVGL 定时器。
 * 作用：让 lv_timer_handler() 始终返回 ~10ms（取未暂停定时器最小剩余时间），
 * 使 ui_loop 的按键扫描稳定在 10ms，不被主界面 500ms 定时器拖慢。
 * 否则扫描周期 ≈ 500ms：单击 Rising（10ms 窗口）丢失、双击 MultiClick
 * （500ms 窗口）被清零 → 双击失效。回调本身无事可做，仅提供节拍。 */
static void ui_scan_tick_cb(lv_timer_t *t)
{
    (void)t;
}

void ui_task_func(void *pvParameters)
{
    (void)pvParameters;
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* ---------- 菜单 ---------- */
    menu_init();
    s_menu_scr = menu_ui_create();
    menu_set_redraw_cb(menu_redraw_handler);

    /* 主界面作为应用进入（菜单应用模型统一管理：create + 注册 run + 激活） */
    menu_app_enter(&menu_app_main);

    /* 开机应用默认背光亮度（1~16，默认 8） */
    menu_backlight_apply();

    lv_timer_create(status_timer_cb, 500, NULL);
    /* 按键扫描节拍定时器：10ms，保证 ui_loop 每 10ms 扫描按键（见 ui_scan_tick_cb） */
    lv_timer_create(ui_scan_tick_cb, 10, NULL);

    while (1) {
        /* 低功耗阻塞：非 RUN 状态或唤醒数据未就绪时主动让出 CPU，等待恢复 */
        uint8_t woke = 0;
        while (pm_api_ui_should_block()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            woke = 1;
        }

        if (woke != 0U) {
            uint32_t wake_home_sec = menu_wake_home_after_sec();

            /* 长时间DeepSleep后表现成“锁屏”：仅临时显示主屏，不清原菜单的
             * page/stack/index；用户按MENU/PREV即可回到睡前页面。 */
            if (menu_is_active() && wake_home_sec != 0U &&
                pm_api_get_last_sleep_seconds() >= wake_home_sec) {
                menu_show_main_locked();
            }

            /* 唤醒恢复：数据已预取就绪，先绘出新图，再恢复背光。 */
            lv_timer_handler();
            menu_backlight_apply();
            continue;
        }

        /* 统一 UI 调度：扫描按键→动作→状态机路由（菜单/主界面应用自包含处理） */
        ui_loop();
        ui_auto_rotate();

        uint32_t t = lv_timer_handler();
        if (t == LV_NO_TIMER_READY) {
            t = LV_DEF_REFR_PERIOD;
        }
        /* 按键防双读已改由 ui_scan_action 消费 Rising 边沿（KEY_ClearEdge），
         * 此处无需人为延长循环间隔（会错过 Rising 导致吞键）。 */
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

/*
 * functions.h - 功能函数统一引用头（Functions 模块）
 *
 * 本头文件声明 functions 模块提供的所有功能函数：由菜单系统 MENU_ITEM_ACTION_
 * 调用的一次性函数、主界面（待机信息屏）的创建/显示等。
 * 各功能的实现按功能分开在各自 .c 中（无 functions_ 前缀）：
 *   - main_screen.c   主界面（待机信息屏）创建/显示
 *   - reset.c         系统复位与运输模式
 *
 * 新增功能函数步骤：
 *  1. 新建 functions_<功能>.c 实现（含本头文件以保持声明一致）；
 *  2. 在本头文件声明；
 *  3. 在 menu_pages.c 用 MENU_ITEM_ACTION_("名称", 函数名) 挂载；
 *  4. 在 Keil 工程 Functions group 加入新 .c。
 */
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"      /* lv_obj_t */
#include "menu.h"      /* menu_item_t（菜单动作回调签名） */

/* 主界面按键处理（主界面应用 menu_app_main.run，由菜单应用模型统一管理）：
 *  - APP_ACTION_UP    （MENU 键）→ 打开菜单（主界面销毁释放内存）
 *  - APP_ACTION_ENTER （CONF 键）→ 开关 WLED（固定一半最大亮度）
 *  - APP_ACTION_DOWN_DBL（PWR双击）→ 小电流/慢充（仅发RAM请求）
 * 约定：每个应用（全屏功能界面）的绘制与按键处理写在同一个 .c 文件中，
 * 注册为 menu_app_t 应用（menu.h），由 menu.c 统一管理生命周期。 */
void main_screen_run(app_action_t action);

/* 注册当前激活应用（非菜单界面的按键自包含处理）。
 * 由 menu.c 应用模型（menu_app_enter/exit）统一管理。 */
void ui_app_register(void (*run)(app_action_t));

/* 全屏工具可冻结180°自动翻转；lock=1强制正常方向，退出后恢复Auto Flip策略。 */
void ui_orientation_lock_set(uint8_t lock);

/* ---------- 一次性动作函数（MENU_ITEM_ACTION_ 调用） ---------- */
void action_reset_now(menu_item_t *it);   /* WORD_CONFIRM：请求恢复用户设置并复位 */
void action_transport_mode(menu_item_t *it); /* WORD_CONFIRM：请求运输模式 Standby */
void action_reset_process(void);            /* load_task：NVM成功落盘后真正复位 */

#ifdef __cplusplus
}
#endif

#endif /* FUNCTIONS_H */

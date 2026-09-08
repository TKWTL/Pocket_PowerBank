/*
 * reset.c - 系统复位功能
 *
 * 一次性动作函数（MENU_ITEM_ACTION_ 调用）。声明见 functions.h（统一引用头）。
 */
#include "menu.h"            /* menu_item_t */
#include "at32f423_misc.h"   /* nvic_system_reset */
#include "functions.h"       /* 本模块统一声明 */

void action_reset_now(menu_item_t *it)
{
    (void)it;
    nvic_system_reset();   /* 立即系统复位，不会返回 */
}

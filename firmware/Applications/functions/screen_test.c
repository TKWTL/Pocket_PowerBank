/*
 * screen_test.c - 屏幕测试工具（应用，Tools 菜单）
 *
 * 全屏纯色测试：进入显示红色 FF0000，按 CONF(ENTER) 依次切换
 * 绿 00FF00 > 蓝 0000FF > 退出。用于验证 GC9D01 面板是否存在
 * 无法写入的坏区。按菜单应用模型实现（create/activate/run/destroy），
 * 退出由 menu_app_exit 统一处理：destroy + 回到进入点菜单页。
 */
#include "applications.h"
#include "lvgl.h"
#include "menu.h"
#include "functions.h"

static lv_obj_t *s_test_scr;   /* 测试屏（lv_obj_create(NULL) 即屏幕） */
static lv_obj_t *s_test_bg;    /* 全屏色块背景 */
static uint8_t s_test_step;    /* 0=红 1=绿 2=蓝（3=退出） */

/* 应用 create：创建测试屏（幂等重建），初始红色 */
static void screen_test_create(void)
{
    if (s_test_scr) return;
    s_test_scr = lv_obj_create(NULL);
    s_test_bg = lv_obj_create(s_test_scr);
    lv_obj_set_size(s_test_bg, 160, 40);
    lv_obj_set_pos(s_test_bg, 0, 0);
    lv_obj_set_style_radius(s_test_bg, 0, 0);
    lv_obj_set_style_border_width(s_test_bg, 0, 0);
    lv_obj_set_style_pad_all(s_test_bg, 0, 0);
    lv_obj_remove_flag(s_test_bg, LV_OBJ_FLAG_SCROLLABLE);
    s_test_step = 0;
    lv_obj_set_style_bg_color(s_test_bg, lv_color_hex(0xFF0000), 0);
}

/* 应用 activate：加载测试屏并全屏重绘（redraw handler 非菜单态时调用） */
static void screen_test_activate(void)
{
    if (s_test_scr) {
        lv_screen_load(s_test_scr);
        lv_obj_invalidate(s_test_scr);
    }
}

/* 应用 run：CONF(ENTER) 依次 绿 > 蓝 > 退出 */
static void screen_test_run(app_action_t action)
{
    switch (action) {
    case APP_ACTION_ENTER:
        s_test_step++;
        if (s_test_step >= 3) {
            menu_app_exit();   /* 统一退出：destroy + 回进入点菜单页 */
        } else {
            lv_obj_set_style_bg_color(s_test_bg,
                (s_test_step == 1) ? lv_color_hex(0x00FF00)
                                   : lv_color_hex(0x0000FF), 0);
            lv_obj_invalidate(s_test_scr);
        }
        break;
    default:
        break;
    }
}

/* 应用 destroy：销毁测试屏释放 LVGL 堆内存 */
static void screen_test_destroy(void)
{
    if (s_test_scr) {
        lv_obj_delete(s_test_scr);
        s_test_scr = NULL;
        s_test_bg  = NULL;
    }
}

/* 屏幕测试应用注册（menu.h 声明；Tools 菜单 MENU_ITEM_APP_ 挂载） */
const menu_app_t menu_app_screen_test = {
    .label    = "Screen Test",
    .create   = screen_test_create,
    .activate = screen_test_activate,
    .run      = screen_test_run,
    .destroy  = screen_test_destroy,
};

/* SOS beacon: application draws UI, load_task runs PWM through wled_algo. */
#include "applications.h"
#include "lvgl.h"
#include "menu.h"

static lv_obj_t *s_sos_screen;

static void sos_create(void)
{
    lv_obj_t *label;
    if (s_sos_screen) return;

    s_sos_screen = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(s_sos_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_sos_screen, lv_color_hex(0x101010), 0);
    label = lv_label_create(s_sos_screen);
    lv_label_set_text(label, "SOS ACTIVE\nAny key to stop");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    lv_obj_center(label);

    WLED_AlgoRequestSOS(1U);
}

static void sos_activate(void)
{
    if (s_sos_screen) lv_screen_load(s_sos_screen);
}

static void sos_run(app_action_t action)
{
    if (action != APP_ACTION_NONE) menu_app_exit();
}

static void sos_destroy(void)
{
    WLED_AlgoRequestSOS(0U);
    if (s_sos_screen) {
        lv_obj_delete(s_sos_screen);
        s_sos_screen = NULL;
    }
}

const menu_app_t menu_app_sos = {
    .label = "SOS",
    .create = sos_create,
    .activate = sos_activate,
    .run = sos_run,
    .destroy = sos_destroy,
};

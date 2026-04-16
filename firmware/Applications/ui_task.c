#include "applications.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#define STATIC_BUF

static void pcap_timer_cb(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(t);
#ifdef STATIC_BUF
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d%% ", SW6306_ReadCapacity());
    lv_label_set_text_static(label, buf);
#else
    lv_label_set_text_fmt(label, "%d%% ", SW6306_ReadCapacity());
#endif
}

static void bat_timer_cb(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(t);
#ifdef STATIC_BUF
    static char buf[24];
    if (SW6306_ReadVBUS() > 9999) {
        snprintf(buf, sizeof(buf), "BAT:  %.2fV  %.3fA", SW6306_ReadVBAT() * 0.001f, SW6306_ReadIBAT() * 0.001f);
    } else {
        snprintf(buf, sizeof(buf), "BAT: %.2fV  %.3fA", SW6306_ReadVBAT() * 0.001f, SW6306_ReadIBAT() * 0.001f);
    }
    lv_label_set_text_static(label, buf);
#else
    if (SW6306_ReadVBUS() > 9999) {
        lv_label_set_text_fmt(label, "BAT:  %.2fV  %.3fA", SW6306_ReadVBAT() * 0.001f, SW6306_ReadIBAT() * 0.001f);
    } else {
        lv_label_set_text_fmt(label, "BAT: %.2fV  %.3fA", SW6306_ReadVBAT() * 0.001f, SW6306_ReadIBAT() * 0.001f);
    }
#endif
}

static void bus_timer_cb(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(t);
#ifdef STATIC_BUF
    static char buf[24];
    snprintf(buf, sizeof(buf), "BUS: %.2fV  %.3fA", SW6306_ReadVBUS() * 0.001f, SW6306_ReadIBUS() * 0.001f);
    lv_label_set_text_static(label, buf);
#else
    lv_label_set_text_fmt(label, "BUS: %.2fV  %.3fA", SW6306_ReadVBUS() * 0.001f, SW6306_ReadIBUS() * 0.001f);
#endif
}

static void btn_event_handler(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_ENTER) {
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e ENTER#");
    } else if (key == KeyIndex_NEXT) {
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e NEXT#");
    } else if (key == LV_KEY_PREV) {
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e MENU#");
    }
}

void ui_task_func(void *pvParameters)
{
    (void)pvParameters;
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    lv_obj_t *scr = lv_screen_active();

    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(indev_keypad, group);
    lv_group_set_editing(group, true);

    lv_obj_t *topic_label = lv_label_create(scr);
    lv_label_set_text(topic_label, " #cf3d3e PowerBank!!!#");
    lv_label_set_recolor(topic_label, true);
    lv_obj_set_align(topic_label, LV_ALIGN_TOP_LEFT);
    lv_group_focus_obj(topic_label);

    lv_obj_t *PCAP_label = lv_label_create(scr);
    lv_obj_set_align(PCAP_label, LV_ALIGN_TOP_RIGHT);
    lv_timer_create(pcap_timer_cb, 2000, PCAP_label);

    lv_obj_t *BAT_label = lv_label_create(scr);
    lv_obj_set_align(BAT_label, LV_ALIGN_LEFT_MID);
    lv_timer_create(bat_timer_cb, 500, BAT_label);

    lv_obj_t *BUS_label = lv_label_create(scr);
    lv_obj_set_align(BUS_label, LV_ALIGN_BOTTOM_LEFT);
    lv_timer_create(bus_timer_cb, 500, BUS_label);

    while (1) {
        uint32_t t = lv_timer_handler();
        if (t == LV_NO_TIMER_READY) {
            t = LV_DEF_REFR_PERIOD;
        }
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

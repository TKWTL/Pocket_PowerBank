#include "applications.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"



#define STATIC_BUF

static void pcap_timer_cb(lv_timer_t * t)
{
    lv_obj_t * label = (lv_obj_t *)lv_timer_get_user_data(t);
    #ifdef STATIC_BUF
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d%% ", SW6306_ReadCapacity());
    lv_label_set_text_static(label, buf);
    #else
    lv_label_set_text_fmt(label, "%d%% ", SW6306_ReadCapacity());
    #endif
}
static void bat_timer_cb(lv_timer_t * t)
{
    lv_obj_t * label = (lv_obj_t *)lv_timer_get_user_data(t);
    #ifdef STATIC_BUF
    static char buf[24];
    if(SW6306_ReadVBUS() > 9999)
        snprintf(buf, sizeof(buf), "BAT:  %.2fV  %.3fA", SW6306_ReadVBAT()* 0.001f, SW6306_ReadIBAT()* 0.001f);
    else
        snprintf(buf, sizeof(buf), "BAT: %.2fV  %.3fA", SW6306_ReadVBAT()* 0.001f, SW6306_ReadIBAT()* 0.001f);
    lv_label_set_text_static(label, buf);
    #else
    if(SW6306_ReadVBUS() > 9999)
        lv_label_set_text_fmt(label, "BAT:  %.2fV  %.3fA", SW6306_ReadVBAT()* 0.001f, SW6306_ReadIBAT()* 0.001f);
    else
        lv_label_set_text_fmt(label, "BAT: %.2fV  %.3fA", SW6306_ReadVBAT()* 0.001f, SW6306_ReadIBAT()* 0.001f);
    #endif
}
static void bus_timer_cb(lv_timer_t * t)
{
    lv_obj_t * label = (lv_obj_t *)lv_timer_get_user_data(t);
    #ifdef STATIC_BUF
    static char buf[24];
    snprintf(buf, sizeof(buf), "BUS: %.2fV  %.3fA", SW6306_ReadVBUS()* 0.001f, SW6306_ReadIBUS()* 0.001f);
    lv_label_set_text_static(label, buf);
    #else
    lv_label_set_text_fmt(label, "BUS: %.2fV  %.3fA", SW6306_ReadVBUS()* 0.001f, SW6306_ReadIBUS()* 0.001f);
    #endif
}
static void btn_event_handler(lv_event_t * e)//按键回调函数
{
    uint32_t key = lv_event_get_key(e);

    if(key == LV_KEY_ENTER) {
        //lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), "#cf3d3e Clicked#");
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e ENTER#");
    }
    else if(key == KeyIndex_NEXT) {
        //lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), "#cf3d3e Toggled#");
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e NEXT#");
    }
    else if(key == LV_KEY_PREV) {
        //lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), "#cf3d3e Toggled#");
        lv_label_set_text(lv_event_get_target(e), "#cf3d3e MENU#");
    }
}

void ui_task_func(void *pvParameters)
{
    lv_init(); 
    lv_port_disp_init(); 
    lv_port_indev_init(); 
    
    lv_obj_t *scr = lv_screen_active();;//活动屏幕对象
    
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(indev_keypad, group);
    lv_group_set_editing(group, true);
    
    lv_obj_t *topic_label = lv_label_create(scr); 
    lv_label_set_text(topic_label, " #cf3d3e PowerBank!!!#"); 
    lv_label_set_recolor(topic_label,true);
    lv_obj_set_align(topic_label, LV_ALIGN_TOP_LEFT);
    //lv_obj_add_event_cb(topic_label, btn_event_handler, LV_EVENT_ALL, NULL);
    lv_group_focus_obj(topic_label);
    /*lv_obj_t * btn1 = lv_btn_create(scr);
    lv_group_focus_obj(btn1);          //分组聚焦到对象
    lv_obj_add_event_cb(btn1, btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn1, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *label = lv_label_create(btn1);
    static char lbl[] = "Button";
    lv_label_set_text_static(label, lbl);
    //lv_label_set_text(label, "#cf3d3e Button#");
    //lv_label_set_recolor(label, true);
    //lv_obj_center(label);*/
    
    lv_obj_t *PCAP_label = lv_label_create(scr); 
    lv_obj_set_align(PCAP_label, LV_ALIGN_TOP_RIGHT);
    lv_timer_create(pcap_timer_cb, 2000, PCAP_label);
    
    lv_obj_t *BAT_label = lv_label_create(scr); 
    lv_obj_set_align(BAT_label, LV_ALIGN_LEFT_MID);
    lv_timer_create(bat_timer_cb, 500, BAT_label);
    
    lv_obj_t *BUS_label = lv_label_create(scr); 
    lv_obj_set_align(BUS_label, LV_ALIGN_BOTTOM_LEFT);
    lv_timer_create(bus_timer_cb, 500, BUS_label);
    
    //lv_group_add_obj(group ,btn1);
    // 画个方块
	/*lv_obj_t * test_rect = lv_obj_create(lv_scr_act());
	lv_obj_set_size(test_rect, 50, 50);
	lv_obj_set_style_bg_color(test_rect, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_align(test_rect, LV_ALIGN_TOP_LEFT, 0, 0);*/
    
    while(1)
    {
        uint32_t t = lv_timer_handler();
        if(t == LV_NO_TIMER_READY) t = LV_DEF_REFR_PERIOD;
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}
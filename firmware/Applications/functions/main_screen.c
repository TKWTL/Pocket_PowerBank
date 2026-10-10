/*
 * main_screen.c - 主界面功能（待机信息屏）
 *
 * 主界面的创建/显示与按键处理（随绘制同文件，自包含）：
 *  - 上电先触发（MiaoUI 风格），同时作为菜单动作挂载；
 *  - 主界面激活（非菜单）时，UI 调度（ui_loop）把按键分发到 main_screen_run()。
 * 声明见 functions.h（统一引用头）。
 */
#include "applications.h"    /* SW6306 等外设驱动 */
#include "lvgl.h"
#include "menu.h"
#include "functions.h"       /* 本模块统一声明 */
#include "main_icons.h"      /* 主界面电池 A8 位图 */
#include <string.h>
#include "mini_format.h"


/* 主界面对象（应用生命周期：进入 create、退出 destroy） */
static lv_obj_t *s_main_scr;
static lv_obj_t *s_bat_bar;  /* 电量条-主体（相对x4..38=绝对124..158，35px，右对齐，底层） */
static lv_obj_t *s_bat_tab;  /* 电量条-电极（相对x1..3=绝对121..123，3px，仅电极行显示，避免漏出图片） */
static lv_obj_t *s_bat_pct;  /* 电池内电量百分比 label */
static lv_obj_t *s_bat_img;  /* 电池位图（随箭头显隐自动移动） */
static int32_t s_bat_x = 72; /* 电池 x 基准：箭头显示 72 / 隐藏 67（对准箭头+电池整体中心 86.5） */
static uint16_t s_bar_tenth; /* 电量条显示值0.1%，空闲=实际，充电=0→实际循环 */

static void icon_update(void);   /* 前向声明（power_label_update 在电池自动移动时调用） */
static lv_timer_t *s_tmr_port;   /* 端口状态刷新（destroy 须删，防回调访问已删对象） */
static lv_timer_t *s_tmr_power;  /* 功率刷新 */
static lv_timer_t *s_tmr_voltage;/* 接口电压刷新 */
static lv_timer_t *s_tmr_icon;   /* 电池图标刷新 */
static lv_obj_t *s_temp_label;   /* NTC 温度 label（信息行 y=22，右对齐） */
static lv_obj_t *s_time_label;   /* 时间 HH:MM label（信息行 y=22，左对齐） */
static lv_obj_t *s_dir_label;    /* 方向指示 label（功率框外，功率框与电池之间） */

void main_screen_run(app_action_t action);   /* 前向声明（menu_app_main 定义在文件尾） */

/* ---------- 主界面定时刷新（标签对象经 user_data 传入） ---------- */
/* 第一行：端口状态（A1 红 / C1 黄，recolor 色码；本部分无 i18n）
 *   - 单口活动时：快充协议跟在端口后（如 C:PDPPS），无快充协议显示 5V
 *   - 多口/同时充放：端口状态照常显示，但不显示协议（多口时单组 qcstat 无法分口）
 *   - 端口未启动：OFF */
/* 端口状态标签立即刷新（create 时调用一次，之后 timer 周期刷新） */
static void port_label_update(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    static char buf[48];   /* 含 recolor 色码：最长双口 "#FF0000 A##000000 +#FFEF00 C##000000 :5V#" = 42 字符（色码后空格不绘制） */
    const char *proto = SW6306_ReadProtocol();
    uint8_t c_in  = SW6306_IsCharging() ? 1 : 0;
    uint8_t c_out = (SW6306_IsDischarging() && SW6306_IsPortC1ON()) ? 1 : 0;
    uint8_t a_out = SW6306_IsPortA1ON() ? 1 : 0;
    uint8_t c_act = c_in || c_out;
    uint8_t has_proto = (proto && strcmp(proto, "NONE") != 0) ? 1 : 0;
    const char *p_s;

    if (a_out && c_act) {
        /* 双口同时打开（A+C 放电 / 同时充放）：紧凑显示；多口无法逐口报协议 → 5V
         * LVGL recolor 色码后第一个空格不绘制 → 可见 A+C:5V（6 字符 = 48px） */
        mini_snprintf(buf, sizeof(buf), "#FF0000 A##000000 +#FFEF00 C##000000 :5V#");
    } else if (a_out) {
        /* 仅 A 口放电 */
        p_s = has_proto ? proto : "5V";
        mini_snprintf(buf, sizeof(buf), "#FF0000 A##000000 :%s#", p_s);
    } else if (c_act) {
        /* 仅 C 口（输入或输出） */
        p_s = has_proto ? proto : "5V";
        mini_snprintf(buf, sizeof(buf), "#F0C000 C##000000 :%s#", p_s);
    } else {
        /* 无端口启动（黑色，区别于白色内容） */
        mini_snprintf(buf, sizeof(buf), "#000000 OFF#");
    }
    lv_label_set_text_static(label, buf);
}

static void port_timer_cb(lv_timer_t *t)
{
    port_label_update((lv_obj_t *)lv_timer_get_user_data(t));
    lv_obj_invalidate(s_main_scr);   /* 强制整屏重绘，清除 label 文字残影/格点 */
}

/* 功率标签立即刷新（create 时调用一次）。方向指示 <>/ 放在功率框外（s_dir_label） */
static void power_label_update(lv_obj_t *label)
{
    uint16_t ibus = SW6306_ReadIBUS();
    uint32_t uw = (uint32_t)SW6306_ReadVBUS() * ibus; /* mV*mA = uW */
    uint32_t centiw;

    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    static char buf[10];

    if (ibus < 5U) uw = 0U;
    centiw = (uw + 5000U) / 10000U; /* 0.01W，四舍五入 */
    mini_snprintf(buf, sizeof(buf), "%02lu.%02luW",
             (unsigned long)(centiw / 100U),
             (unsigned long)(centiw % 100U));
    lv_label_set_text_static(label, buf);

    /* 方向指示（框外）：< 放电 / > 充电，读 REG0x18 镜像；无功率留空 */
    if (s_dir_label) {
        if (uw != 0U) {
            static char dbuf[2];
            dbuf[0] = SW6306_IsDischarging() ? '<' : '>';
            dbuf[1] = '\0';
            lv_label_set_text_static(s_dir_label, dbuf);
        } else {
            lv_label_set_text_static(s_dir_label, "");
        }
        /* 电池框自动移动：箭头显示时保持 x=72；隐藏时中心对准
         * 箭头+电池整体中心（62..111 中心 86.5）→ x=67 */
        int32_t new_x = (uw != 0U) ? 72 : 67;
        if (new_x != s_bat_x) {
            s_bat_x = new_x;
            if (s_bat_img) {
                lv_obj_set_pos(s_bat_img, s_bat_x, 23);
            }
            icon_update();
        }
    }
}

static void power_timer_cb(lv_timer_t *t)
{
    power_label_update((lv_obj_t *)lv_timer_get_user_data(t));
    lv_obj_invalidate(s_main_scr);   /* 强制整屏重绘，清除 label 文字残影/格点 */
}

/* 接口电压标签立即刷新（0.01V，如 12.00V；全整数格式，避免拉入printf浮点转换） */
static void voltage_label_update(lv_obj_t *label)
{
    uint32_t cv = ((uint32_t)SW6306_ReadVBUS() + 5U) / 10U;
    static char buf[8];

    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    mini_snprintf(buf, sizeof(buf), "%02lu.%02luV",
             (unsigned long)(cv / 100U),
             (unsigned long)(cv % 100U));
    lv_label_set_text_static(label, buf);
}

static void voltage_timer_cb(lv_timer_t *t)
{
    voltage_label_update((lv_obj_t *)lv_timer_get_user_data(t));
    lv_obj_invalidate(s_main_scr);   /* 强制整屏重绘，清除 label 文字残影/格点 */
}

/* 电池百分比 + 电量条立即刷新（create 时调用一次）。
 * 电量条动画用0.1%整数保存；500ms每次+16.7%，约3s扫满100%。 */
static void icon_update(void)
{
    static char buf[8];

    if (SW6306_IsCharging()) {
        uint16_t real_tenth = (uint16_t)SW6306_ReadCapacity() * 10U;
        s_bar_tenth = (uint16_t)(s_bar_tenth + 167U);
        if (s_bar_tenth >= real_tenth) s_bar_tenth = 0U;
    } else {
        s_bar_tenth = (uint16_t)SW6306_ReadCapacity() * 10U;
    }

    /* 电量条：拆为主体系(35px,x=124..158,高14) + 电极(x=121..123,3px,仅电极行高6)，
     * 避免电极段在非电极行漏到图片外；≥30%绿 / ≥10%黄(DFDF00) / 其余红 */
    {
        uint8_t pct = (uint8_t)(s_bar_tenth / 10U);
        int32_t fill = (int32_t)(38 * pct / 100);   /* 总长38 = 主体系35 + 电极3 */
        if (fill < 0) fill = 0;
        if (fill > 38) fill = 38;
        int32_t body = (fill < 35) ? fill : 35;
        int32_t tab  = (fill > 35) ? (fill - 35) : 0;   /* 0..3 */

        lv_obj_set_size(s_bat_bar, body, 14);
        lv_obj_set_pos(s_bat_bar, (s_bat_x + 4) + (35 - body), 24);   /* 主体：右对齐 s_bat_x+38 */
        lv_obj_set_style_bg_color(s_bat_bar,
            (pct >= 30) ? lv_color_hex(0x00F00F) :
            (pct >= 10) ? lv_color_hex(0xEFDF00) :
                          lv_color_hex(0xFF0000), 0);

        if (tab > 0) {
            lv_obj_remove_flag(s_bat_tab, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(s_bat_tab, tab, 6);
            lv_obj_set_pos(s_bat_tab, (s_bat_x + 1) + (3 - tab), 28);   /* 电极行右对齐 s_bat_x+3 */
        } else {
            lv_obj_add_flag(s_bat_tab, LV_OBJ_FLAG_HIDDEN);
        }
        /* 电极条只会在 fill>35（电量>95%）时出现，此时 pct>=30，必定是绿色档 */
        lv_obj_set_style_bg_color(s_bat_tab, lv_color_hex(0x00F00F), 0);
    }

    /* 超低电量（≤5%）时容量百分比闪烁：算法与读秒冒号相同但奇偶性相反
     * （秒奇数→冒号隐藏、百分比显示；秒偶数→冒号显示、百分比隐藏，交错闪烁）。
     * RTC 镜像无效时无走秒基准 → 保持常显不闪。 */
    {
        uint8_t pct = SW6306_ReadCapacity();
        mini_snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text_static(s_bat_pct, buf);
        if (pct <= 5U && SW6306_IsDischarging()) {
            uint8_t rtc_valid = (SD3078_ReadMonth() >= 1U && SD3078_ReadMonth() <= 12U);
            if (rtc_valid && !(SD3078_ReadSec() & 1U)) {
                lv_obj_add_flag(s_bat_pct, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(s_bat_pct, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_remove_flag(s_bat_pct, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* NTC 温度：仅用整数格式化，避免printf浮点转换代码。 */
    {
        float temp = SW6306_ReadNTCTemp();
        int32_t t10 = (int32_t)(temp * 10.0f + ((temp >= 0.0f) ? 0.5f : -0.5f));
        uint32_t mag = (uint32_t)((t10 < 0) ? -t10 : t10);
        static char tbuf[10];

        mini_snprintf(tbuf, sizeof(tbuf), "%s%lu.%lu°C",
                 (t10 < 0) ? "-" : "",
                 (unsigned long)(mag / 10U),
                 (unsigned long)(mag % 10U));
        lv_label_set_text_static(s_temp_label, tbuf);
    }

    /* 时间 HH:MM：load_task 更新的 SD3078 镜像（UI 只读），独立缓冲；
     * 走秒提示：秒为奇数时冒号显示为空格（闪烁），等宽保持宽度不变 */
    static char hbuf[8];
    if (SD3078_ReadMonth() >= 1U && SD3078_ReadMonth() <= 12U &&
        SD3078_ReadHour() <= 23U && SD3078_ReadMin() <= 59U) {
        if (SD3078_ReadSec() & 1U) {
            mini_snprintf(hbuf, sizeof(hbuf), "%02d %02d", SD3078_ReadHour(), SD3078_ReadMin());
        } else {
            mini_snprintf(hbuf, sizeof(hbuf), "%02d:%02d", SD3078_ReadHour(), SD3078_ReadMin());
        }
    } else {
        mini_snprintf(hbuf, sizeof(hbuf), "--:--");
    }
    lv_label_set_text_static(s_time_label, hbuf);
}

static void icon_timer_cb(lv_timer_t *t)
{
    (void)t;
    icon_update();
    lv_obj_invalidate(s_main_scr);   /* 强制整屏重绘，清除 label 文字残影/格点 */
}

/* ---------- 主界面（待机信息屏） ---------- */

/* 应用 create：创建主界面（幂等重建）。用独立屏幕对象（lv_obj_create(NULL)），
 * 可整体销毁释放内存（主界面不特殊，也是应用）。 */
static void main_screen_create(void)
{
    lv_obj_t *label;

    if (s_main_scr) {
        return;   /* 已创建 */
    }
    s_main_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_main_scr, lv_display_get_horizontal_resolution(NULL),
                              lv_display_get_vertical_resolution(NULL));
    /* 内容（温度 label）贴右缘时水平超出 1px，会触发 LVGL 横向滚动条（底部 2px 灰条）。
     * 静态界面无需滚动：禁用滚动条，从根上消除该灰条。 */
    lv_obj_set_scrollbar_mode(s_main_scr, LV_SCROLLBAR_MODE_OFF);

    /* 简单背景色方案：整屏深灰背景（滚动条已禁用，直接设到 screen 即可） */
    lv_obj_set_style_bg_color(s_main_scr, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(s_main_scr, LV_OPA_COVER, 0);

    /* 主屏：两个圆角方框（上青下蓝），各包一行文字；两行垂直均匀分布 */

    /* 上框（青色）：第一行端口状态（A 红 / C 黄 recolor；其余白色），靠左上 */
    lv_obj_t *frame1 = lv_obj_create(s_main_scr);
    lv_obj_set_size(frame1, 59, 17);      /* 7 字符宽（terminus-u14b 等宽 8px/字符 = 56px） */
    lv_obj_align(frame1, LV_ALIGN_TOP_LEFT, 2, 2);   /* 右移2px */
    lv_obj_set_style_radius(frame1, 3, 0);
    lv_obj_set_style_bg_color(frame1, lv_color_hex(0x00F0F0), 0);   /* 青 */
    lv_obj_set_style_border_width(frame1, 0, 0);
    lv_obj_set_style_pad_all(frame1, 0, 0);
    lv_obj_remove_flag(frame1, LV_OBJ_FLAG_SCROLLABLE);   /* 静态框：禁用滚动，避免内容溢出显示 2px 灰色滚动条 */

    label = lv_label_create(frame1);
    lv_label_set_recolor(label, true);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 1, 1);   /* #sym:frame1 内文字水平垂直居中 */
    port_label_update(label);   /* 立即显示端口状态（不等 500ms timer 首次触发） */
    s_tmr_port = lv_timer_create(port_timer_cb, 500, label);

    /* 接口功率框（蓝色）：第二行左下（x=2..61, y=22..35，右移2px） */
    lv_obj_t *frame2 = lv_obj_create(s_main_scr);
    lv_obj_set_size(frame2, 59, 17);      /* 7 字符宽 */
    lv_obj_set_pos(frame2, 2, 22);
    lv_obj_set_style_radius(frame2, 3, 0);
    lv_obj_set_style_bg_color(frame2, lv_color_hex(0x000FFF), 0);   /* 蓝 */
    lv_obj_set_style_border_width(frame2, 0, 0);
    lv_obj_set_style_pad_all(frame2, 0, 0);
    lv_obj_remove_flag(frame2, LV_OBJ_FLAG_SCROLLABLE);   /* 静态框：禁用滚动，避免内容溢出显示 2px 灰色滚动条 */

    label = lv_label_create(frame2);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 1, 1);   /* 功率文字向右偏移2px，向下偏移1px */
    power_label_update(label);   /* 立即显示功率（不等 500ms timer 首次触发） */
    s_tmr_power = lv_timer_create(power_timer_cb, 500, label);

    /* 方向指示 label（框外）：功率框右侧与电池图标之间（x=62..69） */
    s_dir_label = lv_label_create(s_main_scr);
    lv_obj_set_style_text_color(s_dir_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_dir_label, menu_font_main(), 0);
    lv_obj_set_pos(s_dir_label, 62, 23);
    lv_label_set_text_static(s_dir_label, "");

    /* 接口电压框（绿色）：第一行中间，xx.xxV（最大 6 字符 = 48px，框宽 52），左移1px */
    lv_obj_t *frame_v = lv_obj_create(s_main_scr);
    lv_obj_set_size(frame_v, 52, 17);
    lv_obj_set_pos(frame_v, 64, 2);
    lv_obj_set_style_radius(frame_v, 3, 0);
    lv_obj_set_style_bg_color(frame_v, lv_color_hex(0x00D000), 0);   /* 绿 */
    lv_obj_set_style_border_width(frame_v, 0, 0);
    lv_obj_set_style_pad_all(frame_v, 0, 0);
    lv_obj_remove_flag(frame_v, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(frame_v);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, menu_font_main(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 1, 1);
    voltage_label_update(label);   /* 立即显示电压 */
    s_tmr_voltage = lv_timer_create(voltage_timer_cb, 500, label);

    /* 电量条（底层矩形，先创建在电池位图之下；电池边框与百分比盖住它） */
    s_bat_bar = lv_obj_create(s_main_scr);
    lv_obj_remove_flag(s_bat_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_bat_bar, 0, 0);
    lv_obj_set_style_radius(s_bat_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bat_bar, 0, 0);
    lv_obj_set_size(s_bat_bar, 0, 14);
    lv_obj_set_pos(s_bat_bar, s_bat_x + 4, 24);   /* 主体：右对齐 s_bat_x+38，y相对24..37 */

    /* 电极：x=121..123（相对x1..3，3px），仅电极行 → 绝对 y7..12（高6），避免漏出图片 */
    s_bat_tab = lv_obj_create(s_main_scr);
    lv_obj_remove_flag(s_bat_tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_bat_tab, 0, 0);
    lv_obj_set_style_radius(s_bat_tab, 0, 0);
    lv_obj_set_style_pad_all(s_bat_tab, 0, 0);
    lv_obj_set_size(s_bat_tab, 0, 6);
    lv_obj_set_pos(s_bat_tab, s_bat_x + 1, 28);
    lv_obj_add_flag(s_bat_tab, LV_OBJ_FLAG_HIDDEN);

    /* 电池图标：内部居中放电量百分比（recolor 白），x 随箭头显隐自动移动 */
    s_bat_img = lv_image_create(s_main_scr);
    lv_image_set_src(s_bat_img, &main_icon_battery);
    lv_obj_set_style_image_recolor(s_bat_img, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(s_bat_img, LV_OPA_COVER, 0);
    lv_obj_align(s_bat_img, LV_ALIGN_TOP_LEFT, s_bat_x, 23);   /* 40px 宽 → 中列 */

    s_bat_pct = lv_label_create(s_bat_img);
    lv_obj_set_style_text_color(s_bat_pct, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_bat_pct, menu_font_main(), 0);
    lv_obj_align(s_bat_pct, LV_ALIGN_CENTER, 1, 0);

    /* 时间+温度（右上/右下，第三列）：整屏深灰背景，时间左对齐、温度右对齐 */
    s_time_label = lv_label_create(s_main_scr);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xC0C0C0), 0);   /* 灰 */
    lv_obj_set_style_text_font(s_time_label, menu_font_main(), 0);
    lv_obj_set_pos(s_time_label, 119, 4);   /* 右上：HH:MM（5字符=40px），左移2px */

    s_temp_label = lv_label_create(s_main_scr);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xC0C0C0), 0);   /* 灰 */
    lv_obj_set_style_text_font(s_temp_label, menu_font_main(), 0);
    lv_obj_set_width(s_temp_label, 56);
    lv_obj_set_style_text_align(s_temp_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_temp_label, 102, 24);   /* 右下：右对齐，下移2px */

    icon_update();   /* 立即显示电量条、电量%、时间与温度（不等 500ms timer 首次触发） */
    s_tmr_icon = lv_timer_create(icon_timer_cb, 500, NULL);

    /* 强制整屏重绘一次：LVGL 只刷新失效区域，空白区若不重绘会露出
     * GC9D01 上电残留（白点）；全屏失效一次即可覆盖清除。 */
    lv_obj_invalidate(s_main_scr);
}

/* 应用 activate：加载主屏并全屏重绘（redraw handler 非菜单态时调用） */
static void main_screen_activate(void)
{
    if (s_main_scr) {
        lv_screen_load(s_main_scr);
        lv_obj_invalidate(s_main_scr);
    }
}

/* 应用 destroy：删除定时器（防回调访问已删对象）与主屏，释放 LVGL 堆内存 */
static void main_screen_destroy(void)
{
    if (s_tmr_port)  { lv_timer_delete(s_tmr_port);  s_tmr_port  = NULL; }
    if (s_tmr_power) { lv_timer_delete(s_tmr_power); s_tmr_power = NULL; }
    if (s_tmr_voltage){ lv_timer_delete(s_tmr_voltage); s_tmr_voltage = NULL; }
    if (s_tmr_icon)  { lv_timer_delete(s_tmr_icon);  s_tmr_icon  = NULL; }
    if (s_main_scr)  { lv_obj_delete(s_main_scr);    s_main_scr  = NULL; }
    s_bat_bar = NULL;
    s_bat_tab = NULL;
    s_bat_pct = NULL;
    s_bat_img = NULL;
    s_temp_label = NULL;
    s_time_label = NULL;
    s_dir_label = NULL;
}

/* 主界面按键处理（主界面激活时由 UI 调度分发，随绘制同文件，自包含）：
 *  - APP_ACTION_UP           （MENU 键）→ 打开菜单
 *  - APP_ACTION_ENTER_DBL    （CONF 双击）→ 开关 WLED
 *  - APP_ACTION_ENTER_HOLD   （CONF 长按）→ 调光（方向默认增，长按结束切换，始终存储）
 *  - APP_ACTION_DOWN_DBL     （PWR/NEXT 双击）→ 小电流/慢充模式。
 * 可用 SW6306_AlgoGetSpecialMode() 的只读状态供主界面后续显示。 */
void main_screen_run(app_action_t action)
{
    static int8_t  s_wled_dir = -1;     /* 调光方向：+1 增亮 / -1 减亮；默认减（变暗），长按结束切换 */
    static uint8_t s_dim_div = 0;        /* 每 8 次长按重复发一个 ±1 档请求 */

    switch (action) {
    case APP_ACTION_UP:      /* MENU/PREV：普通主屏开根菜单；锁屏主屏回休眠前页面 */
        main_screen_destroy();
        menu_open_from_main();
        break;
    case APP_ACTION_ENTER_DBL:   /* CONF 双击：只提交 WLED 开关请求 */
        WLED_AlgoRequestToggle();
        break;
    case APP_ACTION_ENTER_HOLD:  /* CONF 长按：持续调光；UI 只提交 ±1 档请求 */
        if (++s_dim_div >= 8) {
            s_dim_div = 0;
            WLED_AlgoRequestBrightnessStep(s_wled_dir);
        }
        break;
    case APP_ACTION_ENTER_HOLD_END:  /* CONF 长按结束：切换调光方向（下次长按反向） */
        s_wled_dir = (int8_t)(-s_wled_dir);
        break;
    case APP_ACTION_DOWN_DBL: /* NEXT/PWR 双击：根据充电状态切换模式 */
        SW6306_AlgoRequestSpecialToggle();
        break;
    default:
        break;
    }
}

/* 主界面应用注册（menu.h 声明；上电/菜单 Return/应用退出统一走菜单应用模型） */
const menu_app_t menu_app_main = {
    .label    = "Main",
    .create   = main_screen_create,
    .activate = main_screen_activate,
    .run      = main_screen_run,
    .destroy  = main_screen_destroy,
};

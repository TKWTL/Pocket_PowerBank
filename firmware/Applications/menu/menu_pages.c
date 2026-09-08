/*
 * menu_pages.c - 菜单树集中配置（唯一配置入口）
 *
 * 新增页面步骤：
 *  1. 在"页面前向声明"处加一行声明（支持任意层级套娃，无顺序限制）；
 *  2. 用 MENU_ITEM_* 声明条目数组（一行一项）；
 *  3. 用 MENU_PAGE_* 声明页面，条目 MENU_ITEM_PAGE_("名称", &子页)
 *     引用子页即实现套娃。
 *
 * 配置宏见 menu.h（MENU_ITEM_* / MENU_PAGE_*）。
 */
#include <string.h>
#include "menu_pages.h"
#include "menu_theme.h"
#include "menu_icons.h"     /* 根页图标（LVGL A8 位图） */
#include "applications.h"   /* 传感器/时间共享镜像（load_task 0.5s 同步）：g_sensor_mirror */
#include "drivers.h"        /* 统一驱动包含头：GC9D01_SetBL / SW6306_SetMaxOutputPower 等 */
#include "functions.h"     /* 菜单 MENU_ITEM_ACTION_ 调用的一次性函数（含主界面动作） */
#include "framework/pm_api.h"  /* 电源管理：自动休眠开关/超时设置（pm_api_set_sleep_timeout） */

/* ---------- 状态页实时信息缓冲区（ui_task 定时更新） ---------- */
char menu_status_bat[24] = "--.-V --.-A";

/* 电池初始能量 mWh（2S1P 30Q 标称 7.2V×3.0Ah = 21.6Wh；健康度分母）。
 * PowerBank→Battery 设置页可把当前库仑计最大能量写入它，作为健康度参考。 */
static int32_t s_batt_init_energy = 21600;

/* ==================== 国际化（i18n）语言表 ====================
 * 键 = 英文文本（menu_pages.c 里所有 label/title/toggle 值直接用英文作键）。
 * menu_tr(key)：英文态返回 en、中文态返回 zh、未配置回退 key 本身。
 * 中文字库（14/12px 部分字符集）待全部文案确认后生成，生成前切中文缺字形。 */
static const menu_tr_t menu_tr_table[] = {
    /* ---- 页面标题 ---- */
    { "Menu",            "Menu",             "菜单" },
    { "Settings",        "Settings",         "设置" },
    { "Status",          "Status",           "状态" },
    { "Tools",           "Tools",            "工具" },
    { "Games",           "Games",            "游戏" },
    { "About",           "About",            "关于" },
    { "Display",         "Display",          "显示" },
    { "Time",            "Time",             "时间" },
    { "PowerBank",       "PowerBank",        "充电宝" },
    { "Reset",           "Reset",            "复位" },
    /* ---- 条目 ---- */
    { "Return",          "Return",           "返回" },
    { "Backlight",       "Backlight",        "背光" },
    { "Theme",           "Theme",            "主题" },
    { "Color",           "Color",            "颜色" },
    { "Max Power",       "Max Power",        "最大功率" },
    /* ---- PowerBank 子页：Protocol / PowerLimit / Battery ---- */
    { "Protocol",        "Protocol",         "协议" },
    { "PowerLimit",      "PowerLimit",       "功率限制" },
    { "Output",          "Output",           "输出功率" },
    { "Input",           "Input",            "输入功率" },
    { "PD out",          "PD out",           "PD 输出" },
    { "PD in",           "PD in",            "PD 输入" },
    { "PPS Broadcast",   "PPS Broadcast",    "PPS 能力播发" },
    { "PPS1",            "PPS1",             "PPS1" },
    { "PPS3",            "PPS3",             "PPS3" },
    { "UFCS Broadcast",  "UFCS Broadcast",   "UFCS 能力播发" },
    { "QC",              "QC",               "QC" },
    { "FCP",             "FCP",              "FCP" },
    { "AFC out",         "AFC out",          "AFC 输出" },
    { "AFC in",          "AFC in",           "AFC 输入" },
    { "SCP out",         "SCP out",          "SCP 输出" },
    { "SCP in",          "SCP in",           "SCP 输入" },
    { "PE",              "PE",               "PE" },
    { "SFCP",            "SFCP",             "SFCP" },
    { "VOOC out",        "VOOC out",         "VOOC 输出" },
    { "VOOC in",         "VOOC in",          "VOOC 输入" },
    { "SVOOC",           "SVOOC",            "SVOOC" },
    { "UFCS out",        "UFCS out",         "UFCS 输出" },
    { "UFCS in",         "UFCS in",          "UFCS 输入" },
    { "Cap Learn",      "Cap Learn",       "容量学习" },
    { "Record SOH",      "Record SOH",       "记录SOH" },
    { "Reset Now",       "Reset Now",        "立即复位" },
    { "Coming Soon",     "Coming Soon",      "敬请期待" },
    { "Pocket PowerBank","Pocket PowerBank", "口袋充电宝" },
    { "FW 1.0.0",        "FW 1.0.0",         "固件 1.0.0" },
    { "AT32F423+LVGL9",  "AT32F423+LVGL9",   "AT32F423+LVGL9" },
    /* ---- Toggle 值 ---- */
    { "Light",           "Light",            "浅色" },
    { "Dark",            "Dark",             "深色" },
    { "45W",             "45W",              "45W" },
    { "18W",             "18W",              "18W" },
    { "ON",              "ON",               "开" },
    { "OFF",             "OFF",              "关" },
    { "On",              "On",               "开" },
    { "Off",             "Off",              "关" },
    /* ---- 显示页：休眠 ---- */
    { "Auto Sleep",      "Auto Sleep",       "自动休眠" },
    { "None",            "None",             "不休眠" },
    { "Sleep Time",      "Sleep Time",       "休眠时间" },
    { "s",               "s",                "秒" },
    /* ---- 状态页 / 指示 / 设置 ---- */
    { "status.bat",      "%.2fV %.3fA",      "%.2fV %.3fA" },
    { "EDIT",            "EDIT",             "编辑" },
    { "Language",        "Language",         "语言" },
    /* ---- 状态页子页 ---- */
    { "Battery",         "Battery",          "电池" },
    { "Accelerator",     "Accelerator",      "加速度" },
    { "Timer",           "Timer",            "时钟" },
    { "status.maxcap",   "Max: %.2f Wh",     "最大能量: %.2f Wh" },
    { "status.now",      "Now: %.2f Wh",     "当前能量: %.2f Wh" },
    { "status.health",   "Health: %.0f%%",   "健康度: %.0f%%" },
    { "status.learn",    "Learn:%s",        "容量学习:%s" },
    { "learn.waiting",   "Waiting",          "等待" },
    { "learn.ing",       "Learning",         "学习中" },
    { "learn.done",      "Done",             "已完成" },
    { "learn.unknown",   "Unknown",          "未知" },
    { "status.accel_x",  "X: %.2f g",         "X轴: %.2f g" },
    { "status.accel_y",  "Y: %.2f g",         "Y轴: %.2f g" },
    { "status.accel_z",  "Z: %.2f g",         "Z轴: %.2f g" },
    { "status.time",     "%02d:%02d:%02d",    "%02d:%02d:%02d" },
    { "status.date",     "20%02d-%02d-%02d",  "20%02d-%02d-%02d" },
    { "status.temp",     "Temp: %d C",        "温度: %d C" },
    { "status.vbackup",  "Vbackup: %.2fV",    "备用电池: %.2fV" },
    /* ---- 时间页（Time，SD3078 时间设置 + 备用电池充电） ---- */
    { "Sec",             "Sec",               "秒" },
    { "Min",             "Min",               "分" },
    { "Hour",            "Hour",              "时" },
    { "Day",             "Day",               "日" },
    { "Month",           "Month",             "月" },
    { "Year",            "Year",              "年" },
    { "Backup Charge",   "Backup Charge",     "备用电池充电" },
    /* ---- 主界面（functions/main_screen.c） ---- */
    { "main.title",      " #cf3d3e PowerBank!!!#", " #cf3d3e 充电宝!!!#" },
    { "main.bat",        "BAT: %.2fV  %.3fA",      "电池: %.2fV  %.3fA" },
    { "main.bat.space",  "BAT:  %.2fV  %.3fA",     "电池:  %.2fV  %.3fA" },
    { "main.bus",        "BUS: %.2fV  %.3fA",      "输出: %.2fV  %.3fA" },
};

static menu_lang_t s_menu_lang = MENU_LANG_EN;

void menu_lang_set(menu_lang_t lang)
{
    s_menu_lang = lang;
}

menu_lang_t menu_lang_get(void)
{
    return s_menu_lang;
}

const char *menu_tr(const char *key)
{
    unsigned i;

    if (!key) {
        return key;
    }
    for (i = 0; i < sizeof(menu_tr_table) / sizeof(menu_tr_table[0]); i++) {
        if (strcmp(menu_tr_table[i].key, key) == 0) {
            return (s_menu_lang == MENU_LANG_ZH) ? menu_tr_table[i].zh
                                                 : menu_tr_table[i].en;
        }
    }
    return key;   /* 未配置：回退英文（key 即英文文本） */
}

/* 按语言返回字体（全局共享：菜单渲染 + 主界面都用）。
 * 中文字库（14/12px 部分字符集）待全部文案确认后生成——
 * TODO：生成 lv_font_menucn_14 / lv_font_menucn_12 后，中文态分别返回它们。 */
const lv_font_t *menu_font_main(void)
{
    return &lv_font_ter_u14b;
}

const lv_font_t *menu_font_small(void)
{
    return &lv_font_montserrat_12;
}

/* ==================== 显示页（Display，三级，归拢显示相关项） ==================== */
static uint8_t s_theme_toggle = 0;          /* 0=深色 1=浅色 */
static int32_t s_theme_color_idx = 0;       /* 主题色在色板中的下标 */
static int32_t s_backlight = 8;             /* 背光亮度 1~16，默认 8 */
static int32_t s_sleep_idx = 4;             /* 自动休眠：ENUM 选项下标（默认 30s），0=不休眠 */
static uint8_t s_lang = 0;                  /* 0=English 1=中文（语言切换，字库就绪前勿切中文） */

/* 自动休眠选项文本（元素经 menu_tr 本地化；数字秒值通用无需翻译） */
static const char * const s_sleep_opts[] = {
    "None", "5s", "10s", "15s", "30s", "60s", "120s", "300s", "600s",
};
/* 与 s_sleep_opts 一一对应：实际超时秒数（0=永久不休眠） */
static const int s_sleep_opts_sec[] = { 0, 5, 10, 15, 30, 60, 120, 300, 600 };

/* 自动休眠：合并开关+超时为单个枚举，选择即生效（0=No Sleep 永久不休眠） */
static void sleep_apply(menu_item_t *it)
{
    (void)it;
    if (s_sleep_idx >= 0 && s_sleep_idx < (int32_t)(sizeof(s_sleep_opts_sec) / sizeof(s_sleep_opts_sec[0]))) {
        pm_api_set_sleep_timeout(s_sleep_opts_sec[s_sleep_idx]);
    }
}

static void lang_apply(menu_item_t *it)
{
    (void)it;
    menu_lang_set(s_lang ? MENU_LANG_ZH : MENU_LANG_EN);
    menu_notify_changed();
}

static void theme_toggle_apply(menu_item_t *it)
{
    (void)it;
    menu_theme_set(s_theme_toggle ? MENU_THEME_LIGHT : MENU_THEME_DARK);
    menu_notify_changed();
}

static void theme_color_apply(menu_item_t *it)
{
    (void)it;
    menu_theme_set_primary(menu_palette[(uint8_t)s_theme_color_idx]);
    menu_notify_changed();
}

/* 背光 1~16 → GC9D01_SetBL 0~255（值越大越亮；占空比越低越亮）。
 * 菜单步进时由 backlight_apply 调用，开机时由 ui_task 调用一次应用默认值。 */
void menu_backlight_apply(void)
{
    GC9D01_SetBL((uint8_t)((uint32_t)s_backlight * 255U / 16U));
}

static void backlight_apply(menu_item_t *it)
{
    (void)it;
    menu_backlight_apply();
}

/* ==================== 页面前向声明（套娃：任意页面可引用任意页面，无顺序限制）
 * 注意：这里用不带 static 的试探性定义，与下方 MENU_PAGE_* 宏展开的
 * 正式定义（也不带 static）链接属性一致；链接器合并为同一份 RAM 对象。 */
menu_page_t menu_page_display, menu_page_time, menu_page_powerbank, menu_page_reset;
menu_page_t menu_page_settings;
menu_page_t menu_page_status;      /* ui_task 用 &menu_page_status 判断当前页 */
menu_page_t menu_page_status_battery, menu_page_status_accel, menu_page_status_timer;
menu_page_t menu_page_protocol, menu_page_powerlimit, menu_page_battery_set;
menu_page_t menu_page_tools, menu_page_games, menu_page_about;

/* ==================== 显示页（Display，三级，归拢显示相关项） ==================== */
static const menu_item_t menu_items_display[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_VALUE_("Backlight", &s_backlight, 1, 16, 1, NULL, backlight_apply),
    MENU_ITEM_TOGGLE_("Theme", &s_theme_toggle, "Light", "Dark", theme_toggle_apply),
    MENU_ITEM_VALUE_("Color", &s_theme_color_idx, 0, MENU_PALETTE_COUNT - 1, 1, NULL, theme_color_apply),
    MENU_ITEM_ENUM_("Auto Sleep", &s_sleep_idx, s_sleep_opts, 9, sleep_apply),
};
MENU_PAGE_("Display", menu_page_display, menu_items_display);

/* ==================== 时间页（Time，SD3078 时间设置 + 备用电池充电） ==================== */
static int32_t s_time_sec = 0;      /* 秒 0~59 */
static int32_t s_time_min = 0;      /* 分 0~59 */
static int32_t s_time_hour = 0;     /* 时 0~23 */
static int32_t s_time_day = 1;      /* 日 1~31 */
static int32_t s_time_month = 1;    /* 月 1~12 */
static int32_t s_time_year = 24;    /* 年 00~99（2000+） */
static uint8_t s_rtc_charge = 0;    /* 备用电池充电：1=开 0=关（默认关，与驱动默认禁止充电一致） */

/* 进入时间页：从 SD3078 驱动句柄镜像读当前时间（load_task 定期刷新），填充设置变量 */
void menu_time_read(void)
{
    if (SD3078_IsInitialized()) {
        s_time_sec   = SD3078_ReadSec();
        s_time_min   = SD3078_ReadMin();
        s_time_hour  = SD3078_ReadHour();
        s_time_day   = SD3078_ReadDay();
        s_time_month = SD3078_ReadMonth();
        s_time_year  = SD3078_ReadYear();
    }
}

/* 任一时间字段步进后：请求设置时间（写入 SD3078 驱动句柄，由 load_task 经
 * SD3078_TimeSetProcess 提交写回；星期保留，不在菜单中设置） */
static void time_apply(menu_item_t *it)
{
    (void)it;
    SD3078_RequestTimeSet((uint8_t)s_time_year, (uint8_t)s_time_month, (uint8_t)s_time_day,
                          (uint8_t)s_time_hour, (uint8_t)s_time_min, (uint8_t)s_time_sec);
}

/* 备用电池充电开关：直接写 SD3078 充电寄存器（限流电阻用配置宏 SD3078_CHARGE_RES_SEL） */
static void rtc_charge_apply(menu_item_t *it)
{
    (void)it;
    if (SD3078_IsInitialized()) {
        SD3078_ChargeSet(s_rtc_charge ? 1U : 0U, SD3078_CHARGE_RES_SEL);
    }
}

static const menu_item_t menu_items_time[] = {
    MENU_ITEM_BACK_("Return"),
    /* 时间设置：秒→分→时→日→月→年（编辑后整组写回 SD3078，由 load_task 提交） */
    MENU_ITEM_VALUE_("Sec",   &s_time_sec,   0, 59, 1, NULL, time_apply),
    MENU_ITEM_VALUE_("Min",   &s_time_min,   0, 59, 1, NULL, time_apply),
    MENU_ITEM_VALUE_("Hour",  &s_time_hour,  0, 23, 1, NULL, time_apply),
    MENU_ITEM_VALUE_("Day",   &s_time_day,   1, 31, 1, NULL, time_apply),
    MENU_ITEM_VALUE_("Month", &s_time_month, 1, 12, 1, NULL, time_apply),
    MENU_ITEM_VALUE_("Year",  &s_time_year,  0, 99, 1, NULL, time_apply),
    /* 备用电池充电（SD3078 VBAT 充电电路，默认开） */
    MENU_ITEM_TOGGLE_("Backup Charge", &s_rtc_charge, "On", "Off", rtc_charge_apply),
};
MENU_PAGE_("Time", menu_page_time, menu_items_time);

/* ==================== PowerBank → Protocol 子页（全部协议使能/失能） ==================== */
/* 有单独输入/输出方向的协议拆成两个开关（AFC/SCP/VOOC/UFCS/PD）；
 * 仅单方向的（QC/FCP/PE/SFCP/SVOOC）不加后缀；PPS 只提供 PPS1/PPS3（PPS0/PPS2 始终不使用） */
static uint8_t s_proto_pd_out = 1, s_proto_pd_in = 1;       /* PD source/sink */
static uint8_t s_proto_pps1 = 1, s_proto_pps3 = 1;          /* PPS1/PPS3 */
static uint8_t s_proto_qc = 1;                              /* QC（仅 source） */
static uint8_t s_proto_fcp = 1;                             /* FCP（仅 source） */
static uint8_t s_proto_afc_out = 1, s_proto_afc_in = 1;     /* AFC source/sink */
static uint8_t s_proto_scp_out = 1, s_proto_scp_in = 1;     /* SCP source/sink */
static uint8_t s_proto_pe = 1;                              /* PE（仅 source） */
static uint8_t s_proto_sfcp = 1;                            /* SFCP（仅 source） */
static uint8_t s_proto_vooc_out = 1, s_proto_vooc_in = 1;   /* VOOC source/sink */
static uint8_t s_proto_svooc = 1;                           /* SVOOC（仅 source） */
static uint8_t s_proto_ufcs_out = 1, s_proto_ufcs_in = 1;   /* UFCS source/sink */

/* 任一协议开关翻转后：按当前 s_proto_* 状态整组写回 SW6306 */
static void proto_apply(menu_item_t *it)
{
    (void)it;
    if (!SW6306_IsInitialized()) {
        return;
    }
    SW6306_ProtocolEnable(SW6306_PROTO_PD,    SW6306_PROTO_DIR_SOURCE, s_proto_pd_out);
    SW6306_ProtocolEnable(SW6306_PROTO_PD,    SW6306_PROTO_DIR_SINK,   s_proto_pd_in);
    SW6306_PPSEnable(SW6306_PPS_1, s_proto_pps1);
    SW6306_PPSEnable(SW6306_PPS_3, s_proto_pps3);
    SW6306_ProtocolEnable(SW6306_PROTO_QC,    SW6306_PROTO_DIR_SOURCE, s_proto_qc);
    SW6306_ProtocolEnable(SW6306_PROTO_FCP,   SW6306_PROTO_DIR_SOURCE, s_proto_fcp);
    SW6306_ProtocolEnable(SW6306_PROTO_AFC,   SW6306_PROTO_DIR_SOURCE, s_proto_afc_out);
    SW6306_ProtocolEnable(SW6306_PROTO_AFC,   SW6306_PROTO_DIR_SINK,   s_proto_afc_in);
    SW6306_ProtocolEnable(SW6306_PROTO_SCP,   SW6306_PROTO_DIR_SOURCE, s_proto_scp_out);
    SW6306_ProtocolEnable(SW6306_PROTO_SCP,   SW6306_PROTO_DIR_SINK,   s_proto_scp_in);
    SW6306_ProtocolEnable(SW6306_PROTO_PE,    SW6306_PROTO_DIR_SOURCE, s_proto_pe);
    SW6306_ProtocolEnable(SW6306_PROTO_SFCP,  SW6306_PROTO_DIR_SOURCE, s_proto_sfcp);
    SW6306_ProtocolEnable(SW6306_PROTO_VOOC,  SW6306_PROTO_DIR_SOURCE, s_proto_vooc_out);
    SW6306_ProtocolEnable(SW6306_PROTO_VOOC,  SW6306_PROTO_DIR_SINK,   s_proto_vooc_in);
    SW6306_ProtocolEnable(SW6306_PROTO_SVOOC, SW6306_PROTO_DIR_SOURCE, s_proto_svooc);
    SW6306_ProtocolEnable(SW6306_PROTO_UFCS,  SW6306_PROTO_DIR_SOURCE, s_proto_ufcs_out);
    SW6306_ProtocolEnable(SW6306_PROTO_UFCS,  SW6306_PROTO_DIR_SINK,   s_proto_ufcs_in);
}

/* 手动触发 PD/PPS 电流能力播发（Source Capability 重播，使已连接对端重新协商） */
static void pps_broadcast_apply(menu_item_t *it)
{
    (void)it;
    if (SW6306_IsInitialized()) {
        SW6306_PPSBroadcast();
    }
}

/* 手动触发 UFCS 电流能力播发 */
static void ufcs_broadcast_apply(menu_item_t *it)
{
    (void)it;
    if (SW6306_IsInitialized()) {
        SW6306_UFCSBroadcast();
    }
}

static const menu_item_t menu_items_protocol[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_TOGGLE_("PD out",  &s_proto_pd_out,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PD in",   &s_proto_pd_in,   "On", "Off", proto_apply),
    MENU_ITEM_ACTION_("PPS Broadcast", pps_broadcast_apply),   /* PPS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("PPS1",    &s_proto_pps1,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PPS3",    &s_proto_pps3,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("QC",      &s_proto_qc,      "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("FCP",     &s_proto_fcp,     "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("AFC out", &s_proto_afc_out, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("AFC in",  &s_proto_afc_in,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SCP out", &s_proto_scp_out, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SCP in",  &s_proto_scp_in,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PE",      &s_proto_pe,      "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SFCP",    &s_proto_sfcp,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("VOOC out",&s_proto_vooc_out,"On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("VOOC in", &s_proto_vooc_in, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SVOOC",   &s_proto_svooc,   "On", "Off", proto_apply),
    MENU_ITEM_ACTION_("UFCS Broadcast", ufcs_broadcast_apply), /* UFCS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("UFCS out",&s_proto_ufcs_out,"On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("UFCS in", &s_proto_ufcs_in, "On", "Off", proto_apply),
};
MENU_PAGE_("Protocol", menu_page_protocol, menu_items_protocol);

/* ==================== PowerBank → PowerLimit 子页（输入/输出功率） ==================== */
static int32_t s_out_power = 45;    /* 输出功率 5~55W，默认 45 */
static int32_t s_in_power = 30;     /* 输入功率 5~36W，默认参考 SW6306_INPUT_POWER_MAX */

static void out_power_apply(menu_item_t *it)
{
    (void)it;
    SW6306_SetMaxOutputPower((uint8_t)s_out_power);
}

static void in_power_apply(menu_item_t *it)
{
    (void)it;
    SW6306_SetMaxInputPower((uint8_t)s_in_power);
}

static const menu_item_t menu_items_powerlimit[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_VALUE_("Output", &s_out_power, 5, 55, 5, "W", out_power_apply),
    MENU_ITEM_VALUE_("Input",  &s_in_power,  5, 36, 1, "W", in_power_apply),
};
MENU_PAGE_("PowerLimit", menu_page_powerlimit, menu_items_powerlimit);

/* ==================== PowerBank → Battery 子页（容量学习 / 健康度参考） ==================== */
static uint8_t s_learn_enable = 0;   /* 容量学习武装开关：默认关 */

/* 容量学习武装开关：开→LEARNEN 使能 + 清历史完成标志（实际在 UVLO 后重新充电时启动）；
 * 关→关闭 LEARNEN。 */
static void learn_apply(menu_item_t *it)
{
    (void)it;
    if (SW6306_IsInitialized()) {
        SW6306_CapacityLearningSet(s_learn_enable ? 1U : 0U);
    }
}

/* 记录当前库仑计最大能量作为 SOH（健康度）参考容量（健康度分母 = 当前实测最大能量 → 显示约 100%） */
static void record_soh_apply(menu_item_t *it)
{
    (void)it;
    if (SW6306_IsInitialized()) {
        s_batt_init_energy = (int32_t)SW6306_ReadMaxEnergy_mWh();
    }
}

static const menu_item_t menu_items_battery_set[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_TOGGLE_("Learn Waiting", &s_learn_enable, "On", "Off", learn_apply),
    MENU_ITEM_ACTION_("Record SOH",    record_soh_apply),
};
MENU_PAGE_("Battery", menu_page_battery_set, menu_items_battery_set);

/* ==================== 充电宝页（PowerBank，套娃三个子页） ==================== */
static const menu_item_t menu_items_powerbank[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("Protocol",   &menu_page_protocol),
    MENU_ITEM_PAGE_("PowerLimit", &menu_page_powerlimit),
    MENU_ITEM_PAGE_("Battery",    &menu_page_battery_set),
};
MENU_PAGE_("PowerBank", menu_page_powerbank, menu_items_powerbank);

/* ==================== 复位页（Reset，真实触发系统复位；动作函数在 functions 模块） ==================== */
static const menu_item_t menu_items_reset[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ACTION_("Reset Now", action_reset_now),
};
MENU_PAGE_("Reset", menu_page_reset, menu_items_reset);

/* ==================== 设置页（二级，套娃引用上面的子页） ==================== */
static const menu_item_t menu_items_settings[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("PowerBank", &menu_page_powerbank),
    MENU_ITEM_PAGE_("Display",   &menu_page_display),
    MENU_ITEM_PAGE_("Time",      &menu_page_time),
    MENU_ITEM_TOGGLE_("Language", &s_lang, "中文", "English", lang_apply),
    MENU_ITEM_PAGE_("Reset",     &menu_page_reset),
};
MENU_PAGE_("Settings", menu_page_settings, menu_items_settings);

/* ==================== 状态页（含 Battery / Accelerator / Timer 子页） ==================== */

static char menu_status_maxcap[24];      /* 库仑计最大能量 */
static char menu_status_presentcap[24];  /* 库仑计当前能量 */
static char menu_status_health[24];      /* 健康度 */
static char menu_status_learn[24];       /* 容量学习状态 */
static char menu_status_accel_x[20], menu_status_accel_y[20], menu_status_accel_z[20];
static char menu_status_time[16];        /* 时分秒 */
static char menu_status_date[16];        /* 年月日 */
static char menu_status_temp[16];        /* 温度 */
static char menu_status_vbackup[20];     /* 备用电池电压（Vbackup） */

static const menu_item_t menu_items_status_battery[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_(menu_status_bat),       /* 电池电压/电流（无前缀） */
    MENU_ITEM_INFO_(menu_status_maxcap),    /* 库仑计最大能量 */
    MENU_ITEM_INFO_(menu_status_presentcap),/* 库仑计当前能量 */
    MENU_ITEM_INFO_(menu_status_health),    /* 健康度 */
    MENU_ITEM_INFO_(menu_status_learn),     /* 容量学习状态 */
};
MENU_PAGE_("Battery", menu_page_status_battery, menu_items_status_battery);

static const menu_item_t menu_items_status_accel[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_(menu_status_accel_x),
    MENU_ITEM_INFO_(menu_status_accel_y),
    MENU_ITEM_INFO_(menu_status_accel_z),
};
MENU_PAGE_("Accelerator", menu_page_status_accel, menu_items_status_accel);

static const menu_item_t menu_items_status_timer[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_(menu_status_time),
    MENU_ITEM_INFO_(menu_status_date),
    MENU_ITEM_INFO_(menu_status_temp),
    MENU_ITEM_INFO_(menu_status_vbackup),   /* 备用电池电压（Vbackup） */
};
MENU_PAGE_("Timer", menu_page_status_timer, menu_items_status_timer);

static const menu_item_t menu_items_status[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("Battery", &menu_page_status_battery),
    MENU_ITEM_PAGE_("Accelerator", &menu_page_status_accel),
    MENU_ITEM_PAGE_("Timer", &menu_page_status_timer),
};
MENU_PAGE_("Status", menu_page_status, menu_items_status);

/* 刷新状态页全部子页缓冲（ui_task 定时调用）。
 * 对未初始化的芯片输出占位符，避免 I2C 空访问（SD3078/SC7A20 未初始化时勿读）。 */
void menu_status_refresh(void)
{
    /* ---- Battery：电压/电流、最大容量、当前容量、健康度、学习状态 ---- */
    if (SW6306_IsInitialized()) {
        /* 容量/库仑计镜像由 SW6306_task 周期更新（CapacityLoad），UI 只读镜像，勿在此 load */
        snprintf(menu_status_bat, sizeof(menu_status_bat), menu_tr("status.bat"),
                 SW6306_ReadVBAT() * 0.001f, SW6306_ReadIBAT() * 0.001f);
        snprintf(menu_status_maxcap, sizeof(menu_status_maxcap), menu_tr("status.maxcap"),
                 SW6306_ReadMaxEnergy_mWh() / 1000.0f);
        snprintf(menu_status_presentcap, sizeof(menu_status_presentcap), menu_tr("status.now"),
                 SW6306_ReadRemainEnergy_mWh() / 1000.0f);
        if (s_batt_init_energy > 0) {
            /* 健康度 = 最大能量 / 电池初始能量 × 100% */
            snprintf(menu_status_health, sizeof(menu_status_health), menu_tr("status.health"),
                     SW6306_ReadMaxEnergy_mWh() / (float)s_batt_init_energy * 100.0f);
        } else {
            snprintf(menu_status_health, sizeof(menu_status_health), "--");
        }
        /* 容量学习状态：0xA2 两位（bit5=END 高位 / bit6=ING 低位）→ 3 态 + Unknown */
        {
            sw6306_learn_state_t ls = SW6306_ReadLearnState();
            const char *st;
            switch (ls) {
            case SW6306_LEARN_ST_WAITING: st = menu_tr("learn.waiting"); break;
            case SW6306_LEARN_ST_ING:     st = menu_tr("learn.ing");     break;
            case SW6306_LEARN_ST_DONE:    st = menu_tr("learn.done");    break;
            default:                      st = menu_tr("learn.unknown"); break;
            }
            snprintf(menu_status_learn, sizeof(menu_status_learn), menu_tr("status.learn"), st);
        }
    } else {
        snprintf(menu_status_bat,       sizeof(menu_status_bat),       "--");
        snprintf(menu_status_maxcap,    sizeof(menu_status_maxcap),    "--");
        snprintf(menu_status_presentcap,sizeof(menu_status_presentcap),"--");
        snprintf(menu_status_health,    sizeof(menu_status_health),    "--");
        snprintf(menu_status_learn,     sizeof(menu_status_learn),     "--");
    }

    /* ---- Accelerator：三轴加速度（读 SC7A20 驱动句柄镜像，mg → g） ---- */
    if (SC7A20_IsInitialized()) {
        snprintf(menu_status_accel_x, sizeof(menu_status_accel_x), menu_tr("status.accel_x"),
                 SC7A20_ReadX_mg() / 1000.0f);
        snprintf(menu_status_accel_y, sizeof(menu_status_accel_y), menu_tr("status.accel_y"),
                 SC7A20_ReadY_mg() / 1000.0f);
        snprintf(menu_status_accel_z, sizeof(menu_status_accel_z), menu_tr("status.accel_z"),
                 SC7A20_ReadZ_mg() / 1000.0f);
    } else {
        snprintf(menu_status_accel_x, sizeof(menu_status_accel_x), "X: --");
        snprintf(menu_status_accel_y, sizeof(menu_status_accel_y), "Y: --");
        snprintf(menu_status_accel_z, sizeof(menu_status_accel_z), "Z: --");
    }

    /* ---- Timer：时分秒、年月日、温度、备用电池电压（读 SD3078 驱动句柄镜像） ---- */
    if (SD3078_IsInitialized()) {
        snprintf(menu_status_time, sizeof(menu_status_time), menu_tr("status.time"),
                 SD3078_ReadHour(), SD3078_ReadMin(), SD3078_ReadSec());
        snprintf(menu_status_date, sizeof(menu_status_date), menu_tr("status.date"),
                 SD3078_ReadYear(), SD3078_ReadMonth(), SD3078_ReadDay());
        snprintf(menu_status_temp, sizeof(menu_status_temp), menu_tr("status.temp"),
                 (int)SD3078_ReadTemp());
        snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), menu_tr("status.vbackup"),
                 SD3078_ReadBatt() / 1000.0f);
    } else {
        snprintf(menu_status_time, sizeof(menu_status_time), "--:--:--");
        snprintf(menu_status_date, sizeof(menu_status_date), "----");
        snprintf(menu_status_temp, sizeof(menu_status_temp), "Temp: --");
        snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), "Vbackup: --");
    }
}

/* ==================== 工具页 ==================== */
static const menu_item_t menu_items_tools[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_APP_("Screen Test", &menu_app_screen_test),   /* 全屏纯色坏区测试 */
    MENU_ITEM_INFO_("Coming Soon"),
};
MENU_PAGE_("Tools", menu_page_tools, menu_items_tools);

/* ==================== 游戏页 ==================== */
static const menu_item_t menu_items_games[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_("Coming Soon"),
};
MENU_PAGE_("Games", menu_page_games, menu_items_games);

/* ==================== 关于页 ==================== */
static const menu_item_t menu_items_about[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_("Pocket PowerBank"),
    MENU_ITEM_INFO_("FW 1.0.0"),
    MENU_ITEM_INFO_("AT32F423+LVGL9"),
};
MENU_PAGE_("About", menu_page_about, menu_items_about);

/* ==================== 根页（图标页，菜单根节点；套娃引用各子页） ==================== */
static const menu_item_t menu_items_root[] = {
    /* 根节点 Return = 主界面应用（菜单应用模型统一管理；MiaoUI 风格上电先显示主界面） */
    MENU_ITEM_APP_("Return", &menu_app_main),
    MENU_ITEM_PAGE_("Settings", &menu_page_settings),
    MENU_ITEM_PAGE_("Status",   &menu_page_status),
    MENU_ITEM_PAGE_("Tools",    &menu_page_tools),
    MENU_ITEM_PAGE_("Games",    &menu_page_games),
    MENU_ITEM_PAGE_("About",    &menu_page_about),
};

/* 根页图标（与 items 一一对应，顺序即循环顺序） */
static const menu_icon_t menu_icons_root[] = {
    { &menu_icon_return,   &menu_icon_return_small,   "Return"   },
    { &menu_icon_settings, &menu_icon_settings_small, "Settings" },
    { &menu_icon_status,   &menu_icon_status_small,   "Status"   },
    { &menu_icon_tools,    &menu_icon_tools_small,    "Tools"    },
    { &menu_icon_games,    &menu_icon_games_small,    "Games"    },
    { &menu_icon_about,    &menu_icon_about_small,    "About"    },
};
MENU_PAGE_ICON_("Menu", menu_page_root, menu_items_root, menu_icons_root);

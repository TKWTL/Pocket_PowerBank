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
 * 本文件同时保存 i18n 表：菜单树里的 label/title 就是查表用的键，放在一起才好在
 * 改文案时同步。菜单树的条目/页面声明顺序与表的注释分组一一对应。
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
/* ==================== 国际化（i18n） ====================
 * 文案策略：menu_pages.c 里的 label/title/toggle 值保持英文，同时就是字符串键；
 * menu_tr(key) 按当前语言返回显示文本，未配置的键回退 key 本身。
 * 表按【菜单顺序】排列，便于核对新增文案是否有对应条目：
 *   根图标页 → About → Status(含三个子页) → Tools → Games → Settings
 *   → Display → Time → PowerBank(Protocol/PowerLimit/Battery/SW6306) → Reset
 * 表内每一项都在菜单树或 menu_tr() 里有使用点；新增文案务必同步加条目，
 * 否则中文态会显示英文（menu_tr 的回退行为）。
 * 中文字库（14/12px 部分字符集）待全部文案确认后生成，生成前切中文缺字形。 */
static const menu_tr_t menu_tr_table[] = {
    /* ---- 根图标页 ---- */
    { "Menu",            "Menu",             "菜单" },
    { "Return",          "Return",           "返回" },
    { "Settings",        "Settings",         "设置" },
    { "Status",          "Status",           "状态" },
    { "Tools",           "Tools",            "工具" },
    { "Games",           "Games",            "游戏" },
    { "About",           "About",            "关于" },
    /* ---- About 页 ---- */
    { "Pocket PowerBank","Pocket PowerBank", "口袋充电宝" },
    { "FW 1.0.0",        "FW 1.0.0",         "固件 1.0.0" },
    { "AT32F423+LVGL9",  "AT32F423+LVGL9",   "AT32F423+LVGL9" },
    /* ---- Status 页 ---- */
    { "Battery",         "Battery",          "电池" },
    { "Accelerator",     "Accelerator",      "加速度" },
    { "Timer",           "Timer",            "时钟" },
    /* Status → Battery */
    { "status.bat",      "%.2fV %.3fA",      "%.2fV %.3fA" },
    { "status.maxcap",   "Max: %.2f Wh",     "最大能量: %.2f Wh" },
    { "status.now",      "Now: %.2f Wh",     "当前能量: %.2f Wh" },
    { "status.health",   "Health: %.0f%%",   "健康度: %.0f%%" },
    { "status.cycles",   "Cycles: %.2f",     "循环: %.2f" },
    { "status.learn",    "Learn:%s",         "容量学习:%s" },
    { "learn.waiting",   "Waiting",          "等待" },
    { "learn.ing",       "Learning",         "学习中" },
    { "learn.done",      "Done",             "已完成" },
    { "learn.unknown",   "Unknown",          "未知" },
    /* Status → Accelerator */
    { "status.accel_x",  "X: %.2f g",        "X轴: %.2f g" },
    { "status.accel_y",  "Y: %.2f g",        "Y轴: %.2f g" },
    { "status.accel_z",  "Z: %.2f g",        "Z轴: %.2f g" },
    /* Status → Timer */
    { "status.time",     "%02d:%02d:%02d",   "%02d:%02d:%02d" },
    { "status.date",     "20%02d-%02d-%02d", "20%02d-%02d-%02d" },
    { "status.temp",     "Temp: %d°C",       "温度: %d°C" },
    { "status.vbackup",  "Vbackup: %.2fV",   "备用电池: %.2fV" },
    /* ---- Tools 页 ---- */
    { "Screen Test",     "Screen Test",      "屏幕测试" },
    { "Coming Soon",     "Coming Soon",      "敬请期待" },
    /* ---- Settings 页 ---- */
    { "Display",         "Display",          "显示" },
    { "Time",            "Time",             "时间" },
    { "PowerBank",       "PowerBank",        "移动电源" },
    { "Reset",           "Reset",            "复位" },
    { "Language",        "Language",         "语言" },
    /* ---- Display 页 ---- */
    { "Backlight",       "Backlight",        "背光" },
    { "Theme",           "Theme",            "主题" },
    { "Color",           "Color",            "颜色" },
    { "Auto Sleep",      "Auto Sleep",       "自动休眠" },
    { "None",            "None",             "不休眠" },
    { "Auto Flip",       "Auto Flip",        "自动翻转" },
    { "Light",           "Light",            "浅色" },
    { "Dark",            "Dark",             "深色" },
    /* ---- Time 页 ---- */
    { "Sec",             "Sec",              "秒" },
    { "Min",             "Min",              "分" },
    { "Hour",            "Hour",             "时" },
    { "Day",             "Day",              "日" },
    { "Month",           "Month",            "月" },
    { "Year",            "Year",             "年" },
    { "Backup Charge",   "Backup Charge",    "备用电池充电" },
    /* ---- PowerBank 页 ---- */
    { "Protocol",        "Protocol",         "协议" },
    { "PowerLimit",      "PowerLimit",       "功率限制" },
    { "SW6306",          "SW6306",           "SW6306" },
    /* PowerBank → Protocol */
    { "PD out",          "PD out",           "PD 输出" },
    { "PD in",           "PD in",            "PD 输入" },
    { "PPS Broadcast",   "PPS Broadcast",    "PPS 能力播发" },
    { "PPS1",            "PPS1",             "PPS1" },
    { "PPS3",            "PPS3",             "PPS3" },
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
    { "UFCS Broadcast",  "UFCS Broadcast",   "UFCS 能力播发" },
    { "UFCS out",        "UFCS out",         "UFCS 输出" },
    { "UFCS in",         "UFCS in",          "UFCS 输入" },
    /* PowerBank → PowerLimit */
    { "Output",          "Output",           "输出功率" },
    { "Input",           "Input",            "输入功率" },
    /* PowerBank → Battery */
    { "Record SOH",      "Record SOH",       "记录SOH" },
    /* PowerBank → SW6306 */
    { "Init Now",        "Init Now",         "立即初始化" },
    /* ---- Reset 页 ---- */
    { "Reset Now",       "Reset Now",        "立即复位" },
    /* ---- ENUM 取值（时间页后备电池充电模式） ---- */
    { "Auto",            "Auto",             "自动" },
    /* ---- Toggle 取值（menu_pages.c 的 on/off 文本 + menu_ui 默认值） ---- */
    { "ON",              "ON",               "开" },
    { "OFF",             "OFF",              "关" },
    { "On",              "On",               "开" },
    { "Off",             "Off",              "关" },
    { "EDIT",            "EDIT",             "编辑" },
};

/* 当前语言（渲染层状态；切语言后调用 menu_notify_changed() 重绘即生效） */
static menu_lang_t s_menu_lang = MENU_LANG_EN;

static const char *menu_tr_key(const char *key)
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

/* 设置语言（越界值忽略）。切换后由调用方 menu_notify_changed() 触发重绘。 */
void menu_lang_set(menu_lang_t lang)
{
    if (lang < MENU_LANG_EN || lang > MENU_LANG_ZH) {
        return;
    }
    s_menu_lang = lang;
}

/* 供 UI 切换语言：设置页 Language 条目调用（EN <-> ZH） */
void menu_lang_toggle(void)
{
    menu_lang_set((s_menu_lang == MENU_LANG_EN) ? MENU_LANG_ZH : MENU_LANG_EN);
}

menu_lang_t menu_lang_get(void)
{
    return s_menu_lang;
}

const char *menu_tr(const char *key)
{
    return menu_tr_key(key);
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
/* 自动翻转开关：ON=按重力方向自动 180° 翻转（默认）；OFF=固定方向（画面永远正立）。
 * 与背光一样是运行时设置，未持久化。 */
static uint8_t s_auto_flip = 1;

/* 自动休眠选项文本（元素经 menu_tr 本地化；"None" 见 menu_ui.c 的 i18n 表） */
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
    /* 语言切换只在这里发生；切完重绘即生效 */
    menu_lang_toggle();
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

/* Auto Flip：只改标志，实际旋转每帧由 ui_task 的 ui_auto_rotate() 按
 * (ON AND 重力方向) 应用，所以这里不需要直接动 GC9D01。 */
void menu_auto_flip_set(uint8_t on)
{
    s_auto_flip = (on != 0U) ? 1U : 0U;
}

uint8_t menu_auto_flip_get(void)
{
    return s_auto_flip;
}

static void auto_flip_apply(menu_item_t *it)
{
    (void)it;
    menu_auto_flip_set(s_auto_flip);
}

/* ==================== 页面前向声明（套娃：任意页面可引用任意页面，无顺序限制）
 * 注意：这里用不带 static 的试探性定义，与下方 MENU_PAGE_* 宏展开的
 * 正式定义（也不带 static）链接属性一致；链接器合并为同一份 RAM 对象。 */
menu_page_t menu_page_display, menu_page_time, menu_page_powerbank, menu_page_reset;
menu_page_t menu_page_settings;
menu_page_t menu_page_status;      /* ui_task 用 &menu_page_status 判断当前页 */
menu_page_t menu_page_status_battery, menu_page_status_accel, menu_page_status_timer;
menu_page_t menu_page_protocol, menu_page_powerlimit, menu_page_battery_set, menu_page_sw6306;
menu_page_t menu_page_tools, menu_page_games, menu_page_about;

/* ==================== 显示页（Display，三级，归拢显示相关项） ==================== */
static const menu_item_t menu_items_display[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_VALUE_("Backlight", &s_backlight, 1, 16, 1, NULL, backlight_apply),
    MENU_ITEM_TOGGLE_("Theme", &s_theme_toggle, "Light", "Dark", theme_toggle_apply),
    MENU_ITEM_VALUE_("Color", &s_theme_color_idx, 0, MENU_PALETTE_COUNT - 1, 1, NULL, theme_color_apply),
    MENU_ITEM_ENUM_("Auto Sleep", &s_sleep_idx, s_sleep_opts, 9, sleep_apply),
    /* 自动翻转：ON=跟随重力方向；OFF=固定方向。
     * 自动翻转生效时画面会倒过来，按键在视觉上左右对调，所以菜单里 PREV/NEXT
     * 会跟着互换语义（见 ui_task 的 ui_scan_action）；主界面的 MENU/NEXT 语义是
     * HOME/LED，不换。 */
    MENU_ITEM_TOGGLE_("Auto Flip", &s_auto_flip, "On", "Off", auto_flip_apply),
};
MENU_PAGE_("Display", menu_page_display, menu_items_display);

/* ==================== 时间页（Time，SD3078 时间设置 + 备用电池充电） ==================== */
static int32_t s_time_sec = 0;      /* 秒 0~59 */
static int32_t s_time_min = 0;      /* 分 0~59 */
static int32_t s_time_hour = 0;     /* 时 0~23 */
static int32_t s_time_day = 1;      /* 日 1~31 */
static int32_t s_time_month = 1;    /* 月 1~12 */
static int32_t s_time_year = 24;    /* 年 00~99（2000+） */
static int32_t s_rtc_charge_mode = SD3078_BACKUP_CHARGE_AUTO;
static const char * const s_rtc_charge_opts[] = { "Off", "On", "Auto" };

/* Time页非编辑态由 ui_task 500ms 调用：时间跟随驱动镜像，充电模式跟随算法状态。 */
void menu_time_read(void)
{
    s_time_sec   = SD3078_ReadSec();
    s_time_min   = SD3078_ReadMin();
    s_time_hour  = SD3078_ReadHour();
    s_time_day   = SD3078_ReadDay();
    s_time_month = SD3078_ReadMonth();
    s_time_year  = SD3078_ReadYear();
    s_rtc_charge_mode = (int32_t)SD3078_AlgoGetBackupChargeMode();
}

/* 菜单只产生 RAM 请求；RTC 的实时合并与 I2C 提交都在 sd3078_algo/load_task。 */
static void time_apply(menu_item_t *it)
{
    if (it && it->value_ptr == &s_time_sec) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_SEC, (uint8_t)s_time_sec);
    } else if (it && it->value_ptr == &s_time_min) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_MIN, (uint8_t)s_time_min);
    } else if (it && it->value_ptr == &s_time_hour) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_HOUR, (uint8_t)s_time_hour);
    } else if (it && it->value_ptr == &s_time_day) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_DAY, (uint8_t)s_time_day);
    } else if (it && it->value_ptr == &s_time_month) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_MONTH, (uint8_t)s_time_month);
    } else if (it && it->value_ptr == &s_time_year) {
        (void)SD3078_AlgoRequestTimeFieldSet(SD3078_TIME_FIELD_YEAR, (uint8_t)s_time_year);
    }
}

static void rtc_charge_apply(menu_item_t *it)
{
    (void)it;
    if (SD3078_AlgoSetBackupChargeMode(
            (sd3078_backup_charge_mode_t)s_rtc_charge_mode) != I2C_OK) {
        s_rtc_charge_mode = (int32_t)SD3078_AlgoGetBackupChargeMode();
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
    /* MS621FE 后备电池：Off / On / Auto（Auto=2.95V启充、3.10V停充） */
    MENU_ITEM_ENUM_("Backup Charge", &s_rtc_charge_mode, s_rtc_charge_opts, 3, rtc_charge_apply),
};
MENU_PAGE_("Time", menu_page_time, menu_items_time);

/* ==================== PowerBank → Protocol 子页 ====================
 * 配置真实状态放在 sw6306_algo；菜单只修改配置镜像并发 request。 */
static void proto_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestProtocolApply();
}

static void pps_broadcast_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestPPSBroadcast();
}

static void ufcs_broadcast_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestUFCSBroadcast();
}

static const menu_item_t menu_items_protocol[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_TOGGLE_("PD out",  &SW6306_AlgoConfig.pd_out,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PD in",   &SW6306_AlgoConfig.pd_in,   "On", "Off", proto_apply),
    MENU_ITEM_ACTION_("PPS Broadcast", pps_broadcast_apply),   /* PPS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("PPS1",    &SW6306_AlgoConfig.pps1,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PPS3",    &SW6306_AlgoConfig.pps3,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("QC",      &SW6306_AlgoConfig.qc,      "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("FCP",     &SW6306_AlgoConfig.fcp,     "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("AFC out", &SW6306_AlgoConfig.afc_out, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("AFC in",  &SW6306_AlgoConfig.afc_in,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SCP out", &SW6306_AlgoConfig.scp_out, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SCP in",  &SW6306_AlgoConfig.scp_in,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PE",      &SW6306_AlgoConfig.pe,      "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SFCP",    &SW6306_AlgoConfig.sfcp,    "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("VOOC out",&SW6306_AlgoConfig.vooc_out,"On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("VOOC in", &SW6306_AlgoConfig.vooc_in, "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("SVOOC",   &SW6306_AlgoConfig.svooc,   "On", "Off", proto_apply),
    MENU_ITEM_ACTION_("UFCS Broadcast", ufcs_broadcast_apply), /* UFCS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("UFCS out",&SW6306_AlgoConfig.ufcs_out,"On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("UFCS in", &SW6306_AlgoConfig.ufcs_in, "On", "Off", proto_apply),
};
MENU_PAGE_("Protocol", menu_page_protocol, menu_items_protocol);

/* ==================== PowerBank → PowerLimit 子页（输入/输出功率） ==================== */
static void power_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestPowerApply();
}

static const menu_item_t menu_items_powerlimit[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_VALUE_("Output", &SW6306_AlgoConfig.output_power_w, 5, 55, 5, "W", power_apply),
    MENU_ITEM_VALUE_("Input",  &SW6306_AlgoConfig.input_power_w,  5, 36, 1, "W", power_apply),
};
MENU_PAGE_("PowerLimit", menu_page_powerlimit, menu_items_powerlimit);

/* ==================== PowerBank → Battery 子页（SOH 出厂基准） ====================
 * 容量学习由 sw6306_algo 固定常开并自动重新武装，不再提供用户开关。 */
static void record_soh_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestRecordFactoryCapacity();
}

static const menu_item_t menu_items_battery_set[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ACTION_("Record SOH", record_soh_apply),
};
MENU_PAGE_("Battery", menu_page_battery_set, menu_items_battery_set);

/* ==================== PowerBank → SW6306 子页（手动重新初始化） ====================
 * 菜单只发 RAM request；SW6306_task 负责失效 session、重初始化硬件，
 * 成功后 sw6306_algo 会重新应用当前协议/功率，并确保容量学习保持常开。 */
static void sw6306_reinit_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestReinit();
}

static const menu_item_t menu_items_sw6306[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ACTION_("Init Now", sw6306_reinit_apply),
};
MENU_PAGE_("SW6306", menu_page_sw6306, menu_items_sw6306);

/* ==================== 充电宝页（PowerBank，套娃四个子页） ==================== */
static const menu_item_t menu_items_powerbank[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("Protocol",   &menu_page_protocol),
    MENU_ITEM_PAGE_("PowerLimit", &menu_page_powerlimit),
    MENU_ITEM_PAGE_("Battery",    &menu_page_battery_set),
    MENU_ITEM_PAGE_("SW6306",     &menu_page_sw6306),
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
    MENU_ITEM_ACTION_("Language", lang_apply),   /* 单击即在中/英之间切换 */
    MENU_ITEM_PAGE_("Reset",     &menu_page_reset),
};
MENU_PAGE_("Settings", menu_page_settings, menu_items_settings);

/* ==================== 状态页（含 Battery / Accelerator / Timer 子页） ==================== */

static char menu_status_maxcap[24];      /* 库仑计最大能量 */
static char menu_status_presentcap[24];  /* 库仑计当前能量 */
static char menu_status_health[24];      /* 健康度 */
static char menu_status_cycles[24];      /* 等效完整循环 EFC */
static char menu_status_learn[24];       /* 容量学习状态 */
static char menu_status_accel_x[20], menu_status_accel_y[20], menu_status_accel_z[20];
static char menu_status_time[16];        /* 时分秒 */
static char menu_status_date[16];        /* 年月日 */
static char menu_status_temp[16];        /* 温度 */
static char menu_status_vbackup[20];     /* 备用电池电压（Vbackup） */
static char menu_status_uid[20];         /* SD3078 UID（16 位十六进制字符，无前缀后缀） */

static const menu_item_t menu_items_status_battery[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_(menu_status_bat),       /* 电池电压/电流（无前缀） */
    MENU_ITEM_INFO_(menu_status_maxcap),    /* 库仑计最大能量 */
    MENU_ITEM_INFO_(menu_status_presentcap),/* 库仑计当前能量 */
    MENU_ITEM_INFO_(menu_status_health),    /* 健康度 */
    MENU_ITEM_INFO_(menu_status_cycles),    /* 等效完整循环 */
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
    MENU_ITEM_INFO_(menu_status_uid),       /* SD3078 UID（末项） */
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
 * 全部只读驱动/算法RAM镜像，不在UI线程发起I2C。 */
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
        if (nvm_is_valid()) {
            snprintf(menu_status_health, sizeof(menu_status_health), menu_tr("status.health"),
                     SW6306_AlgoGetSOHPercent());
            snprintf(menu_status_cycles, sizeof(menu_status_cycles), menu_tr("status.cycles"),
                     SW6306_AlgoGetEquivalentCycles());
        } else {
            snprintf(menu_status_health, sizeof(menu_status_health), "--");
            snprintf(menu_status_cycles, sizeof(menu_status_cycles), "--");
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
        snprintf(menu_status_cycles,    sizeof(menu_status_cycles),    "--");
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

    /* ---- Timer：时分秒、年月日、温度、备用电池电压、UID（读 SD3078 驱动句柄镜像） ---- */
    if (SD3078_ReadMonth() >= 1U && SD3078_ReadMonth() <= 12U &&
        SD3078_ReadDay() >= 1U && SD3078_ReadDay() <= 31U &&
        SD3078_ReadHour() <= 23U && SD3078_ReadMin() <= 59U && SD3078_ReadSec() <= 59U) {
        snprintf(menu_status_time, sizeof(menu_status_time), menu_tr("status.time"),
                 SD3078_ReadHour(), SD3078_ReadMin(), SD3078_ReadSec());
        snprintf(menu_status_date, sizeof(menu_status_date), menu_tr("status.date"),
                 SD3078_ReadYear(), SD3078_ReadMonth(), SD3078_ReadDay());
        snprintf(menu_status_temp, sizeof(menu_status_temp), menu_tr("status.temp"),
                 (int)SD3078_ReadTemp());
        snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), menu_tr("status.vbackup"),
                 SD3078_ReadBatt() / 1000.0f);
        snprintf(menu_status_uid, sizeof(menu_status_uid), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 SD3078_ReadID(0), SD3078_ReadID(1), SD3078_ReadID(2), SD3078_ReadID(3),
                 SD3078_ReadID(4), SD3078_ReadID(5), SD3078_ReadID(6), SD3078_ReadID(7));
    } else {
        snprintf(menu_status_time, sizeof(menu_status_time), "--:--:--");
        snprintf(menu_status_date, sizeof(menu_status_date), "----");
        snprintf(menu_status_temp, sizeof(menu_status_temp), "Temp: --");
        snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), "Vbackup: --");
        snprintf(menu_status_uid, sizeof(menu_status_uid), "----------------");
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

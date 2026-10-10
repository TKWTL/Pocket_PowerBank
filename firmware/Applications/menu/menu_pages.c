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
 * API 速查（精确签名见 menu.h）：
 *   MENU_ITEM_BACK_("Return")         返回父菜单；
 *   MENU_ITEM_PAGE_(label, &page)     进入子页；
 *   MENU_ITEM_ACTION_(label, fn)      调用 fn(menu_item_t*)；
 *   MENU_ITEM_INFO_(ram_text)         显示由菜单刷新函数填写的字符串；
 *   MENU_ITEM_TOGGLE_/VALUE_/ENUM_    修改变量并调用应用回调；
 *   MENU_ITEM_APP_(label, &app)       进入 create/activate/run/destroy 全屏应用；
 *   MENU_PAGE_(title, page, items)    文本菜单；
 *   MENU_PAGE_ICON_(...)              带正常/选中两套图标的根菜单；
 *   MENU_WORD_INFO_(page, lines)      3行滚动，CONF返回；
 *   MENU_WORD_CONFIRM_(page,lines,fn) 阅读到末屏CONF执行动作；
 *   MENU_WORD_ACTION_(page,lines,hook) 进入/TICK/退出回调。
 * 新建页面：先在“页面前向声明”处声明对象，再用相应宏定义并由父页引用。
 * WORD 英文逻辑行不超过20字符；菜单动作只提交RAM请求，禁止在UI任务中操作I2C。
 * 新增工具应用需实现 menu_app_t 并在 Keil Functions group 登记 .c 文件。
 * 本文件同时保存 i18n 表：菜单树里的 label/title 就是查表用的键，放在一起才好在
 * 改文案时同步。菜单树的条目/页面声明顺序与表的注释分组一一对应。
 */
#include <string.h>
#include "mini_format.h"
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
 * 文案策略：英文文本本身就是 key；表中只保存 key + 中文，英文态不查表。
 * 中英文完全相同的条目也无需入表，未配置时直接回退英文 key。
 * 表按【菜单顺序】排列，便于核对新增文案是否有对应条目：
 *   根图标页 → About → Status(Battery/Accelerator/Timer) → Tools → Games
 *   → Settings(PowerBank/Display/Sleep & Wake/System)
 *   → PowerBank(Protocol#1/Protocol#2/PowerLimit)
 *   → Display → Sleep & Wake → System(Clock/Language/Factory Option/Reset) → toggle/ENUM 取值
 * 表中仅保留需要中文替换的文案；中英文相同的字符串不入表，
 * 否则中文态会显示英文（menu_tr 的回退行为）。
 * 中文字库（14/12px 部分字符集）待全部文案确认后生成，生成前切中文缺字形。 */
static const menu_tr_t menu_tr_table[] = {
    /* ---- 根图标页 ---- */
    { "Menu", "菜单" },
    { "Return", "返回" },
    { "Settings", "设置" },
    { "Status", "状态" },
    { "Tools", "工具" },
    { "Games", "游戏" },
    { "About", "关于" },
    /* ---- About WORD页 ---- */
    { "Pocket PowerBank", "口袋充电宝" },
    { "Firmware 1.2.0", "固件 1.2.0" },
    { "Open Source Hardware", "开源硬件" },
    { "Author: TKWTL", "作者: TKWTL" },
    /* ---- Status 页 ---- */
    { "Battery", "电池" },
    { "Accelerator", "加速度" },
    { "Timer", "时钟" },
    /* Status → Battery */
    { "Max: %lu.%02lu Wh", "最大能量: %lu.%02lu Wh" },
    { "Now: %lu.%02lu Wh", "当前能量: %lu.%02lu Wh" },
    { "Health: %lu%%", "健康度: %lu%%" },
    { "Cycles: %lu.%02lu", "循环: %lu.%02lu" },
    { "Learn:%s", "容量学习:%s" },
    { "Waiting", "等待" },
    { "Idle", "空闲" },
    { "Learning", "学习中" },
    { "Done", "已完成" },
    { "Unknown", "未知" },
    /* Status → Accelerator */
    { "X: %s%lu.%02lu g", "X轴: %s%lu.%02lu g" },
    { "Y: %s%lu.%02lu g", "Y轴: %s%lu.%02lu g" },
    { "Z: %s%lu.%02lu g", "Z轴: %s%lu.%02lu g" },
    /* Status → Timer */
    { "Temp: %d°C", "温度: %d°C" },
    { "Vbackup: %lu.%02luV", "备用电池: %lu.%02luV" },
    /* ---- Tools 页 ---- */
    { "Leveler", "水平仪" },
    { "SOS Blink", "SOS 闪灯" },
    { "Screen Test", "屏幕测试" },
    { "Emergency Light", "紧急闪灯" },
    { "Max Bright Pulses", "最高亮度脉冲" },
    { "100ms Dot / 5s+", "100ms点 / 5秒以上" },
    { "Function Not Ready", "功能尚未完成" },
    { "Cycles Solid Colors", "循环显示纯色" },
    { "Check Pixel Defects", "检查坏点" },
    { "Gravity Calibrate", "重力校准" },
    { "Auto Detect 6 Sides", "自动识别六个方向" },
    { "Hold Each Side 2 sec", "每个方向静置 2 秒" },
    { "UP/DOWN/LEFT/RIGHT", "上/下/左/右依次校准" },
    { "Then Face Up/Down", "再将屏幕朝上/朝下" },
    { "Any Key Exits Test", "中途任意键退出" },
    { "Press NEXT to Exit", "按 NEXT 退出" },
    { "Press CONF to Exit", "按 CONF 退出" },
    { "Press CONF to Start", "按 CONF 开始" },
    /* ---- Games 页 ---- */
    { "Coming Soon", "敬请期待" },
    /* ---- Settings 页（四个子页） ---- */
    { "PowerBank", "移动电源" },
    { "Display", "显示" },
    { "Sleep & Wake", "休眠与唤醒" },
    { "System", "系统" },
    /* Settings → PowerBank */
    { "Protocol#1", "协议#1" },
    { "Protocol#2", "协议#2" },
    { "PowerLimit", "功率限制" },
    /* PowerBank → Protocol#1（PD / PPS / UFCS） */
    { "PD out", "PD 输出" },
    { "PD in", "PD 输入" },
    { "PPS Broadcast", "PPS 能力播发" },
    { "UFCS Broadcast", "UFCS 能力播发" },
    { "UFCS out", "UFCS 输出" },
    { "UFCS in", "UFCS 输入" },
    /* PowerBank → Protocol#2（其它快充协议） */
    { "AFC out", "AFC 输出" },
    { "AFC in", "AFC 输入" },
    { "SCP out", "SCP 输出" },
    { "SCP in", "SCP 输入" },
    { "VOOC out", "VOOC 输出" },
    { "VOOC in", "VOOC 输入" },
    /* PowerBank → PowerLimit */
    { "Output", "输出功率" },
    { "Input", "输入功率" },
    /* ---- Display 页 ---- */
    { "Backlight", "背光" },
    { "Theme", "主题" },
    { "Color", "颜色" },
    { "Auto Flip", "自动翻转" },
    { "Light", "浅色" },
    { "Dark", "深色" },
    /* ---- Sleep & Wake 页 ---- */
    { "Auto Sleep", "自动休眠" },
    { "None", "不休眠" },
    { "Pickup Wake", "抬起唤醒" },
    { "Motion Wake", "运动唤醒" },
    { "Wake Home After", "唤醒回主页" },
    { "1 min", "1 分钟" },
    { "5 min", "5 分钟" },
    { "15 min", "15 分钟" },
    { "30 min", "30 分钟" },
    /* ---- System 页 ---- */
    { "Clock", "时钟" },
    /* Clock 子页的页面标题仍是 "Time"（菜单项标签改叫 Clock，页面标题未改） */
    { "Time", "时间" },
    { "Language", "语言" },
    { "Factory Unlock", "出厂解锁" },
    { "Factory Option", "出厂选项" },
    { "Factory Locked", "出厂选项已锁定" },
    { "Use Factory Unlock", "请先进行出厂解锁" },
    { "Enter Password", "输入密码" },
    { "Waiting...", "等待输入..." },
    { "Unlocked", "已解锁" },
    { "Wrong Password", "密码错误" },
    { "Transport Mode", "运输模式" },
    { "Minimum Power State", "最低功耗状态" },
    { "Wake: Press PWR/NEXT", "按 PWR/NEXT 唤醒" },
    { "System Will Reset", "系统将复位" },
    { "RAM Settings Lost", "运行时设置会丢失" },
    { "Press CONF to Enter", "按 CONF 进入" },
    { "Reset", "复位" },
    /* System → Clock（原 Settings→Time：SD3078 时间设置 + 后备电池充电） */
    { "Sec", "秒" },
    { "Min", "分" },
    { "Hour", "时" },
    { "Day", "日" },
    { "Month", "月" },
    { "Year", "年" },
    { "Backup Charge", "备用电池充电" },
    /* System → Factory Option */
    { "Cycle Clear", "循环清零" },
    { "Record SOH", "记录SOH" },
    { "RBAT Calibrate", "RBAT 校准" },
    /* WORD确认页公共提示 + Reset */
    { "Reset System?", "复位系统？" },
    { "Settings Will Reset", "所有设置将恢复默认" },
    { "Factory Data Stays", "保留出厂数据" },
    { "Press CONF to Reset", "按 CONF 复位" },
    /* ---- ENUM 取值（Clock 页后备电池充电模式） ---- */
    { "Auto", "自动" },
    /* ---- Toggle 取值（menu_pages.c 的 on/off 文本 + menu_ui 默认值） ---- */
    { "ON", "开" },
    { "OFF", "关" },
    { "On", "开" },
    { "Off", "关" },
    { "EDIT", "编辑" },
};

/* 当前语言（渲染层状态；切语言后调用 menu_notify_changed() 重绘即生效） */
static menu_lang_t s_menu_lang = MENU_LANG_EN;

static const char *menu_tr_key(const char *key)
{
    unsigned i;

    if (!key || s_menu_lang == MENU_LANG_EN) {
        return key;   /* key 本身就是英文，英文态无需查表 */
    }
    for (i = 0; i < sizeof(menu_tr_table) / sizeof(menu_tr_table[0]); i++) {
        if (strcmp(menu_tr_table[i].key, key) == 0) {
            return menu_tr_table[i].zh;
        }
    }
    return key;       /* 未配置或中英文相同：直接使用英文 key */
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
static int32_t s_wake_home_idx = 2;         /* 唤醒回主页：默认 DeepSleep >=5min */
/* 自动翻转开关：ON=按重力方向自动 180° 翻转；OFF=固定方向（画面永远正立，默认）。
 * 与背光一样是运行时设置，未持久化。 */
static uint8_t s_auto_flip = 0;

/* Pickup/Motion Wake 开关状态直接存放在 SC7A20_AlgoConfig 中：
 * 菜单只改RAM变量，具体休眠时保留10Hz还是Power-down由SC7A20算法PM回调决定。 */

/* 占位动作：用于"界面先立起来、功能待接入"的条目（Sleep & Wake / System 里若干项）。
 * 用 ACTION 而非 INFO，是为了保留"可执行条目"的形态与位置；功能接入时把对应
 * 函数体填上即可，菜单结构不用再动。 */
static void placeholder_apply(menu_item_t *it)
{
    (void)it;
}

/* 自动休眠选项文本（元素经 menu_tr 本地化；"None" 见 menu_ui.c 的 i18n 表） */
static const char * const s_sleep_opts[] = {
    "None", "5s", "10s", "15s", "30s", "60s", "120s", "300s", "600s",
};
/* 与 s_sleep_opts 一一对应：实际超时秒数（0=永久不休眠） */
static const int s_sleep_opts_sec[] = { 0, 5, 10, 15, 30, 60, 120, 300, 600 };

static const char * const s_wake_home_opts[] = {
    "Off", "1 min", "5 min", "15 min", "30 min",
};
static const uint16_t s_wake_home_sec[] = { 0U, 60U, 300U, 900U, 1800U };

uint32_t menu_wake_home_after_sec(void)
{
    if (s_wake_home_idx < 0 ||
        s_wake_home_idx >= (int32_t)(sizeof(s_wake_home_sec) / sizeof(s_wake_home_sec[0]))) {
        return 0U;
    }
    return s_wake_home_sec[s_wake_home_idx];
}

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
menu_page_t menu_page_sleep_wake, menu_page_system, menu_page_factory_option;
menu_page_t menu_page_factory_unlock, menu_page_factory_locked, menu_page_transport;
menu_page_t menu_page_sos, menu_page_screen_test_confirm, menu_page_accel_calibrate;
menu_page_t menu_page_status;      /* ui_task 用 &menu_page_status 判断当前页 */
menu_page_t menu_page_status_battery, menu_page_status_accel, menu_page_status_timer;
menu_page_t menu_page_protocol1, menu_page_protocol2, menu_page_powerlimit;
menu_page_t menu_page_tools, menu_page_games, menu_page_about;

/* ==================== 显示页（Display，三级，归拢显示相关项） ==================== */
static const menu_item_t menu_items_display[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_VALUE_("Backlight", &s_backlight, 1, 16, 1, NULL, backlight_apply),
    MENU_ITEM_TOGGLE_("Theme", &s_theme_toggle, "Light", "Dark", theme_toggle_apply),
    MENU_ITEM_VALUE_("Color", &s_theme_color_idx, 0, MENU_PALETTE_COUNT - 1, 1, NULL, theme_color_apply),
    /* 自动翻转：ON=跟随重力方向；OFF=固定方向。
     * 自动翻转生效时画面会倒过来，按键在视觉上左右对调，所以菜单里 PREV/NEXT
     * 会跟着互换语义（见 ui_task 的 ui_scan_action）；主界面的 MENU/NEXT 语义是
     * HOME/LED，不换。 */
    MENU_ITEM_TOGGLE_("Auto Flip", &s_auto_flip, "On", "Off", auto_flip_apply),
};
MENU_PAGE_("Display", menu_page_display, menu_items_display);

/* ==================== 休眠与唤醒页（Sleep & Wake，三级） ====================
 * Pickup/Motion Wake 仍控制DeepSleep时SC7A20是否保留10Hz；
 * Wake Home After控制一次DeepSleep持续多久后，唤醒先显示主屏而保留原菜单现场。 */
static const menu_item_t menu_items_sleep_wake[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ENUM_("Auto Sleep", &s_sleep_idx, s_sleep_opts, 9, sleep_apply),
    MENU_ITEM_TOGGLE_("Pickup Wake", &SC7A20_AlgoConfig.pickup_wake, "On", "Off", NULL),
    MENU_ITEM_TOGGLE_("Motion Wake", &SC7A20_AlgoConfig.motion_wake, "On", "Off", NULL),
    MENU_ITEM_ENUM_("Wake Home After", &s_wake_home_idx, s_wake_home_opts, 5, NULL),
};
MENU_PAGE_("Sleep & Wake", menu_page_sleep_wake, menu_items_sleep_wake);

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

/* ==================== PowerBank → Protocol#1（PD / PPS / UFCS 播发与开关） ====================
 * 配置真实状态放在 sw6306_algo；菜单只修改配置镜像并发 request。
 * 拆成两页是为了单页 20 项太长：本页是 PD/PPS/UFCS 一组，其余快充协议在 Protocol#2。 */
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

static const menu_item_t menu_items_protocol1[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_TOGGLE_("PD out",  &SW6306_AlgoConfig.pd_out,  "On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("PD in",   &SW6306_AlgoConfig.pd_in,   "On", "Off", proto_apply),
    MENU_ITEM_ACTION_("PPS Broadcast", pps_broadcast_apply),   /* PPS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("11V PPS", &SW6306_AlgoConfig.pps1,    "On", "Off", proto_apply),  /* 原 PPS1 */
    MENU_ITEM_TOGGLE_("21V PPS", &SW6306_AlgoConfig.pps3,    "On", "Off", proto_apply),  /* 原 PPS3 */
    MENU_ITEM_ACTION_("UFCS Broadcast", ufcs_broadcast_apply), /* UFCS 设置项之前：手动播发能力 */
    MENU_ITEM_TOGGLE_("UFCS out",&SW6306_AlgoConfig.ufcs_out,"On", "Off", proto_apply),
    MENU_ITEM_TOGGLE_("UFCS in", &SW6306_AlgoConfig.ufcs_in, "On", "Off", proto_apply),
};
MENU_PAGE_("Protocol#1", menu_page_protocol1, menu_items_protocol1);

/* ==================== PowerBank → Protocol#2（其它快充协议） ==================== */
static const menu_item_t menu_items_protocol2[] = {
    MENU_ITEM_BACK_("Return"),
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
};
MENU_PAGE_("Protocol#2", menu_page_protocol2, menu_items_protocol2);

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

/* ==================== PowerBank 页（Protocol#1/#2 + PowerLimit） ====================
 * Battery / SW6306 两个子页已删除：
 *  - 容量学习由 sw6306_algo 固定常开，不再需要用户开关；
 *  - SW6306 重初始化也不在菜单里手动做。
 * Record SOH 移到 System → Factory Option。 */
static const menu_item_t menu_items_powerbank[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("Protocol#1", &menu_page_protocol1),
    MENU_ITEM_PAGE_("Protocol#2", &menu_page_protocol2),
    MENU_ITEM_PAGE_("PowerLimit", &menu_page_powerlimit),
};
MENU_PAGE_("PowerBank", menu_page_powerbank, menu_items_powerbank);

/* ==================== Reset WORD_CONFIRM ====================
 * 必须翻到末屏后才能退出/确认；只恢复用户设置，不清EFC/SOH/硬件校准。 */
static const char * const word_reset[] = {
    "Reset System?",
    "Settings Will Reset",
    "Factory Data Stays",
    "Press NEXT to Exit",
    "Press CONF to Reset",
};
MENU_WORD_CONFIRM_(menu_page_reset, word_reset, action_reset_now);

/* ==================== 设置页（二级，四个子页） ====================
 * PowerBank / Display / Sleep & Wake / System（Language 在 System 里）。 */
static const menu_item_t menu_items_settings[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("PowerBank",    &menu_page_powerbank),
    MENU_ITEM_PAGE_("Display",      &menu_page_display),
    MENU_ITEM_PAGE_("Sleep & Wake", &menu_page_sleep_wake),
    MENU_ITEM_PAGE_("System",       &menu_page_system),
};
MENU_PAGE_("Settings", menu_page_settings, menu_items_settings);

/* ==================== System 页（三级：时间/出厂/运输/复位） ==================== */
/* Factory Option 发布版可加锁；开发版 MENU_FACTORY_LOCK_ENABLE=0 时直接开放。
 * 解锁授权只允许使用一次：成功进入 Factory Option 后立即清除。 */
static uint8_t s_factory_unlocked;
static char s_factory_unlock_status[20] = "Waiting...";
static char s_factory_password_buf[12];
static uint8_t s_factory_password_len;

static const char * const word_factory_unlock[] = {
    "Factory Unlock",
    "Enter Password",
    s_factory_unlock_status,
};

static void factory_unlock_check(void)
{
    s_factory_password_buf[s_factory_password_len] = '\0';
    if (strcmp(s_factory_password_buf, "TKWTL114514") == 0) {
        s_factory_unlocked = 1U;
        strcpy(s_factory_unlock_status, "Unlocked");
    } else {
        s_factory_unlocked = 0U;
        strcpy(s_factory_unlock_status, "Wrong Password");
    }
    s_factory_password_len = 0U;
    menu_notify_changed();
}

static void factory_unlock_hook(menu_word_hook_event_t event)
{
    uint8_t ch;

    if (event == MENU_WORD_HOOK_ENTER) {
        s_factory_unlocked = 0U;
        s_factory_password_len = 0U;
        strcpy(s_factory_unlock_status, "Waiting...");
        USART_RxBegin();
        return;
    }
    if (event == MENU_WORD_HOOK_EXIT) {
        USART_RxEnd();
        s_factory_password_len = 0U;
        return;
    }

    while (s_factory_unlocked == 0U && USART_RxReadByte(&ch)) {
        pm_api_refresh_idle();   /* 输入过程视为用户活动，避免等待密码时自动黑屏 */
        if (ch == '\r' || ch == '\n') {
            if (s_factory_password_len != 0U) factory_unlock_check();
        } else if (ch == 0x08U || ch == 0x7FU) {
            if (s_factory_password_len != 0U) s_factory_password_len--;
        } else if (s_factory_password_len < sizeof(s_factory_password_buf) - 1U) {
            s_factory_password_buf[s_factory_password_len++] = (char)ch;
            if (s_factory_password_len == sizeof("TKWTL114514") - 1U) {
                factory_unlock_check();
            }
        }
    }
}
MENU_WORD_ACTION_(menu_page_factory_unlock, word_factory_unlock, factory_unlock_hook);

static const char * const word_factory_locked[] = {
    "Factory Locked",
    "Use Factory Unlock",
    "Press CONF to Exit",
};
MENU_WORD_INFO_(menu_page_factory_locked, word_factory_locked);

/* Record SOH：把当前库仑计容量记录为 SOH 出厂基准（菜单只发 RAM request）。 */
static void record_soh_apply(menu_item_t *it)
{
    (void)it;
    SW6306_AlgoRequestRecordFactoryCapacity();
}

static const menu_item_t menu_items_factory_option[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ACTION_("Cycle Clear",    placeholder_apply),  /* TODO */
    MENU_ITEM_ACTION_("Record SOH",     record_soh_apply),
    MENU_ITEM_ACTION_("RBAT Calibrate", placeholder_apply),  /* TODO */
};
MENU_PAGE_("Factory Option", menu_page_factory_option, menu_items_factory_option);

static void factory_option_apply(menu_item_t *it)
{
    (void)it;
#if MENU_FACTORY_LOCK_ENABLE
    if (s_factory_unlocked == 0U) {
        menu_enter_page(&menu_page_factory_locked);
        return;
    }
#endif
    menu_enter_page(&menu_page_factory_option);
    s_factory_unlocked = 0U;   /* 授权只允许进入一次，进入后立即清除 */
}

static const char * const word_transport[] = {
    "Transport Mode",
    "Minimum Power State",
    "Wake: Press PWR/NEXT",
    "System Will Reset",
    "RAM Settings Lost",
    "Press NEXT to Exit",
    "Press CONF to Enter",
};
MENU_WORD_CONFIRM_(menu_page_transport, word_transport, action_transport_mode);

static const menu_item_t menu_items_system[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_PAGE_("Clock",           &menu_page_time),
    MENU_ITEM_ACTION_("Language",      lang_apply),
    MENU_ITEM_PAGE_("Factory Unlock",  &menu_page_factory_unlock),
    MENU_ITEM_ACTION_("Factory Option", factory_option_apply),
    MENU_ITEM_PAGE_("Transport Mode",  &menu_page_transport),
    MENU_ITEM_PAGE_("Reset",           &menu_page_reset),
};
MENU_PAGE_("System", menu_page_system, menu_items_system);

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
static char menu_status_uid[24];         /* SD3078 UID，显示为 "ID:0x" + 16 个十六进制字符 */

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
    /* ---- Battery：全整数字符串格式，避免链接printf浮点转换代码 ---- */
    if (SW6306_IsInitialized()) {
        uint32_t vbat_cv = ((uint32_t)SW6306_ReadVBAT() + 5U) / 10U;
        uint32_t ibat_ma = SW6306_ReadIBAT();
        uint32_t max_cwh = (uint32_t)(SW6306_ReadMaxEnergy_mWh() * 0.1f + 0.5f);
        uint32_t now_cwh = (uint32_t)(SW6306_ReadRemainEnergy_mWh() * 0.1f + 0.5f);

        mini_snprintf(menu_status_bat, sizeof(menu_status_bat), menu_tr("%lu.%02luV %lu.%03luA"),
                 (unsigned long)(vbat_cv / 100U), (unsigned long)(vbat_cv % 100U),
                 (unsigned long)(ibat_ma / 1000U), (unsigned long)(ibat_ma % 1000U));
        mini_snprintf(menu_status_maxcap, sizeof(menu_status_maxcap), menu_tr("Max: %lu.%02lu Wh"),
                 (unsigned long)(max_cwh / 100U), (unsigned long)(max_cwh % 100U));
        mini_snprintf(menu_status_presentcap, sizeof(menu_status_presentcap), menu_tr("Now: %lu.%02lu Wh"),
                 (unsigned long)(now_cwh / 100U), (unsigned long)(now_cwh % 100U));

        if (nvm_is_valid()) {
            float soh = SW6306_AlgoGetSOHPercent();
            float efc = SW6306_AlgoGetEquivalentCycles();
            uint32_t soh_i = (soh > 0.0f) ? (uint32_t)(soh + 0.5f) : 0U;
            uint32_t efc_centi = (efc > 0.0f) ? (uint32_t)(efc * 100.0f + 0.5f) : 0U;

            mini_snprintf(menu_status_health, sizeof(menu_status_health), menu_tr("Health: %lu%%"),
                     (unsigned long)soh_i);
            mini_snprintf(menu_status_cycles, sizeof(menu_status_cycles), menu_tr("Cycles: %lu.%02lu"),
                     (unsigned long)(efc_centi / 100U), (unsigned long)(efc_centi % 100U));
        } else {
            mini_snprintf(menu_status_health, sizeof(menu_status_health), "--");
            mini_snprintf(menu_status_cycles, sizeof(menu_status_cycles), "--");
        }

        {
            sw6306_learn_state_t ls = SW6306_ReadLearnState();
            const char *st;
            switch (ls) {
            case SW6306_LEARN_ST_DONE: st = menu_tr("Done"); break;
            case SW6306_LEARN_ST_ING:
                st = menu_tr(SW6306_IsCharging() ? "Learning" : "Idle");
                break;
            case SW6306_LEARN_ST_WAITING:
                st = menu_tr(SW6306_IsCharging() ? "Waiting" : "Idle");
                break;
            default: st = menu_tr("Unknown"); break;
            }
            mini_snprintf(menu_status_learn, sizeof(menu_status_learn), menu_tr("Learn:%s"), st);
        }
    } else {
        mini_snprintf(menu_status_bat,        sizeof(menu_status_bat),        "--");
        mini_snprintf(menu_status_maxcap,     sizeof(menu_status_maxcap),     "--");
        mini_snprintf(menu_status_presentcap, sizeof(menu_status_presentcap), "--");
        mini_snprintf(menu_status_health,     sizeof(menu_status_health),     "--");
        mini_snprintf(menu_status_cycles,     sizeof(menu_status_cycles),     "--");
        mini_snprintf(menu_status_learn,      sizeof(menu_status_learn),      "--");
    }

    /* ---- Accelerator：mg直接换算为0.01g整数，再格式化 ---- */
    if (SC7A20_IsInitialized()) {
        float mg[3] = { SC7A20_ReadX_mg(), SC7A20_ReadY_mg(), SC7A20_ReadZ_mg() };
        char *dst[3] = { menu_status_accel_x, menu_status_accel_y, menu_status_accel_z };
        size_t len[3] = { sizeof(menu_status_accel_x), sizeof(menu_status_accel_y), sizeof(menu_status_accel_z) };
        const char *key[3] = { "X: %s%lu.%02lu g", "Y: %s%lu.%02lu g", "Z: %s%lu.%02lu g" };
        uint8_t i;

        for (i = 0U; i < 3U; i++) {
            int32_t cg = (int32_t)(mg[i] * 0.1f + ((mg[i] >= 0.0f) ? 0.5f : -0.5f));
            uint32_t mag = (uint32_t)((cg < 0) ? -cg : cg);
            mini_snprintf(dst[i], len[i], menu_tr(key[i]),
                     (cg < 0) ? "-" : "",
                     (unsigned long)(mag / 100U), (unsigned long)(mag % 100U));
        }
    } else {
        mini_snprintf(menu_status_accel_x, sizeof(menu_status_accel_x), "X: --");
        mini_snprintf(menu_status_accel_y, sizeof(menu_status_accel_y), "Y: --");
        mini_snprintf(menu_status_accel_z, sizeof(menu_status_accel_z), "Z: --");
    }

    /* ---- Timer ---- */
    if (SD3078_ReadMonth() >= 1U && SD3078_ReadMonth() <= 12U &&
        SD3078_ReadDay() >= 1U && SD3078_ReadDay() <= 31U &&
        SD3078_ReadHour() <= 23U && SD3078_ReadMin() <= 59U && SD3078_ReadSec() <= 59U) {
        uint32_t vbackup_cv = ((uint32_t)SD3078_ReadBatt() + 5U) / 10U;

        mini_snprintf(menu_status_time, sizeof(menu_status_time), menu_tr("%02d:%02d:%02d"),
                 SD3078_ReadHour(), SD3078_ReadMin(), SD3078_ReadSec());
        mini_snprintf(menu_status_date, sizeof(menu_status_date), menu_tr("20%02d-%02d-%02d"),
                 SD3078_ReadYear(), SD3078_ReadMonth(), SD3078_ReadDay());
        mini_snprintf(menu_status_temp, sizeof(menu_status_temp), menu_tr("Temp: %d°C"),
                 (int)SD3078_ReadTemp());
        mini_snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), menu_tr("Vbackup: %lu.%02luV"),
                 (unsigned long)(vbackup_cv / 100U), (unsigned long)(vbackup_cv % 100U));
        mini_snprintf(menu_status_uid, sizeof(menu_status_uid), menu_tr("ID:0x%02X%02X%02X%02X%02X%02X%02X%02X"),
                 SD3078_ReadID(0), SD3078_ReadID(1), SD3078_ReadID(2), SD3078_ReadID(3),
                 SD3078_ReadID(4), SD3078_ReadID(5), SD3078_ReadID(6), SD3078_ReadID(7));
    } else {
        mini_snprintf(menu_status_time, sizeof(menu_status_time), "--:--:--");
        mini_snprintf(menu_status_date, sizeof(menu_status_date), "----");
        mini_snprintf(menu_status_temp, sizeof(menu_status_temp), "Temp: --");
        mini_snprintf(menu_status_vbackup, sizeof(menu_status_vbackup), "Vbackup: --");
        mini_snprintf(menu_status_uid, sizeof(menu_status_uid), "ID:0x----------------");
    }
}

/* ==================== 工具页 ==================== */
static void sos_confirm_apply(menu_item_t *it)
{
    (void)it;
    menu_app_enter(&menu_app_sos);
}

static const char * const word_sos[] = {
    "SOS Blink",
    "Emergency Light",
    "Max Bright Pulses",
    "100ms Dot / 5s+",
    "Press NEXT to Exit",
    "Press CONF to Start",
};
MENU_WORD_CONFIRM_(menu_page_sos, word_sos, sos_confirm_apply);

static void screen_test_confirm_apply(menu_item_t *it)
{
    (void)it;
    menu_app_enter(&menu_app_screen_test);
}

static const char * const word_screen_test[] = {
    "Screen Test",
    "Cycles Solid Colors",
    "Check Pixel Defects",
    "Press NEXT to Exit",
    "Press CONF to Start",
};
MENU_WORD_CONFIRM_(menu_page_screen_test_confirm, word_screen_test, screen_test_confirm_apply);

static void gravity_calibrate_apply(menu_item_t *it)
{
    (void)it;
    menu_app_enter(&menu_app_gravity_calibrate);
}

static const char * const word_accel_calibrate[] = {
    "Gravity Calibrate",
    "Auto Detect 6 Sides",
    "Hold Each Side 2 sec",
    "UP/DOWN/LEFT/RIGHT",
    "Then Face Up/Down",
    "Any Key Exits Test",
    "Press NEXT to Exit",
    "Press CONF to Start",
};
MENU_WORD_CONFIRM_(menu_page_accel_calibrate, word_accel_calibrate, gravity_calibrate_apply);

static const menu_item_t menu_items_tools[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_ACTION_("Leveler", placeholder_apply),
    MENU_ITEM_PAGE_("SOS Blink", &menu_page_sos),
    MENU_ITEM_PAGE_("Screen Test", &menu_page_screen_test_confirm),
    MENU_ITEM_PAGE_("Gravity Calibrate", &menu_page_accel_calibrate),
};
MENU_PAGE_("Tools", menu_page_tools, menu_items_tools);

/* ==================== 游戏页 ==================== */
static const menu_item_t menu_items_games[] = {
    MENU_ITEM_BACK_("Return"),
    MENU_ITEM_INFO_("Coming Soon"),
};
MENU_PAGE_("Games", menu_page_games, menu_items_games);

/* ==================== About WORD_INFO ====================
 * 3行窗口，PREV/NEXT有边界滚动，CONF返回根ICON页。 */
static const char * const word_about[] = {
    "Pocket PowerBank",
    "Author:TKWTL   Git:",
    "github.com/TKWTL/",
    "Pocket_PowerBank",
    " ",
    "Powered by:",
    "SW6306 + 2S 18650",
    "AT32F423 + LVGL 9.4",
    "Firmware 1.2.0",
};
MENU_WORD_INFO_(menu_page_about, word_about);

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

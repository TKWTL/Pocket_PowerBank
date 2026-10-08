/*
 * menu_ui.c - 菜单的 LVGL 渲染实现
 *
 * 条目切换动画：参考 MiaoUI 的 UI_Animation（PID 控制器）做左右滑动切换：
 *  - 旧条目标签按 PID 滑出（NEXT 向左、PREV 向右），新条目标签从对侧滑入；
 *  - 用 lv_obj_set_x() 设置真实位置（会同时无效化新旧区域，不留残影）；
 *  - 由 10ms 的 lv_timer 驱动 PID 步进。
 *
 * 注意：
 *  - 菜单屏背景必须显式 bg_opa=COVER（默认透明），否则旧屏像素透出叠加成乱码；
 *  - 不能用 style translate_x 做滑动动画（不带 EXT_DRAW_UPDATE，旧位置不无效化会留残影）。
 *
 * 参考来源：滑动切换与 PID 动画参数参考 MiaoUI 的 UI_Animation
 * （https://github.com/JFeng-Z/MiaoUI）；菜单框架整体借用点见 menu.h 文件头"参考来源"。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lvgl.h"
#include "menu_ui.h"
#include "menu.h"
#include "menu_pages.h"
#include "menu_theme.h"
#include "drivers.h"   /* 统一驱动包含头：SD3078 时间指示 */

/* ---------- 布局 ---------- */
/* MENU_SCR_W/H 在 menu.h 定义（状态机与渲染共用） */
/* 文本页对角线布局（160x40，全部主界面字体 terminus-u14b）：
 *  - 上一项：左上角，灰；当前项：中间，高亮；下一项：右下角，灰
 *  - 菜单名称：左下角，主题色；个数指示：右上角，主题色
 *  - 切换动画：NEXT/PREV 对称滑动（详见“滑动切换”节注释） */
#define MENU_ROW_TOP_Y   (-1)  /* 顶行 y（左上角：上一项，上移 1px 与标题对称） */
#define MENU_ROW_MID_Y   13     /* 中行 y（当前项，高亮） */
#define MENU_ROW_BOT_Y   26     /* 底行 y（左下标题 / 右下下一项） */
#define MENU_CORNER_X    1      /* 角落/左右边距 x */

/* ---------- PID 动画参数（参考 MiaoUI：位置类动画 ki 必须为 0，否则过冲乱飞） ---------- */
#define MENU_PID_KP       0.25f
#define MENU_PID_KI       0.0f
#define MENU_PID_KD       0.05f
#define MENU_PID_INT_LIMIT 80.0f
#define MENU_PID_ANIM_MS  10    /* 驱动定时器周期 */

typedef struct {
    float kp, ki, kd;
    float current;
    float target;
    float integral;
    float last_error;
    bool  active;
} pid_anim_t;

static lv_obj_t *s_scr;        /* 菜单屏 */
static lv_obj_t *s_header;     /* 左下：菜单名称（主题色） */
static lv_obj_t *s_indicator;  /* 右上：页码 / EDIT（主题色） */
static lv_obj_t *s_item_prev;  /* 左上：上一项（灰） */
static lv_obj_t *s_item_prev_in; /* 左上：动画期间屏外滑入的上一项 */
static lv_obj_t *s_item;       /* 中间：当前条目（高亮，滑出/静止） */
static lv_obj_t *s_item_in;    /* 中间：滑入条目（仅动画期间显示） */
static lv_obj_t *s_item_next;  /* 右下：下一项（灰） */
static lv_obj_t *s_item_next_in; /* 右下：动画期间屏外滑入的下一项 */

/* PID 滑动状态（斜向：x/y 各一组） */
static pid_anim_t s_pid_out;   /* 滑出 x */
static pid_anim_t s_pid_out_y; /* 滑出 y */
static pid_anim_t s_pid_in;    /* 滑入 x */
static pid_anim_t s_pid_in_y;  /* 滑入 y */
/* 角落 prev/next：出屏（旧上一/下一项水平移出屏幕，与滑入方向平行）+ 滑入
 * （上上/下下一项屏外水平滑入）。所有对象从各自起点立即移动，
 * 不在角落留下静止图标。 */
static pid_anim_t s_prev_out_x;  /* s_item_prev 出屏 x（NEXT，向左） */
static pid_anim_t s_next_out_x;  /* s_item_next 出屏 x（PREV，向右） */
static pid_anim_t s_prev_in_x;   /* s_item_prev_in 滑入（PREV） */
static pid_anim_t s_next_in_x;   /* s_item_next_in 滑入（NEXT） */
static bool s_sliding;
/* 本次滑动的方向锁存（+1 NEXT / -1 PREV）：slide_start 时固定，slide_finish 只认它，
 * 不再回头读 menu_get_state()->nav_dir——动画期间再有按键会改写 nav_dir，
 * 否则收尾会把方向判反（角色交接错位）。 */
static int8_t s_slide_dir;
static lv_timer_t *s_anim_timer;

/* 渲染状态跟踪 */
static const menu_page_t *s_last_page;
static uint8_t s_last_index;
static bool s_last_active;
static bool s_first_after_open;

/* ---------- PID（参考 MiaoUI UI_Animation） ---------- */
static void pid_init(pid_anim_t *p, float current, float target)
{
    p->kp = MENU_PID_KP;
    p->ki = MENU_PID_KI;
    p->kd = MENU_PID_KD;
    p->current = current;
    p->target  = target;
    p->integral = 0.0f;
    p->last_error = 0.0f;
    p->active = true;
}

/* 单步 PID；返回是否仍在运动 */
static bool pid_step(pid_anim_t *p)
{
    float err, delta, vel;

    if (!p->active) {
        return false;
    }
    err = p->target - p->current;
    p->integral += err;
    if (p->integral >  MENU_PID_INT_LIMIT) p->integral =  MENU_PID_INT_LIMIT;
    if (p->integral < -MENU_PID_INT_LIMIT) p->integral = -MENU_PID_INT_LIMIT;
    delta = err - p->last_error;
    p->last_error = err;
    vel = p->kp * err + p->ki * p->integral + p->kd * delta;
    /* 健壮性：非有限值直接收敛到目标，防止坐标被 NaN/Inf 污染 */
    if (!isfinite(p->current) || !isfinite(p->target) || !isfinite(vel)) {
        p->current = p->target;
        p->active  = false;
        return false;
    }
    p->current += vel;
    if (fabsf(err) < 0.5f && fabsf(vel) < 0.5f) {
        p->current = p->target;
        p->active = false;
    }
    return p->active;
}

/* 带每帧最大位移限制的 PID 步进（限速）：
 * 用于长距离出屏/滑入，避免第一帧 kp*距离 过大而"瞬间消失"。 */
static bool pid_step_lim(pid_anim_t *p, float max_step)
{
    float err, delta, vel;

    if (!p->active) {
        return false;
    }
    err = p->target - p->current;
    p->integral += err;
    if (p->integral >  MENU_PID_INT_LIMIT) p->integral =  MENU_PID_INT_LIMIT;
    if (p->integral < -MENU_PID_INT_LIMIT) p->integral = -MENU_PID_INT_LIMIT;
    delta = err - p->last_error;
    p->last_error = err;
    vel = p->kp * err + p->ki * p->integral + p->kd * delta;
    /* 健壮性：非有限值直接收敛到目标，防止坐标被 NaN/Inf 污染 */
    if (!isfinite(p->current) || !isfinite(p->target) || !isfinite(vel)) {
        p->current = p->target;
        p->active  = false;
        return false;
    }
    if (vel >  max_step) vel =  max_step;
    if (vel < -max_step) vel = -max_step;
    p->current += vel;
    if (fabsf(err) < 0.5f && fabsf(vel) < 0.5f) {
        p->current = p->target;
        p->active = false;
    }
    return p->active;
}

/* 条目文本格式化。
 * selected=true：当前选中（中间大项）——显示完整内容（箭头/选项/数值）；
 * selected=false：角落 prev/next——只显示纯 label（无箭头、无值），角落保持简洁。 */
static void fmt_item_text(const menu_item_t *it, bool editing, bool selected, char *buf, uint8_t len)
{
    if (!it) {
        buf[0] = '\0';
        return;
    }
    if (!selected) {
        /* 未选中（角落）：所有类型只显示 label（去 Return 的 <、子页/动作的 >、
         * toggle 的选项与数值，避免角落溢出），完整内容仅选中时展示。 */
        snprintf(buf, len, "%s", it->label ? menu_tr(it->label) : "");
        return;
    }
    switch (it->type) {
    case MENU_ITEM_BACK:
        snprintf(buf, len, "< %s", menu_tr(it->label));
        break;
    case MENU_ITEM_PAGE:
        snprintf(buf, len, "%s >", menu_tr(it->label));   /* 子页：> 进入下一级 */
        break;
    case MENU_ITEM_ACTION:
        snprintf(buf, len, "%s !", menu_tr(it->label));   /* 动作：! 立即执行（与子页 > 区分） */
        break;
    case MENU_ITEM_TOGGLE: {
        /* 选中：显示完整选项（如 "Language: English"）；未选中已在函数开头
         * 统一返回纯 label，不显示箭头/选项。 */
        uint8_t on = it->toggle_ptr ? *it->toggle_ptr : 0;
        const char *txt = on ? (it->toggle_on ? it->toggle_on : "ON")
                             : (it->toggle_off ? it->toggle_off : "OFF");
        snprintf(buf, len, "%s: %s", menu_tr(it->label), menu_tr(txt));
        break;
    }
    case MENU_ITEM_VALUE:
    case MENU_ITEM_ENUM: {
        int32_t v = it->value_ptr ? *it->value_ptr : 0;
        if (it->type == MENU_ITEM_ENUM && it->enum_opts && (uint8_t)v < it->enum_count) {
            /* 枚举：显示 "label: 选项文本"（选项经 menu_tr 本地化） */
            snprintf(buf, len, "%s: %s", menu_tr(it->label), menu_tr(it->enum_opts[v]));
        } else if (it->unit) {
            snprintf(buf, len, "%s: %d %s", menu_tr(it->label), (int)v, menu_tr(it->unit));
        } else {
            snprintf(buf, len, "%s: %d", menu_tr(it->label), (int)v);
        }
        if (editing) {
            size_t l = strlen(buf);
            if (l + 2 < len) {
                buf[l] = ' ';
                buf[l + 1] = '*';
                buf[l + 2] = '\0';
            }
        }
        break;
    }
    case MENU_ITEM_INFO:
    default:
        snprintf(buf, len, "%s", it->label ? menu_tr(it->label) : "");
        break;
    }
}

/* ---------- 文本宽度处理（按实测像素，不再按"字符数估算"） ----------
 * 分两个角色：
 *   - 中间当前项：box 上限 = 14 个字符宽；超过则由 SCROLL_CIRCULAR 循环滚动；
 *   - 角落/动画项：box 直接贴合文本实测宽度，只要不超出屏幕就完整显示，
 *     只有真要越过右边界时才从尾部修剪（UTF-8 边界）。
 * 历史教训：曾经用"12 × 空格步进"当角落 box 上限，实测这条路线会把 box 压得
 * 比文字窄（Display 只有 56px 都被截成 "Dis..."），所以现在一律以
 * lv_text_get_size() 的实测宽度为准，只保证"别超出屏幕"。 */
#define MENU_TEXT_SCROLL_CHARS   14U   /* 中间项：超过 14 个字符宽才开始滚动 */

static int32_t menu_char_w(const lv_obj_t *lbl)
{
    const lv_font_t *f = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
    int32_t w;

    if (f == NULL || f->line_height <= 0) {
        return 8;   /* 兜底：按 8px 字宽估算 */
    }
    w = lv_font_get_glyph_width(f, ' ', ' ');
    return (w > 0) ? w : 8;
}

/* 中间项：box 上限 = 14 个字符宽（不超屏），超出交给 SCROLL_CIRCULAR 滚动 */
static void label_fit_box(lv_obj_t *lbl, uint8_t scroll)
{
    int32_t cap   = (scroll != 0U) ? (int32_t)MENU_TEXT_SCROLL_CHARS * menu_char_w(lbl)
                                   : MENU_SCR_W;
    int32_t want  = lv_obj_get_self_width(lbl);
    int32_t limit = (want < cap) ? want : cap;

    if (limit > MENU_SCR_W) limit = MENU_SCR_W;
    if (limit < 1) limit = 1;
    if (lv_obj_get_width(lbl) != limit) {
        lv_obj_set_width(lbl, limit);
    }
}

/* 把 label 水平居中（手动定位，不依赖对齐） */
static void item_recenter(lv_obj_t *label)
{
    label_fit_box(label, 1U);   /* 中间项：限宽 + 超出循环滚动 */
    lv_obj_update_layout(label);
    lv_obj_set_x(label, (MENU_SCR_W - lv_obj_get_width(label)) / 2);
}

/* 右下角定位（手动计算）。不能用 lv_obj_align：align 会写入 LV_STYLE_ALIGN，
 * 布局刷新时 lv_obj_refr_pos() 会按对齐把对象拉回右下角，覆盖动画中的
 * lv_obj_set_x()，导致角落项滑不出屏幕而变成固定遮罩。 */
static void pos_next_corner(lv_obj_t *lbl)
{
    lv_obj_update_layout(lbl);
    lv_obj_set_pos(lbl, MENU_SCR_W - lv_obj_get_width(lbl) - MENU_CORNER_X, MENU_ROW_BOT_Y);
}

/* 左上角定位：统一走这里，避免各处直接 lv_obj_set_pos 时漏掉前处理 */
static void pos_prev_corner(lv_obj_t *lbl)
{
    lv_obj_set_pos(lbl, MENU_CORNER_X, MENU_ROW_TOP_Y);
}

/* 角落文本：box 贴合实测宽度；只有超过"到右边界还剩多少"时才从尾部修剪。
 * 短文案（Display、Sleep & Wake 之类）因此不会被无谓截断。 */
static void corner_set_text(lv_obj_t *lbl, const char *text)
{
    const lv_font_t *font = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
    int32_t limit = MENU_SCR_W - MENU_CORNER_X;   /* 角落起点在屏内，最多能占这么宽 */
    lv_point_t sz;
    char tmp[96];

    if (text == NULL) {
        text = "";
    }
    if (strlen(text) >= sizeof(tmp)) {
        text = "";   /* 异常超长：宁可空，也不越界拷贝 */
    } else {
        memcpy(tmp, text, strlen(text) + 1U);
    }

    lv_text_get_size(&sz, tmp, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= limit) {
        lv_label_set_text(lbl, tmp);
        lv_obj_set_width(lbl, (sz.x > 0) ? (sz.x + 2) : 1);
        return;
    }

    /* 超宽：按 UTF-8 字符边界从尾部回退，直到宽度落进可用范围 */
    {
        size_t cut = strlen(tmp);
        while (cut > 0U) {
            cut--;
            while (cut > 0U && ((uint8_t)tmp[cut] & 0xC0U) == 0x80U) {
                cut--;   /* 回退到 UTF-8 字符起始字节 */
            }
            tmp[cut] = '\0';
            lv_text_get_size(&sz, tmp, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x <= limit) {
                break;
            }
        }
    }
    lv_label_set_text(lbl, tmp);
    lv_obj_set_width(lbl, (sz.x > 0) ? (sz.x + 2) : 1);
}

/* ---------- 条目配色（必须定义在所有使用它的函数之前） ----------
 * 三种角色，各一个函数，互不复用（曾经复用导致过一次"角落也变白"的连锁错误）：
 *  - item_selected_color()：中间选中行。Return 用白色（on_primary，突出高亮），
 *    其余用普通文字色；"<" 与文字同一 label，所以一起变色。
 *  - item_dim_color()：未选中且非 Return 的次要灰。
 *  - item_corner_color()：角落（未选中）标签。Return 保持主题色 primary（要求如此），
 *    其余落灰。 */
static lv_color_t item_selected_color(const menu_item_t *it)
{
    if (it && it->type == MENU_ITEM_BACK) {
        return menu_theme_get()->on_primary;   /* 选中 Return：白色高亮（不是主题色） */
    }
    return menu_theme_get()->text;
}

static lv_color_t item_dim_color(void)
{
    return menu_theme_get()->text_sec;
}

/* 取某页某索引的条目（越界返回 NULL） */
static const menu_item_t *page_item(const menu_page_t *pg, uint8_t index)
{
    if (!pg || index >= pg->item_count) {
        return NULL;
    }
    return &pg->items[index];
}

/* 角落（未选中）项颜色：Return 用主题色，其余次要灰。
 * 这里不复用 item_selected_color()：那样角上的 Return 会跟着变成白色。 */
static lv_color_t item_corner_color(const menu_item_t *it)
{
    return (it && it->type == MENU_ITEM_BACK) ? menu_theme_get()->primary
                                              : item_dim_color();
}

static void color_unselected(lv_obj_t *lbl, const menu_item_t *it)
{
    lv_obj_set_style_text_color(lbl, item_corner_color(it), 0);
}

/* 更新左上/右下角上一项/下一项文本（菜单循环，取模索引）。
 * 颜色在这里与文本一起写：两者用同一个条目指针，避免"文本换了、颜色还是上一帧
 * 那个条目"造成的错色（角落 Return 有时显示为灰就是这类不同步）。 */
static void refresh_prev_next(const menu_page_t *pg, uint8_t index)
{
    char buf[24];
    const menu_item_t *it;
    uint8_t n = pg ? pg->item_count : 0;

    if (n > 1) {
        it = page_item(pg, (uint8_t)((index + n - 1) % n));
        fmt_item_text(it, false, false, buf, sizeof(buf));
        corner_set_text(s_item_prev, buf);
        color_unselected(s_item_prev, it);
        pos_prev_corner(s_item_prev);   /* 复位左上角（出屏动画后可能停在屏外，否则恢复显示也看不见） */
        it = page_item(pg, (uint8_t)((index + 1) % n));
        fmt_item_text(it, false, false, buf, sizeof(buf));
        corner_set_text(s_item_next, buf);
        color_unselected(s_item_next, it);
        pos_next_corner(s_item_next);   /* 文本变化后手动重定位右下角 */
    } else {
        corner_set_text(s_item_prev, "");
        corner_set_text(s_item_next, "");
    }
}

/* ---------- 滑动切换 ---------- */
/* 条目切换动画（NEXT / PREV 对称，每次切换同时有 4 个对象在动，不是整条对角线整体流动）。
 * 静止时 3 个可动对象：左上角 A=上一项、中间 B=当前项、右下角 C=下一项。
 *
 * 坐标行：MENU_ROW_TOP_Y=-1（顶行）/ MENU_ROW_MID_Y=13（中行）/ MENU_ROW_BOT_Y=26（底行）；
 * 左上角 x=MENU_CORNER_X=1（左对齐），右下角 x=160-w-1（右对齐）。
 *
 * NEXT（对象 → 起点 → 终点，驱动 PID）：
 *  - A（s_item_prev）      (1,-1)           → (-160,-1)        s_prev_out_x：水平向左出屏（限速 30px/帧）
 *  - B（s_item）           中间居中          → (1,-1)            s_pid_out / s_pid_out_y：沿对角线滑到左上角
 *  - C（s_item_in）        (160-in_w-1, 26)  → 中间居中          s_pid_in / s_pid_in_y：自右下角滑入中间
 *  - D（s_item_next_in）   (160, 26)         → (160-nw-1, 26)    s_next_in_x：自屏外右缘水平滑入右下角
 *
 * PREV（对象 → 起点 → 终点，驱动 PID）：
 *  - C（s_item_next）      (160-w-1, 26)     → (160, 26)         s_next_out_x：水平向右出屏（限速 30px/帧）
 *  - B（s_item）           中间居中          → (160-out_w-1, 26) s_pid_out / s_pid_out_y：沿对角线滑到右下角
 *  - A（s_item_in）        (1,-1)            → 中间居中          s_pid_in / s_pid_in_y：自左上角滑入中间
 *  - E（s_item_prev_in）   (-160,-1)         → (1,-1)            s_prev_in_x：自屏外左缘水平滑入左上角
 *
 * 所有对象从各自起点立即移动（不延迟、不在角落留下静止图标）。
 * 动画中所有文本一律按未选中格式（纯 label，无箭头/选项）显示——完整格式长文本
 * 在角落/过渡位置会与滑出项重叠成"影子"；动画结束由 slide_commit_* 做对象交接
 * 后中间项才切换完整格式并高亮。滑动期间所有项灰色，高亮只出现在静止的中间项
 * （永不两个同时高亮）。 */

/* 停止全部 PID（防中断后残留动画继续驱动对象，导致角落被拖出屏/状态错乱）。
 * 同时把 current 吸附到 target：动画结束/打断时对象精确停在目标位，
 * 避免交接/快照的起点带偏移或残留抖动。 */
static void pid_stop_all(void)
{
    s_pid_out.active    = false;  s_pid_out.current    = s_pid_out.target;
    s_pid_out_y.active  = false;  s_pid_out_y.current  = s_pid_out_y.target;
    s_pid_in.active     = false;  s_pid_in.current     = s_pid_in.target;
    s_pid_in_y.active   = false;  s_pid_in_y.current   = s_pid_in_y.target;
    s_prev_out_x.active = false;  s_prev_out_x.current = s_prev_out_x.target;
    s_next_out_x.active = false;  s_next_out_x.current = s_next_out_x.target;
    s_prev_in_x.active  = false;  s_prev_in_x.current  = s_prev_in_x.target;
    s_next_in_x.active  = false;  s_next_in_x.current  = s_next_in_x.target;
}

/* 把整屏设置成指定索引的静止态（无动画残留、对象状态完全一致）：
 * 滑动结束、以及动画被按键打断后重开滑动前都走这里，保证从一致状态出发。
 * 三个静止标签（中间/左上/右下）的文本与颜色都在这里一次写全 —— 换页时若只写
 * 中间项，角落会带着上一页的条目残留（例如从 Settings 退出再进别的文本页，
 * 左上角还显示 Power Limit）。 */
static void snap_static(const menu_page_t *pg, uint8_t index)
{
    char buf[32];
    uint8_t n = pg ? pg->item_count : 0;

    pid_stop_all();
    s_sliding = false;
    s_slide_dir = 0;

    lv_obj_add_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);

    if (pg && index < pg->item_count) {
        fmt_item_text(&pg->items[index], menu_get_state()->editing, true, buf, sizeof(buf));
        lv_label_set_text(s_item, buf);
        item_recenter(s_item);
        lv_obj_set_y(s_item, MENU_ROW_MID_Y);
        /* 选中项高亮色：选中 Return 用白色，其余普通文字色（"<" 与文字同一 label） */
        lv_obj_set_style_text_color(s_item, item_selected_color(&pg->items[index]), 0);
        lv_obj_remove_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);
        if (n > 1) {
            const menu_item_t *itp = page_item(pg, (uint8_t)((index + n - 1U) % n));
            const menu_item_t *itn = page_item(pg, (uint8_t)((index + 1U) % n));
            fmt_item_text(itp, false, false, buf, sizeof(buf));
            corner_set_text(s_item_prev, buf);
            color_unselected(s_item_prev, itp);
            pos_prev_corner(s_item_prev);
            fmt_item_text(itn, false, false, buf, sizeof(buf));
            corner_set_text(s_item_next, buf);
            color_unselected(s_item_next, itn);
            pos_next_corner(s_item_next);
        } else {
            /* 单条目页：两个角落清空，避免残留上一页文本 */
            corner_set_text(s_item_prev, "");
            corner_set_text(s_item_next, "");
        }
    }

    lv_obj_invalidate(s_scr);
}

static void slide_start(int8_t dir)
{
    const menu_page_t *pg = menu_current_page();
    const menu_state_t *st = menu_get_state();
    char buf[32];
    const menu_item_t *it = menu_current_item();
    int32_t out_w, in_w, out_x, out_y, in_x, in_y, in_center_x, nw;
    uint8_t n = pg ? pg->item_count : 1;
    /* 滑出项用的是"上一帧的索引"，而换页后它可能属于上一页（索引超出本页条目数）。
     * 这里必须夹到本页范围内，否则 pg->items[prev] 越界读到别的条目
     * （例如进过 6 项的 Settings 页再进 2 项的 Games 页，左上角会显示 PowerLimit）。 */
    uint8_t prev = (s_last_index < n) ? s_last_index : 0;

    /* 健壮性：任何新滑动前先停止全部 PID（被中断的动画不得残留驱动对象），
     * 并保证条目数 >=1，避免取模除零 */
    pid_stop_all();
    if (n == 0) {
        n = 1;
    }

    /* 健壮性：先隐藏全部滑入对象（s_item_in / s_item_prev_in / s_item_next_in），
     * 再按方向重新显示需要的——防止上次动画残留的 _in 对象停在屏内
     * 变成“不动的图标”（显示旧文本、不随切换更新）。 */
    lv_obj_add_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);

    /* 滑入项文本 = 当前（新）条目（动画中一律未选中格式纯 label：
     * 完整格式长文本在角落/过渡位置会与滑出项重叠成"影子"；
     * 到位后由 slide_commit_* 换成完整格式并高亮） */
    fmt_item_text(it, st->editing, false, buf, sizeof(buf));
    lv_label_set_text(s_item_in, buf);
    lv_obj_remove_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_item_in);
    in_w = lv_obj_get_width(s_item_in);
    in_center_x = (MENU_SCR_W - in_w) / 2;

    /* 滑出项（旧当前项）在动画中同样按未选中格式显示（纯 label，无箭头/选项），
     * 避免它带着完整格式长文本滑到角落与滑入项重叠成"影子"；
     * 先更新文本再量宽度，保证滑出起点居中正确。 */
    fmt_item_text(&pg->items[prev], false, false, buf, sizeof(buf));
    lv_label_set_text(s_item, buf);
    lv_obj_update_layout(s_item);
    out_w = lv_obj_get_width(s_item);
    lv_obj_set_x(s_item, (MENU_SCR_W - out_w) / 2);
    lv_obj_set_y(s_item, MENU_ROW_MID_Y);

    if (dir > 0) {
        /* NEXT：左上角旧上一项向左滑出屏幕；中间项滑到左上；原下一项自右下滑入中间；
         * 下下一项自屏外右滑入右下角 */
        out_x = MENU_CORNER_X;                        /* s_item → 左上角 */
        out_y = MENU_ROW_TOP_Y;
        in_x  = MENU_SCR_W - in_w - MENU_CORNER_X;    /* s_item_in ← 右下角 */
        in_y  = MENU_ROW_BOT_Y;
        /* s_item_prev：重设为旧上一项（refresh 已改为新值），向左滑出屏幕。
         * 先把对象位置与 PID 起点显式对齐（不依赖 set_text 前的旧位置）。 */
        fmt_item_text(&pg->items[(prev + n - 1) % n], false, false, buf, sizeof(buf));
        corner_set_text(s_item_prev, buf);
        pos_prev_corner(s_item_prev);
        pid_init(&s_prev_out_x, (float)MENU_CORNER_X, (float)(-MENU_SCR_W));   /* 水平向左出屏 */
        /* s_item_next：隐藏，由 s_item_next_in（下下一项）自屏外右滑入右下角。
         * 滑入项文本直接从索引计算（items[index+1]），不依赖角落标签当前文本：
         * 中断路径下角落文本可能是"按键前"旧值（=新当前项），复制会变成两个
         * 相同文字重叠，且后续每次切换都沿用错误文本。 */
        lv_obj_add_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);
        fmt_item_text(&pg->items[(st->index + 1) % n], false, false, buf, sizeof(buf));
        corner_set_text(s_item_next_in, buf);
        lv_obj_remove_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(s_item_next_in);
        nw = lv_obj_get_width(s_item_next_in);
        lv_obj_set_pos(s_item_next_in, MENU_SCR_W, MENU_ROW_BOT_Y);
        pid_init(&s_next_in_x, (float)MENU_SCR_W, (float)(MENU_SCR_W - nw - MENU_CORNER_X));
    } else {
        /* PREV：右下角旧下一项向右滑出屏幕；中间项滑到右下；原上一项自左上滑入中间；
         * 上上一项自屏外左滑入左上角 */
        out_x = MENU_SCR_W - out_w - MENU_CORNER_X;   /* s_item → 右下角 */
        out_y = MENU_ROW_BOT_Y;
        in_x  = MENU_CORNER_X;                        /* s_item_in ← 左上角 */
        in_y  = MENU_ROW_TOP_Y;
        /* s_item_next：重设为旧下一项，向右滑出屏幕（起点手动计算并显式定位，
         * 避免 set_text 后 align/旧文本位置残留导致起点错误/滑出失效） */
        fmt_item_text(&pg->items[(prev + 1) % n], false, false, buf, sizeof(buf));
        corner_set_text(s_item_next, buf);
        lv_obj_update_layout(s_item_next);
        lv_obj_set_pos(s_item_next,
                       (MENU_SCR_W - lv_obj_get_width(s_item_next) - MENU_CORNER_X),
                       MENU_ROW_BOT_Y);
        pid_init(&s_next_out_x,
                 (float)(MENU_SCR_W - lv_obj_get_width(s_item_next) - MENU_CORNER_X),
                 (float)MENU_SCR_W);   /* 水平向右出屏 */
        /* s_item_prev：隐藏，由 s_item_prev_in（上上一项）自屏外左滑入左上角。
         * 滑入项文本直接从索引计算（items[index-1]），不依赖角落标签当前文本
         * （原因同上：中断路径下角落文本可能是"按键前"旧值=新当前项）。 */
        lv_obj_add_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
        fmt_item_text(&pg->items[(st->index + n - 1) % n], false, false, buf, sizeof(buf));
        corner_set_text(s_item_prev_in, buf);
        lv_obj_remove_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_item_prev_in, -MENU_SCR_W, MENU_ROW_TOP_Y);
        pid_init(&s_prev_in_x, (float)(-MENU_SCR_W), (float)MENU_CORNER_X);
    }

    /* 新项起点（对象位置与 PID current 一致） */
    lv_obj_set_pos(s_item_in, in_x, in_y);

    pid_init(&s_pid_out,   (float)lv_obj_get_x(s_item), (float)out_x);
    pid_init(&s_pid_out_y, (float)lv_obj_get_y(s_item), (float)out_y);
    pid_init(&s_pid_in,    (float)in_x,                 (float)in_center_x);
    pid_init(&s_pid_in_y,  (float)in_y,                 (float)MENU_ROW_MID_Y);

    /* 滚动动画中所有项均为灰色（不高亮），落定后才恢复高亮 */
    lv_obj_set_style_text_color(s_item,    menu_theme_get()->text_sec, 0);
    lv_obj_set_style_text_color(s_item_in, menu_theme_get()->text_sec, 0);

    s_slide_dir = dir;               /* 方向锁存：收尾只认它 */
    s_sliding = true;
}

/* ---------- 动画收尾：对象交接（不重建静止态） ----------
 * 动画终态本身就已经是静止态——四个对象都停在目标位。若收尾再调 snap_static()，
 * 它会把【刚刚滑出屏幕的那个对象】当作角落项重新 set_pos 回 (1,-1)，于是动画结束
 * 瞬间整条 label 又"闪回原位"（这不是显存残像素，而是软件把对象搬回来了）。
 * 正确做法是交换 lv_obj_t* 指针，让已经到位的动画对象直接成为新的静止对象：
 * 不重新 set_pos、不重新 set_text 内容（只在需要时补完整格式），坐标与像素都连续。
 * snap_static() 仍然保留给：首次进页、页面切换、动画被按键打断、异常恢复。 */

static void slide_end_common(void)
{
    s_sliding   = false;
    s_slide_dir = 0;
}

static void slide_commit_next(const menu_page_t *pg, uint8_t index)
{
    lv_obj_t *op = s_item_prev;      /* 已滑出屏外 */
    lv_obj_t *oi = s_item;           /* 已到左上角 */
    lv_obj_t *on = s_item_next;      /* 动画中被隐藏 */
    const menu_item_t *it;
    char buf[32];
    uint8_t n = pg ? pg->item_count : 0;

    /* 三个动画临时对象在终态均不可见，直接交接下来当前项用 */
    lv_obj_add_flag(s_item_in,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);

    s_item_prev = oi;                /* B → 左上角（已在位，不改坐标） */
    s_item      = s_item_in;         /* C → 中间（已在位，不改坐标） */
    s_item_next = s_item_next_in;    /* D → 右下角（已在位，不改坐标） */
    s_item_in      = on;
    s_item_next_in = op;             /* 备用于下一轮 */

    /* 交接后的三个静止对象必须显式可见：它们在动画期间处于各种隐藏状态
     * （例如 NEXT 的 B 当时是 s_item_next，被 slide_start 隐藏过），
     * 而 menu_ui_redraw 的 unhide 只在下一帧才发生，不补这一手会看到"缺项"。 */
    lv_obj_remove_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_item,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);

    /* 角色变了要补静止态的"内容"：中间项换完整格式并恢复高亮，角落补齐文本+颜色 */
    if (n > 0 && index < n) {
        fmt_item_text(&pg->items[index], menu_get_state()->editing, true, buf, sizeof(buf));
        lv_label_set_text(s_item, buf);
        /* 落定高亮：选中 Return 用白色，其余用普通文字色（"<" 与文字同一 label） */
        lv_obj_set_style_text_color(s_item, item_selected_color(&pg->items[index]), 0);
        /* 必须重新居中：动画期间中间项按【未选中简格式】文本宽度定位，
         * 换成完整格式（带箭头/选项）后宽度变了，沿用旧 x 会让它偏左甚至看起来"消失"
         * （snap_static 走的是同一套：set_text 后 item_recenter） */
        item_recenter(s_item);
        it = page_item(pg, (uint8_t)((index + 1) % n));
        fmt_item_text(it, false, false, buf, sizeof(buf));
        corner_set_text(s_item_next, buf);
        color_unselected(s_item_next, it);
        pos_next_corner(s_item_next);
        /* 左上角这个对象在 NEXT 里刚由旧中间项滑来，文本已是本轮的上一项，
         * 颜色在这里一并补齐（与文本同源），不依赖下一帧 apply_theme。 */
        it = page_item(pg, (uint8_t)((index + n - 1) % n));
        color_unselected(s_item_prev, it);
    }

    slide_end_common();
}

static void slide_commit_prev(const menu_page_t *pg, uint8_t index)
{
    lv_obj_t *op = s_item_prev;      /* 动画中被隐藏 */
    lv_obj_t *oi = s_item;           /* 已到右下角 */
    lv_obj_t *on = s_item_next;      /* 已滑出屏外 */
    const menu_item_t *it;
    char buf[32];
    uint8_t n = pg ? pg->item_count : 0;

    lv_obj_add_flag(s_item_in,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);

    s_item_prev = s_item_prev_in;    /* 滑入项 → 左上角（已在位，不改坐标） */
    s_item      = s_item_in;         /* 滑入项 → 中间（已在位，不改坐标） */
    s_item_next = oi;                /* B → 右下角（已在位，不改坐标） */
    /* 备用对象重新分配（必须与 NEXT 完全镜像，6 个角色只能对应 6 个不同对象）：
     *  - 旧 prev：动画中被隐藏（那时它还是 s_item_prev）→ 转下一轮的 s_item_in
     *  - 旧 next：向左滑出屏外（那时它还是 s_item_next）→ 转下一轮的 s_item_prev_in
     *  - s_item_next_in 保持原备用对象不动
     * 注意：这里曾经写成 s_item_next_in = on，导致 s_item_prev 与 s_item_prev_in
     * 指向同一对象；此后每次 PREV 都少一个独立对象，最终多个角色重叠、
     * 左上角项被一起隐藏、以及"高亮项被搬到错误位置"。 */
    s_item_in      = op;
    s_item_prev_in = on;

    /* 同 NEXT：交接后三个静止对象显式可见（PREV 的 B 当时是被隐藏的 s_item_prev） */
    lv_obj_remove_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_item,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);

    if (n > 0 && index < n) {
        fmt_item_text(&pg->items[index], menu_get_state()->editing, true, buf, sizeof(buf));
        lv_label_set_text(s_item, buf);
        lv_obj_set_style_text_color(s_item, item_selected_color(&pg->items[index]), 0);
        /* 同 NEXT：换完整格式后必须重新居中（动画期间是按简格式宽度定位的） */
        item_recenter(s_item);
        it = page_item(pg, (uint8_t)((index + n - 1) % n));
        fmt_item_text(it, false, false, buf, sizeof(buf));
        corner_set_text(s_item_prev, buf);
        color_unselected(s_item_prev, it);
        pos_prev_corner(s_item_prev);
        /* 右下角这个对象在 PREV 里刚由旧中间项滑来，颜色与文本一并补齐 */
        it = page_item(pg, (uint8_t)((index + 1) % n));
        color_unselected(s_item_next, it);
    }

    slide_end_common();
}

static void slide_finish(void)
{
    const menu_page_t *pg = menu_current_page();
    uint8_t index = menu_get_state()->index;

    /* 只按启动时锁存的方向交接角色；不再 snap_static()（那会把滑出的对象搬回原位） */
    if (s_slide_dir > 0) {
        slide_commit_next(pg, index);
    } else {
        slide_commit_prev(pg, index);
    }

    /* 收尾整屏重绘一次：滑动期间 GC9D01 上可能留下未被覆盖的脏像素
     * （随机位置/长度的虚线），局部失效不保证把这些行包含进去。
     * 整屏失效 → 下一帧全量重绘 → 被正确的背景+文字覆盖。
     * 注：这是"收尾一次"，不是每帧；删掉它会在动画落定后残留虚线（实测）。 */
    lv_obj_invalidate(s_scr);
}





/* ================================================================
 * 图标页（第一级菜单，MiaoUI UI_PAGE_ICON）
 * ================================================================ */

/* ---------- 图标页布局（与 menu.h 的 MENU_ICON_SLOT 等一致） ---------- */
#define MENU_MAX_ICONS     8
#define ICON_SEL_OPA       255      /* 选中：原色 */
#define ICON_NORM_OPA      110      /* 未选中：灰暗（半透明叠背景） */
#define ICON_Y_SEL         8        /* 30 高图标 y（下对齐基础上抬 2：40-30-2=8） */
#define ICON_Y_NORM        16       /* 20 高图标 y（下对齐基础上抬 4：40-20-4=16） */
#define ICON_TOP_Y         0        /* 顶部文字条 y（12px 字体顶格） */
/* 高亮图标中心允许活动的水平范围。
 * 图标不再绑定固定屏幕槽位：打开时选中项中心位于 x=80；
 * 随 NEXT/PREV 自由移动，只在中心越过 [60,110] 时让整条图标带做“最小必要位移”，
 * 因此多数按键只移动高亮，不滚动整条图标带。 */
#define ICON_LEFT_LIMIT     60
#define ICON_RIGHT_LIMIT   110

/* 图标对象（每个图标一个：透明度 PID 动画；尺寸用双位图 src 切换，不走 scale transform） */
typedef struct {
    lv_obj_t *img;
    pid_anim_t pid_opa;
    uint8_t  tgt_opa;
} menu_icon_obj_t;

/* 图标页顶部文字条（图标下对齐后空出的上方空间） */
static lv_obj_t *s_icon_hint;  /* 左：选中项名称指示 */
static lv_obj_t *s_icon_clock; /* 右：时间指示（SD3078 RTC） */

/* 图标页对象池 */
static menu_icon_obj_t s_icons[MENU_MAX_ICONS];
static uint8_t s_icon_count;       /* 当前图标页图标数 */

/* 图标页滚动状态 */
static pid_anim_t s_head_pid;      /* 页面滚动 head_x 动画 */
static int16_t s_head_x_cur;       /* 当前 head_x（整数显示值） */
static int16_t s_head_x_target;    /* 目标 head_x */
static bool s_icon_page;           /* 当前页是否为图标页 */
static bool s_icon_page_was;       /* 上一帧是否图标页（判断进入图标页） */
static bool s_icon_anim_was_active;/* 图标动画上一帧是否在动（结束后整屏重绘清残留） */

/* 按语言返回字体（menu_pages.c 实现，全局共享：菜单渲染 + 主界面都用） */

/* 主题应用（文本页 + 图标页共用）：放在图标页变量定义之后，
 * 因为图标页对象（s_icons / s_icon_hint / s_icon_clock）在本分区声明。
 * pg/index 必须是本次 redraw 实际在用的那一份（不能回头读 menu_current_item()：
 * 页面已切换但菜单状态尚未更新时，它会返回旧页条目，导致新页首项被误染主题色）。 */
static void apply_theme(const menu_page_t *pg, uint8_t index)
{
    const menu_theme_t *t = menu_theme_get();
    uint8_t n = pg ? pg->item_count : 0;
    uint8_t i;

    /* 关键：bg_opa 默认是透明（LV_STYLE_BG_OPA 默认 0），
       必须显式设为 COVER，否则菜单背景不绘制、旧屏像素会透出来叠加成乱码 */
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    /* 字体：按当前语言统一应用（切语言后 redraw 自动换字体） */
    lv_obj_set_style_text_font(s_header,     menu_font_small(), 0);
    lv_obj_set_style_text_font(s_indicator,  menu_font_small(), 0);
    lv_obj_set_style_text_font(s_item_prev,      menu_font_main(), 0);
    lv_obj_set_style_text_font(s_item_next,      menu_font_main(), 0);
    lv_obj_set_style_text_font(s_item_prev_in,   menu_font_main(), 0);
    lv_obj_set_style_text_font(s_item_next_in,   menu_font_main(), 0);
    lv_obj_set_style_text_font(s_item,           menu_font_main(), 0);
    lv_obj_set_style_text_font(s_item_in,        menu_font_main(), 0);
    lv_obj_set_style_text_font(s_icon_hint,  menu_font_small(), 0);
    lv_obj_set_style_text_font(s_icon_clock, menu_font_small(), 0);
    /* 主题色（primary，可改）：菜单名称与页码，使 Color 切换可见 */
    lv_obj_set_style_text_color(s_header, t->primary, 0);
    lv_obj_set_style_text_color(s_indicator, t->primary, 0);
    /* 上一项/下一项（含动画滑入的那两份）：按条目类型上色 ——
     * Return 保持主题色，其余次要灰。 */
    if (n > 1) {
        color_unselected(s_item_prev,    page_item(pg, (uint8_t)((index + n - 1U) % n)));
        color_unselected(s_item_next,    page_item(pg, (uint8_t)((index + 1U) % n)));
        color_unselected(s_item_prev_in, page_item(pg, (uint8_t)((index + n - 1U) % n)));
        color_unselected(s_item_next_in, page_item(pg, (uint8_t)((index + 1U) % n)));
    } else {
        lv_obj_set_style_text_color(s_item_prev,    item_dim_color(), 0);
        lv_obj_set_style_text_color(s_item_next,    item_dim_color(), 0);
        lv_obj_set_style_text_color(s_item_prev_in, item_dim_color(), 0);
        lv_obj_set_style_text_color(s_item_next_in, item_dim_color(), 0);
    }
    /* 当前条目：静止高亮；滑动动画中灰色（防止动画期间任何 redraw 把
     * s_item/s_item_in 刷回高亮，造成新旧选中项同时高亮——
     * 高亮只允许出现在静止态的中间项）。
     * Return（MENU_ITEM_BACK）选中时用主题色 primary，"<" 与文字同一 label 一起变色。 */
    lv_obj_set_style_text_color(s_item,
        s_sliding ? item_dim_color() : item_selected_color(page_item(pg, index)), 0);
    lv_obj_set_style_text_color(s_item_in,
        s_sliding ? item_dim_color() : item_selected_color(page_item(pg, index)), 0);
    /* 图标页顶部文字条：名称与时间始终高亮（主题色 primary） */
    lv_obj_set_style_text_color(s_icon_hint, t->primary, 0);
    lv_obj_set_style_text_color(s_icon_clock, t->primary, 0);
    /* 图标页：A8 图标（透明背景）用 recolor 染成主题文字色 */
    for (i = 0; i < MENU_MAX_ICONS; i++) {
        lv_obj_set_style_image_recolor(s_icons[i].img, t->text, 0);
        lv_obj_set_style_image_recolor_opa(s_icons[i].img, LV_OPA_COVER, 0);
    }
}

/* 循环折叠：raw_x 是图标“中心坐标”，把线性图标带折叠到屏幕中心附近的
 * 最近一个周期。这样末端 About 与首端 Return 可以自然首尾相接，不需要固定槽位。 */
static int16_t icon_fold_center(int32_t raw_x, uint8_t n)
{
    int32_t period = (int32_t)n * MENU_ICON_SLOT;
    int32_t half = MENU_SCR_W / 2;

    if (period <= 0) {
        return (int16_t)raw_x;
    }

    raw_x -= half;
    raw_x %= period;
    if (raw_x < 0) raw_x += period;
    raw_x += half;
    if (raw_x - half > period / 2) raw_x -= period;
    return (int16_t)raw_x;
}

/* 应用所有图标当前位置/透明度/显隐。
 * s_head_x_cur 表示 index 0 图标的中心坐标；MENU_ICON_SLOT 仅表示相邻图标中心距。
 * 38px pitch + 160px 屏宽时，正常可见 5 个图标，左右最外侧各只露出一部分。 */
static void icon_apply_positions(const menu_state_t *st, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        menu_icon_obj_t *ic = &s_icons[i];
        bool sel = (i == st->index);
        int16_t center_x = icon_fold_center(s_head_x_cur + (int32_t)i * MENU_ICON_SLOT, n);
        int16_t size = sel ? MENU_ICON_SEL : MENU_ICON_NORM;
        int16_t y = sel ? ICON_Y_SEL : ICON_Y_NORM;
        int16_t x = center_x - size / 2;
        bool visible = (x + size > 0) && (x < MENU_SCR_W);

        lv_obj_set_pos(ic->img, x, y);
        lv_obj_set_style_opa(ic->img, (lv_opa_t)ic->pid_opa.current, 0);
        if (visible) {
            lv_obj_remove_flag(ic->img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ic->img, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 顶部时间指示：1s 周期刷新。SD3078 不再维护初始化 flag；
 * UI 只读取 load_task 已更新的镜像，镜像不合法时显示 --:--。 */
static void icon_clock_tick(lv_timer_t *t)
{
    char buf[8];
    (void)t;
    if (!s_icon_page) {
        return;
    }
    /* 驱动不再提供 initialized flag：用时间镜像本身的合法范围判断是否已有有效数据。 */
    if (SD3078_ReadMonth() < 1U || SD3078_ReadMonth() > 12U ||
        SD3078_ReadHour() > 23U || SD3078_ReadMin() > 59U) {
        lv_label_set_text(s_icon_clock, "--:--");
        return;
    }
    /* 时间镜像由 load_task 周期更新，UI 只读，不访问 I2C */
    snprintf(buf, sizeof(buf), "%02d:%02d", SD3078_ReadHour(), SD3078_ReadMin());
    lv_label_set_text(s_icon_clock, buf);
}

/* 图标页重绘（redraw 时调用）：初始化/滚动目标/透明度目标/绑定 src */
static void icon_redraw(const menu_state_t *st, const menu_page_t *pg)
{
    uint8_t i, n = pg->item_count;
    int16_t sel_x;

    if (n > MENU_MAX_ICONS) n = MENU_MAX_ICONS;

    /* 顶部文字条：左=选中项名称 */
    {
        const menu_icon_t *mi = &pg->icons[st->index];
        lv_label_set_text(s_icon_hint, (mi && mi->label) ? menu_tr(mi->label) : "");
        label_fit_box(s_icon_hint, 0U);   /* 图标名超出剩宽才截断（顶部文字条，右侧留给时间） */
    }

    /* 刚进入图标页（打开菜单/页面切换）：head_x 定位到保存值，图标直接到位 */
    if (!s_icon_page_was) {
        s_head_x_cur = s_head_x_target = pg->head_x;
        pid_init(&s_head_pid, (float)s_head_x_cur, (float)s_head_x_target);
        for (i = 0; i < n; i++) {
            menu_icon_obj_t *ic = &s_icons[i];
            bool sel = (i == st->index);
            ic->tgt_opa = sel ? ICON_SEL_OPA : ICON_NORM_OPA;
            pid_init(&ic->pid_opa, (float)ic->tgt_opa, (float)ic->tgt_opa);
        }
    }

    /* 最小位移滚动：
     * 选中图标的中心允许自由处于 [40,120]。按键后若仍在范围内，图标带完全不滚；
     * 若越界，只补偿“超出的那几像素”，把高亮中心刚好钳到边界，而不是整格滚动。
     * 因此屏幕中心 x=80 只用于初始位置，随后不会强迫高亮重新居中。 */
    if (s_icon_page_was && s_last_index != st->index) {
        int16_t delta = 0;
        sel_x = icon_fold_center(s_head_x_target + (int32_t)st->index * MENU_ICON_SLOT, n);

        if (sel_x > ICON_RIGHT_LIMIT) {
            delta = (int16_t)(ICON_RIGHT_LIMIT - sel_x);
        } else if (sel_x < ICON_LEFT_LIMIT) {
            delta = (int16_t)(ICON_LEFT_LIMIT - sel_x);
        }

        if (delta != 0) {
            s_head_x_target += delta;
            menu_page_set_head_x(s_head_x_target);
            pid_init(&s_head_pid, (float)s_head_x_cur, (float)s_head_x_target);
        }
    }

    /* 透明度目标：选中渐变变亮；取消选中立即变暗（避免快速切换时
       新旧图标同时处于中间亮度 = 多高亮）。尺寸由 src 双位图切换。 */
    for (i = 0; i < n; i++) {
        menu_icon_obj_t *ic = &s_icons[i];
        bool sel = (i == st->index);
        uint8_t topa = sel ? ICON_SEL_OPA : ICON_NORM_OPA;
        if (ic->tgt_opa != topa) {
            ic->tgt_opa = topa;
            if (sel) {
                /* 新选中：从当前亮度渐变到全亮 */
                pid_init(&ic->pid_opa, ic->pid_opa.current, (float)topa);
            } else {
                /* 取消选中：立即置为灰暗，不经过中间态 */
                pid_init(&ic->pid_opa, (float)topa, (float)topa);
            }
        }
    }

    /* 绑定图标 src（对象池复用）：选中 30x30，未选中 20x20 */
    for (i = 0; i < n; i++) {
        const menu_icon_t *mi = &pg->icons[i];
        const void *want = (i == st->index) ? mi->icon : mi->icon_small;
        if (want && lv_image_get_src(s_icons[i].img) != want) {
            lv_image_set_src(s_icons[i].img, want);
        }
        lv_obj_remove_flag(s_icons[i].img, LV_OBJ_FLAG_HIDDEN);
    }
    for (i = n; i < s_icon_count; i++) {
        lv_obj_add_flag(s_icons[i].img, LV_OBJ_FLAG_HIDDEN);
    }
    s_icon_count = n;

    /* 立即应用（打开首帧直接到位；动画期间也保证对象与状态一致） */
    icon_apply_positions(st, n);
}

/* 由 10ms 定时器驱动：图标页 head_x 滚动 + 各图标透明度 PID 步进
 * 全部静止时直接返回，不产生任何 LVGL 调用（避免菜单页长时间每 10ms 空转重绘） */
static void icon_anim_tick(void)
{
    const menu_state_t *st = menu_get_state();
    const menu_page_t *pg = menu_current_page();
    uint8_t i, n;
    bool any_active;

    if (!s_icon_page || !pg) {
        return;
    }
    n = (pg->item_count < MENU_MAX_ICONS) ? pg->item_count : MENU_MAX_ICONS;

    any_active = s_head_pid.active;
    for (i = 0; i < n; i++) {
        if (s_icons[i].pid_opa.active) any_active = true;
    }
    if (!any_active) {
        /* 动画刚结束：整屏强制重绘一次，清除图标边缘/槽位区域的残留像素
         * （GC9D01 部分刷新下，图标上下边界可能留下未覆盖的灰点/格点） */
        if (s_icon_anim_was_active) {
            lv_obj_invalidate(s_scr);
        }
        s_icon_anim_was_active = false;
        return;
    }
    s_icon_anim_was_active = true;

    if (s_head_pid.active) {
        pid_step(&s_head_pid);
        s_head_x_cur = (int16_t)s_head_pid.current;
    }
    for (i = 0; i < n; i++) {
        pid_step(&s_icons[i].pid_opa);
    }
    icon_apply_positions(st, n);
}

/* 由 10ms 定时器驱动：PID 步进 + 应用位置 */
static void menu_ui_anim_tick(lv_timer_t *t)
{
    (void)t;
    /* 文本页整体斜向滚动：中间项到角落 + 新项滑入中间。
     * 所有对象从各自起点立即移动（不做延迟，避免角落留下静止图标）：
     *  - 旧中间项 s_item 沿对角线滑向角落；
     *  - 新中间项 s_item_in 从对侧角落滑入中间；
     *  - 角落新项从屏外水平滑入对应角落。 */
    if (s_sliding) {
        pid_step(&s_pid_out);
        pid_step(&s_pid_out_y);
        pid_step(&s_pid_in);
        pid_step(&s_pid_in_y);
        lv_obj_set_pos(s_item,    (int32_t)s_pid_out.current,   (int32_t)s_pid_out_y.current);
        lv_obj_set_pos(s_item_in, (int32_t)s_pid_in.current,    (int32_t)s_pid_in_y.current);
    }
    /* 角落 prev/next：旧上一/下一项水平出屏（30px/帧：先于滑入项覆盖角落前
     * 快速清空，全程可见不失效）+ 上上/下下一项屏外水平滑入（立即移动）。 */
    if (s_prev_out_x.active) {
        pid_step_lim(&s_prev_out_x, 30.0f);
        lv_obj_set_x(s_item_prev, (int32_t)s_prev_out_x.current);
    }
    if (s_next_out_x.active) {
        pid_step_lim(&s_next_out_x, 30.0f);
        lv_obj_set_x(s_item_next, (int32_t)s_next_out_x.current);
    }
    if (s_prev_in_x.active) {
        pid_step(&s_prev_in_x);
        lv_obj_set_x(s_item_prev_in, (int32_t)s_prev_in_x.current);
    }
    if (s_next_in_x.active) {
        pid_step(&s_next_in_x);
        lv_obj_set_x(s_item_next_in, (int32_t)s_next_in_x.current);
    }
    if (s_sliding &&
        !s_pid_out.active && !s_pid_out_y.active && !s_pid_in.active && !s_pid_in_y.active &&
        !s_prev_out_x.active && !s_next_out_x.active &&
        !s_prev_in_x.active && !s_next_in_x.active) {
        slide_finish();
    }
    /* 图标页滚动/缩放/透明度动画 */
    if (s_icon_page) {
        icon_anim_tick();
    }
}

/* ---------- 创建 / 重绘 ---------- */
lv_obj_t *menu_ui_create(void)
{
    int32_t one_line;   /* 一行文字的高度：所有文本 label 都锁成一行高 */

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_size(s_scr, MENU_SCR_W, MENU_SCR_H);

    /* 兜底防"竖排"：文本 label 的 box 高度固定为一行。即使文本因任何原因被判定
     * 需要换行，也只会裁掉多出来的行，不会画出多行/竖排的字。 */
    one_line = lv_font_get_line_height(lv_obj_get_style_text_font(s_scr, LV_PART_MAIN));
    if (one_line < 1) {
        one_line = 14;
    }

    /* 文本页对角线布局（全部主界面字体 = LV_FONT_DEFAULT = jetbrains_mono_14） */
    s_header = lv_label_create(s_scr);
    lv_obj_set_pos(s_header, MENU_CORNER_X - 1, MENU_ROW_BOT_Y - 1);   /* 左下：菜单名称（左/上各 1px） */
    lv_obj_set_style_text_font(s_header, &lv_font_montserrat_12, 0);
    lv_obj_set_height(s_header, one_line);
    lv_label_set_text(s_header, "");

    s_indicator = lv_label_create(s_scr);
    lv_obj_align(s_indicator, LV_ALIGN_TOP_RIGHT, -3, 0);      /* 右上：页码/EDIT */
    lv_obj_set_style_text_font(s_indicator, &lv_font_montserrat_12, 0);
    lv_obj_set_height(s_indicator, one_line);
    lv_label_set_text(s_indicator, "");

    s_item_prev = lv_label_create(s_scr);
    lv_obj_set_pos(s_item_prev, MENU_CORNER_X, MENU_ROW_TOP_Y);      /* 左上：上一项（灰） */
    lv_label_set_long_mode(s_item_prev, LV_LABEL_LONG_MODE_DOTS);    /* 角落：超屏宽才截断 */
    lv_obj_set_height(s_item_prev, one_line);
    corner_set_text(s_item_prev, "");

    s_item_next = lv_label_create(s_scr);
    lv_obj_set_pos(s_item_next, MENU_SCR_W, MENU_ROW_BOT_Y);    /* 右下：下一项（灰，redraw 时正确定位） */
    lv_label_set_long_mode(s_item_next, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_item_next, one_line);
    corner_set_text(s_item_next, "");

    /* 角落滑入标签：动画期间从屏外滑入（下下一项/上上一项），与角落同层 */
    s_item_next_in = lv_label_create(s_scr);
    lv_obj_set_pos(s_item_next_in, MENU_SCR_W, MENU_ROW_BOT_Y);
    lv_label_set_long_mode(s_item_next_in, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_item_next_in, one_line);
    corner_set_text(s_item_next_in, "");
    lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);

    s_item_prev_in = lv_label_create(s_scr);
    lv_obj_set_pos(s_item_prev_in, -MENU_SCR_W, MENU_ROW_TOP_Y);
    lv_label_set_long_mode(s_item_prev_in, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_item_prev_in, one_line);
    corner_set_text(s_item_prev_in, "");
    lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);

    /* 中间项：后创建 = 顶层。动画中滑向角落的中间项始终盖住同角落
     * 滑出的旧角落项 → 交汇处无重影/重叠（若角落在上层，PREV 时中间项
     * 滑到右下会被滑出的旧下一项盖住，形成双重影）。
     * 中间项用 SCROLL_CIRCULAR：超过 14 个英文宽时循环滚动（滚动条已由
     * 主题样式关闭，见 apply_theme）。 */
    s_item = lv_label_create(s_scr);
    lv_obj_set_pos(s_item, 0, MENU_ROW_MID_Y);                    /* 中间：当前项（x/y 由动画/居中控制） */
    lv_label_set_long_mode(s_item, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_height(s_item, one_line);
    lv_label_set_text(s_item, "");

    s_item_in = lv_label_create(s_scr);
    lv_obj_set_pos(s_item_in, 0, MENU_ROW_MID_Y);
    lv_label_set_long_mode(s_item_in, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_height(s_item_in, one_line);
    lv_label_set_text(s_item_in, "");
    lv_obj_add_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);

    /* 图标页顶部文字条（图标下对齐后空出的上方空间，12px 高亮文字） */
    s_icon_hint = lv_label_create(s_scr);
    lv_obj_set_pos(s_icon_hint, 3, ICON_TOP_Y);
    lv_obj_set_style_text_font(s_icon_hint, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_icon_hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_icon_hint, one_line);
    lv_label_set_text(s_icon_hint, "");
    lv_obj_add_flag(s_icon_hint, LV_OBJ_FLAG_HIDDEN);

    s_icon_clock = lv_label_create(s_scr);
    lv_obj_align(s_icon_clock, LV_ALIGN_TOP_RIGHT, -3, ICON_TOP_Y);
    lv_obj_set_style_text_font(s_icon_clock, &lv_font_montserrat_12, 0);
    lv_obj_set_height(s_icon_clock, one_line);
    lv_label_set_text(s_icon_clock, "--:--");
    lv_obj_add_flag(s_icon_clock, LV_OBJ_FLAG_HIDDEN);

    /* 图标页对象池（预创建 A8 位图，默认隐藏；颜色由主题控制，尺寸用双位图 src 切换） */
    for (uint8_t i = 0; i < MENU_MAX_ICONS; i++) {
        s_icons[i].img = lv_image_create(s_scr);
        lv_image_set_src(s_icons[i].img, NULL);
        lv_obj_set_style_opa(s_icons[i].img, ICON_SEL_OPA, 0);
        lv_obj_add_flag(s_icons[i].img, LV_OBJ_FLAG_HIDDEN);
        s_icons[i].tgt_opa = ICON_SEL_OPA;
    }

    /* PID 动画驱动定时器 */
    s_anim_timer = lv_timer_create(menu_ui_anim_tick, MENU_PID_ANIM_MS, NULL);
    /* 顶部时间指示（1s 刷新） */
    lv_timer_create(icon_clock_tick, 1000, NULL);

    menu_ui_redraw();
    return s_scr;
}

void menu_ui_redraw(void)
{
    const menu_state_t *st = menu_get_state();
    const menu_page_t  *pg = menu_current_page();
    bool changed, page_changed, icon_page;
    char buf[32];

    /* 用本次 redraw 的 (pg, index) 上色：不能让它自己回头读 menu_current_item()，
     * 否则页面已切换、菜单状态未更新的窗口里会取到旧页条目（曾导致 Settings→
     * PowerBank 首项被误染主题色）。
     * s_last_page 必须先跟上本帧的页：下面 page_changed 靠它判断，若这里还留着
     * 上一页，本次进入新页会被当成"同页变化"，snap_static 不执行 → 中间项文本与
     * 颜色停在上一页内容（返回子页后"又是白色/颜色不刷新"就是这个原因）。 */
    s_last_page = pg;
    apply_theme(pg, st->index);

    icon_page = st->active && pg && pg->type == MENU_PAGE_ICON
                && pg->icons && pg->item_count > 0;

    if (!st->active) {
        pid_stop_all();           /* 菜单关闭：停止一切残留动画 */
        s_sliding = false;
        /* 隐藏全部滑入对象，防止菜单关闭后残留显示“不动的图标” */
        lv_obj_add_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
        s_last_active = false;
        s_icon_page_was = false;
        return;
    }

    if (!s_last_active) {
        s_first_after_open = true;
    }

    /* 文本页元素与图标页元素互斥显隐 */
    if (icon_page) {
        lv_obj_add_flag(s_header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_prev_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_item_next_in, LV_OBJ_FLAG_HIDDEN);
        /* 图标页：显示顶部文字条（名称+时间） */
        lv_obj_remove_flag(s_icon_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_icon_clock, LV_OBJ_FLAG_HIDDEN);
        /* 必须显式停掉滑动与四个角落 PID：menu_ui_anim_tick 里角落 PID 的执行
         * 只判各自的 .active，不判 s_sliding——只清标志的话，文本页切走之后
         * 已被隐藏的文本对象仍会被动画定时器继续搬动（位置/显隐残留）。
         * 中断方向也一并清掉，避免下次进文本页沿用旧方向收尾。 */
        if (s_sliding || s_prev_out_x.active || s_next_out_x.active ||
            s_prev_in_x.active || s_next_in_x.active) {
            pid_stop_all();
            s_sliding = false;
            s_slide_dir = 0;
        }
    } else {
        lv_obj_remove_flag(s_header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_indicator, LV_OBJ_FLAG_HIDDEN);
        /* 只无条件显示静止态的 3 个文本对象（左上/中间/右下）。
         * s_item_prev_in / s_item_in / s_item_next_in 是动画专用临时对象，
         * 绝不能在这里 unhide：否则动画途中（不同任务/定时器触发的 redraw）
         * 会把 slide_start 刚隐藏的滑入对象连文本一起显示出来，形成"完整的旧字符"
         * 残影。它们只允许由 slide_start / slide_commit_* / snap_static 控制显隐。 */
        lv_obj_remove_flag(s_item_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_item,      LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_item_next, LV_OBJ_FLAG_HIDDEN);
        /* 文本页：隐藏图标页文字条 */
        lv_obj_add_flag(s_icon_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_icon_clock, LV_OBJ_FLAG_HIDDEN);
    }

    if (icon_page) {
        icon_redraw(st, pg);
        s_icon_page = true;
    } else {
        s_icon_page = false;
        /* 隐藏残留图标（图标页 → 文本页切换） */
        for (uint8_t i = 0; i < s_icon_count; i++) {
            lv_obj_add_flag(s_icons[i].img, LV_OBJ_FLAG_HIDDEN);
        }
        s_icon_count = 0;

        /* 标题/页码/上一项/下一项不参与滑动，总是直接刷新 */
        lv_label_set_text(s_header, (pg && pg->title) ? menu_tr(pg->title) : "");
        if (st->editing) {
            lv_label_set_text(s_indicator, menu_tr("EDIT"));
        } else {
            snprintf(buf, sizeof(buf), "%d/%d", (int)st->index + 1,
                     pg ? (int)pg->item_count : 0);
            lv_label_set_text(s_indicator, buf);
        }
        /* 角落 prev/next 的静止位定位只允许在静止态做：动画中（s_sliding）调用
         * refresh_prev_next 会把正在滑动的角落项（滑出/滑入途中）瞬移到静止位
         * 并停留，产生"绘图过程中短暂出现的静止位置的图标"（影子/闪烁）。
         * 动画中角落位置由 slide_start 的 PID 接管，动画结束由 snap_static 统一。 */
        if (!s_sliding) {
            refresh_prev_next(pg, st->index);
        }

        page_changed = (s_last_page != pg);
        changed = page_changed || (s_last_index != st->index);

        if (s_first_after_open) {
            /* 打开首帧：直接显示，不滑动 */
            snap_static(pg, st->index);
            s_first_after_open = false;
        } else if (changed) {
            /* 只有"上一帧和本帧是同一页、且上一帧索引在本页范围内"才做滑动动画。
             * 若页变了、或上一帧索引比本页条目数还大（状态机刚更新、缓存滞后），
             * 说明两者不同源，此时滑动会用到越界/错页的条目，改走 snap_static 重建。 */
            bool slide_ok = !page_changed && (s_last_index < (pg ? pg->item_count : 0));
            if (!slide_ok) {
                /* 页面切换（进入/返回子页）或状态不同源：直接刷新，不做滑动动画
                 * （避免把上一页内容作为滑出项，如 Status 的电池信息） */
                if (pg == &menu_page_time) {
                    /* 进入时间页：从 load_task 镜像读当前时间，填充时间设置变量 */
                    menu_time_read();
                }
                snap_static(pg, st->index);
            } else {
                /* 同页条目切换：斜向滚动
                 * 方向由菜单状态机提供：循环滚动时按索引比较（如 5→0）会判错方向。
                 * 若上次动画未完成（动画中被按键打断）：先把整屏硬快照到"本次按键前"
                 * 的静止态（s_last_index = 上次 redraw 的目标索引），再开始新滑动，
                 * 保证滑出/滑入项与角落都从一致状态出发，杜绝文字错乱与残留动画。 */
                if (s_sliding) {
                    snap_static(s_last_page, s_last_index);
                }
                slide_start((st->nav_dir != 0) ? st->nav_dir : 1);
            }
        } else if (!s_sliding) {
            /* 值更新/主题变化等：原地刷新（滑动中不打断） */
            snap_static(pg, st->index);
        }
    }

    s_last_page = pg;
    s_last_index = st->index;
    s_last_active = true;
    s_icon_page_was = icon_page;
}

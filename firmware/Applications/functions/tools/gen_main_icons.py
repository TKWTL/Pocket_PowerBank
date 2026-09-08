#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_main_icons.py - 主界面 A8 位图图标生成器（箭头 / 电池轮廓）

纯 Python 几何算法绘制（不依赖 PIL），输出 LVGL v9 A8 位图 C 文件：
  - main_icon_arrow   (24x14, 指向右)；运行时用 lv_image_set_scale_x(-1000) 镜像为指向左
  - main_icon_battery (50x24, 中空电池轮廓 + 右侧正极凸起)；内部放电量百分比文字

用法（在 firmware 目录下）：
  python Applications/functions/tools/gen_main_icons.py
生成：Applications/functions/main_icons.c / main_icons.h
"""

import os

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT_C = os.path.join(OUT_DIR, "main_icons.c")
OUT_H = os.path.join(OUT_DIR, "main_icons.h")

# ---------------------------------------------------------------- 像素算法 ---

def arrow_pixel(x, y, w=14, h=14):
    """指向右的小箭头（约一个字符大小）：左段柄(厚4px)，右段三角头"""
    mid = h // 2                       # 垂直中心 y=7
    if x < 9:                          # 柄: x=0..8
        return 255 if (mid - 2 <= y <= mid + 1) else 0
    # 三角头: x=9..13, 在 x=9 收敛为顶点, x=13 全高
    t = x - 9                          # 0..4
    half = (t * (h // 2)) // 4         # 0..7
    return 255 if (mid - half <= y <= mid + half) else 0

def arrow_l_pixel(x, y, w=14, h=14):
    """指向左的箭头（arrow_pixel 水平镜像）；LVGL9 不支持负 scale_x 镜像，故预生成两个方向"""
    return arrow_pixel(w - 1 - x, y, w, h)

def battery_pixel(x, y, w=54, h=24):
    """横置电池轮廓：主体 x=0..51 外框厚3px，右侧正极凸起 x=52..53；内部宽 46px 可放下 "100%"""
    if x >= 52:                        # 正极凸起
        return 255 if (8 <= y <= 15) else 0
    if x < 0 or x >= 52 or y < 0 or y >= h:
        return 0
    # 主体外框 3px（上/下/左/右），其余中空
    if y < 3 or y >= h - 3 or x < 3 or x >= 49:
        return 255
    return 0

# ------------------------------------------------------------ C 代码生成 ---

def gen_c_array(name, w, h, pixel_fn):
    """把像素函数转成 16 字节一行的 C 数组文本"""
    lines = []
    for y in range(h):
        row = []
        for x in range(w):
            row.append(pixel_fn(x, y))
        for i in range(0, w, 16):
            chunk = row[i:i + 16]
            lines.append("    " + ", ".join("0x%02X" % b for b in chunk) + ",")
    return "\n".join(lines)

def gen_c_file():
    arrow_r_data = gen_c_array("arrow_r", 14, 14, arrow_pixel)
    arrow_l_data = gen_c_array("arrow_l", 14, 14, arrow_l_pixel)
    battery_data = gen_c_array("battery", 54, 24, battery_pixel)

    c = """/* main_icons.c - 主界面图标(LVGL v9 A8 位图, 由 tools/gen_main_icons.py 自动生成) */
/* 箭头: 14x14 (指向右 arrow_r / 指向左 arrow_l)，LVGL9 不支持负 scale 镜像，故预生成两个方向 */
/* 电池: 54x24 中空轮廓(内部放电量百分比文字), 右侧为正极凸起 */
/* A8: 255=前景(配合 lv_obj_set_style_image_recolor 换主题色), 0=透明 */
#include "lvgl.h"
#include "main_icons.h"

LV_ATTRIBUTE_MEM_ALIGN static const uint8_t main_icon_arrow_r_data[] = {
%s
};

const lv_image_dsc_t main_icon_arrow_r = {
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.w = 14,
    .header.h = 14,
    .data_size = sizeof(main_icon_arrow_r_data),
    .data = main_icon_arrow_r_data,
};

LV_ATTRIBUTE_MEM_ALIGN static const uint8_t main_icon_arrow_l_data[] = {
%s
};

const lv_image_dsc_t main_icon_arrow_l = {
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.w = 14,
    .header.h = 14,
    .data_size = sizeof(main_icon_arrow_l_data),
    .data = main_icon_arrow_l_data,
};

LV_ATTRIBUTE_MEM_ALIGN static const uint8_t main_icon_battery_data[] = {
%s
};

const lv_image_dsc_t main_icon_battery = {
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.w = 54,
    .header.h = 24,
    .data_size = sizeof(main_icon_battery_data),
    .data = main_icon_battery_data,
};
""" % (arrow_r_data, arrow_l_data, battery_data)

    h = """/* main_icons.h - 主界面图标(LVGL v9 A8 位图, 由 tools/gen_main_icons.py 自动生成) */
#ifndef MAIN_ICONS_H
#define MAIN_ICONS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* main_icon_arrow_r (14x14, A8) 指向右 */
extern const lv_image_dsc_t main_icon_arrow_r;

/* main_icon_arrow_l (14x14, A8) 指向左 */
extern const lv_image_dsc_t main_icon_arrow_l;

/* main_icon_battery (54x24, A8) 电池轮廓（中空，内部放电量百分比文字） */
extern const lv_image_dsc_t main_icon_battery;

#ifdef __cplusplus
}
#endif

#endif /* MAIN_ICONS_H */
"""
    # 修正头文件注释里的笔误（避免 "l_v_")
    h = h.replace("l_v_image_set_scale_x", "lv_image_set_scale_x")

    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write(c)
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write(h)
    print("written:", OUT_C)
    print("written:", OUT_H)

if __name__ == "__main__":
    gen_c_file()

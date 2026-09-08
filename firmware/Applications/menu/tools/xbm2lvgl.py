#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xbm2lvgl.py - 把 MiaoUI 的 XBM 图标(30x30)转换为 LVGL v9 A8 图像描述符。

用法:
    python xbm2lvgl.py

输入:  firmware/Applications/ui/images/image.c 中的 XBM 数组
输出:  firmware/Applications/menu/menu_icons.c / menu_icons.h

格式说明:
    - LVGL v9 的 I1 渲染不查 palette(按 0=黑 1=白 灰度渲染且背景不透明),
      不满足"透明背景 + 主题色"的需求。
    - 改用 A8(8-bit alpha): 1 像素=0xFF(不透明), 0 像素=0x00(透明),
      渲染时以 recolor 颜色作为图标颜色(随主题变色), 透明背景无方块。
    - 每个图标生成两个尺寸: 30x30(选中, 900B) + 20x20(未选中, 400B)。
    - ⚠️ 不要用 lv_image_set_scale 做缩放: A8 缩放走 transform 路径,
      动画期间每帧 lv_malloc 缓冲, 24KB LVGL 堆易碎片/分配失败 ->
      LV_ASSERT_MALLOC 死循环(卡住) / 空指针 HardFault(复位)。
      用双尺寸位图直接换 src 最稳。
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "ui", "images", "image.c")
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_C = os.path.join(OUT_DIR, "menu_icons.c")
OUT_H = os.path.join(OUT_DIR, "menu_icons.h")

# 根页图标映射: (XBM 数组名, LVGL 符号名, 是否反色)
# img_toby_fox 是白底黑狐狸(前景 72%), 其余图标是黑底白形(前景 <30%), 需反色
WANT = [
    ("img_home",           "menu_icon_return",   False),
    ("img_configuration",  "menu_icon_settings", False),
    ("img_statistics",     "menu_icon_status",   False),
    ("img_tools",          "menu_icon_tools",    False),
    ("img_games",          "menu_icon_games",    False),
    ("img_toby_fox",       "menu_icon_about",    True),
]

ICON_W, ICON_H = 30, 30
ROW_BYTES = (ICON_W + 7) // 8          # 4 字节/行
EXPECTED = ROW_BYTES * ICON_H          # 120 字节


def reverse_byte(b):
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4)
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2)
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1)
    return b


def xbm_to_a8(raw, size=30, invert=False):
    """XBM(LSB-first, 30x30) -> A8 字节流(每像素 1 字节 alpha), 可输出 30 或 20 尺寸
    缩小用最近邻采样: 目标像素 (row,col) 对应源 (row*30/size, col*30/size)。
    invert=True: 反色(白底黑形 -> 黑形白底), 用于 img_toby_fox。"""
    out = []
    for row in range(size):
        sy = row * ICON_H // size
        for col in range(size):
            sx = col * ICON_W // size
            byte = raw[sy * ROW_BYTES + sx // 8]
            bit = (byte >> (sx % 8)) & 1
            if invert:
                bit = 1 - bit
            out.append(0xFF if bit else 0x00)
    return out


def parse_xbm(path):
    """解析 image.c 中所有 const unsigned char img_xxx[] = {...}; 数组"""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()
    found = {}
    pat = re.compile(
        r"const\s+unsigned\s+char\s+(img_\w+)\s*\[\]\s*=\s*\{(.*?)\};",
        re.S)
    for m in pat.finditer(text):
        nums = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(2))]
        found[m.group(1)] = nums
    return found


def fmt_bytes(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + per_line])
        lines.append("    " + chunk + ",")
    return "\n".join(lines)


def main():
    src = parse_xbm(SRC)
    missing = [n for n, _, _ in WANT if n not in src]
    if missing:
        print("缺少图标:", missing)
        sys.exit(1)

    c_lines = []
    h_lines = []
    h_lines.append("/* menu_icons.h - 根页图标(LVGL v9 A8 位图, 由 tools/xbm2lvgl.py 自动生成) */")
    h_lines.append("#ifndef MENU_ICONS_H")
    h_lines.append("#define MENU_ICONS_H")
    h_lines.append("")
    h_lines.append('#include "lvgl.h"')
    h_lines.append("")
    h_lines.append("#ifdef __cplusplus")
    h_lines.append('extern "C" {')
    h_lines.append("#endif")
    h_lines.append("")

    c_lines.append("/* menu_icons.c - 根页图标(LVGL v9 A8 位图, 由 tools/xbm2lvgl.py 自动生成) */")
    c_lines.append('/* 源素材: Applications/ui/images/image.c (MiaoUI XBM 30x30) */')
    c_lines.append('/* A8: 1=不透明(前景), 0=透明背景; 渲染时用 lv_image_set_recolor 换主题色 */')
    c_lines.append('#include "lvgl.h"')
    c_lines.append('#include "menu_icons.h"')
    c_lines.append("")

    for xbm_name, sym, invert in WANT:
        raw = src[xbm_name]
        if len(raw) != EXPECTED:
            print("警告: %s 大小 %d != %d, 跳过" % (xbm_name, len(raw), EXPECTED))
            continue
        for size, suffix in ((30, ""), (20, "_small")):
            data = xbm_to_a8(raw, size, invert)
            c_lines.append("LV_ATTRIBUTE_MEM_ALIGN static const uint8_t %s%s_data[] = {" % (sym, suffix))
            c_lines.append(fmt_bytes(data))
            c_lines.append("};")
            c_lines.append("")
            c_lines.append("const lv_image_dsc_t %s%s = {" % (sym, suffix))
            c_lines.append("    .header.cf = LV_COLOR_FORMAT_A8,")
            c_lines.append("    .header.w = %d," % size)
            c_lines.append("    .header.h = %d," % size)
            c_lines.append("    .data_size = sizeof(%s%s_data)," % (sym, suffix))
            c_lines.append("    .data = %s%s_data," % (sym, suffix))
            c_lines.append("};")
            c_lines.append("")
            h_lines.append("/* %s%s (%dx%d, A8) */" % (sym, suffix, size, size))
            h_lines.append("extern const lv_image_dsc_t %s%s;" % (sym, suffix))
            h_lines.append("")

    h_lines.append("#ifdef __cplusplus")
    h_lines.append("}")
    h_lines.append("#endif")
    h_lines.append("")
    h_lines.append("#endif /* MENU_ICONS_H */")

    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))
    print("生成完成:")
    print("  " + OUT_C)
    print("  " + OUT_H)


if __name__ == "__main__":
    import sys
    main()

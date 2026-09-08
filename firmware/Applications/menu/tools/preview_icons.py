# -*- coding: utf-8 -*-
"""验证 XBM 图标渲染形状（ASCII 预览）"""
import re
import sys

SRC = r"E:\Embedded_Full_ENG_Path\Pocket_PowerBank\firmware\Applications\ui\images\image.c"

with open(SRC, encoding="utf-8", errors="ignore") as f:
    text = f.read()
pat = re.compile(r"const\s+unsigned\s+char\s+(img_\w+)\s*\[\]\s*=\s*\{(.*?)\};", re.S)
found = {}
for m in pat.finditer(text):
    found[m.group(1)] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(2))]

def render(name, size, invert=False):
    raw = found[name]
    lines = []
    for r in range(size):
        line = ""
        for c in range(size):
            sx = c * 30 // size
            sy = r * 30 // size
            b = raw[sy * 4 + sx // 8]
            bit = ((b >> (sx % 8)) & 1)
            if invert:
                bit = 1 - bit
            line += "#" if bit else "."
        lines.append(line)
    return lines

names = sys.argv[1:] if len(sys.argv) > 1 else ["img_configuration", "img_toby_fox", "img_home"]

for name in names:
    print("== %s 30x30 ==" % name)
    for l in render(name, 30):
        print(l)
    print("== %s 20x20 ==" % name)
    for l in render(name, 20):
        print(l)
    print()

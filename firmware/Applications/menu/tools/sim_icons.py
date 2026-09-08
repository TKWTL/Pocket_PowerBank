# -*- coding: utf-8 -*-
"""模拟图标菜单滚动/折叠逻辑，验证所有导航场景"""

MENU_SCR_W = 160
ICON_SLOT = 40
ICON_LEFT_LIMIT = MENU_SCR_W // 2 - ICON_SLOT    # 40 槽位左边界(图标中心60)
ICON_RIGHT_LIMIT = MENU_SCR_W // 2             # 80 槽位左边界(图标中心100)
N = 6

def fold(raw_x):
    period = N * ICON_SLOT
    half = MENU_SCR_W // 2
    raw_x -= half
    raw_x %= period
    if raw_x < 0:
        raw_x += period
    raw_x += half
    if raw_x - half > period // 2:
        raw_x -= period
    return raw_x

def icon_head_center(idx):
    return MENU_SCR_W // 2 - ICON_SLOT - idx * ICON_SLOT
def nav(head, idx, nav_dir, last_idx):
    """模拟一次按键后的滚动。返回新 head。"""
    if idx == last_idx:
        return head, head
    sel_x = fold(head + idx * ICON_SLOT)
    # 方向感知的中间 2 槽位约束
    if (nav_dir > 0 and sel_x > ICON_RIGHT_LIMIT) or (nav_dir < 0 and sel_x < ICON_LEFT_LIMIT):
        if nav_dir > 0:
            head -= ICON_SLOT
        else:
            head += ICON_SLOT
    return head, sel_x

def show(head, sel):
    print("  head_x=%d 选中=%s" % (head, sel))
    for i in range(N):
        x = fold(head + i * ICON_SLOT)
        vis = (x + 30 > 0) and (x < MENU_SCR_W)
        mark = " *" if i == sel else "  "
        if vis:
            print("    [%d]%s x=%-4d %s" % (i, mark, x, "选中30" if i == sel else "20"))
        else:
            print("    [%d]%s x=%-4d (隐藏)" % (i, mark, x))

# 场景 1: 进入菜单 home(0) 居中
print("== 进入菜单: index=0, head=icon_head_center(0) ==")
head = icon_head_center(0)
show(head, 0)

# 场景 2: PREV home->about (循环, 一步切换, about 在左侧)
print("\n== PREV: 0 -> 5 (about 应在左侧, 不滚) ==")
head, selx = nav(head, 5, -1, 0)
show(head, 5)

# 场景 3: NEXT 5 -> 0 (循环)
print("\n== NEXT: 5 -> 0 (home 应在 about 右侧) ==")
head, selx = nav(head, 0, 1, 5)
show(head, 0)

# 场景 4: 连续 NEXT 0->1->2->3->4->5
print("\n== 连续 NEXT 0->1->2->3->4->5 ==")
head = icon_head_center(0)
last = 0
for nxt in [1, 2, 3, 4, 5]:
    head, selx = nav(head, nxt, 1, last)
    print("  0->%d: head=%d, 选中槽位=%d" % (nxt, head, selx))
    last = nxt
show(head, 5)

# 场景 5: 连续 PREV 5->4->3->2->1->0
print("\n== 连续 PREV 5->4->3->2->1->0 ==")
last = 5
for nxt in [4, 3, 2, 1, 0]:
    head, selx = nav(head, nxt, -1, last)
    print("  5->%d: head=%d, 选中槽位=%d" % (nxt, head, selx))
    last = nxt
show(head, 0)

# 场景 6: 从 settings(1) 到 status(2)（用户之前报"跳到工具项"的场景）
print("\n== 0 -> 1 -> 2 (设置->状态) ==")
head = icon_head_center(0)
last = 0
for nxt in [1, 2]:
    head, selx = nav(head, nxt, 1, last)
    print("  ->%d: head=%d, 选中槽位=%d" % (nxt, head, selx))
    last = nxt
show(head, 2)

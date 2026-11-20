#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 docs/assets/img/og.png（1200×630 社交分享图）。

设计语言与 docs/banner.svg / docs/index.html 一致：纸色 #F4F1EA / 墨色 #17130F / 锈红 #A9502B。
3 倍超采样绘制后 LANCZOS 缩到 1200×630，保证中文小字与圆角干净。

用法（仓库根目录）：
    python tools/gen_site_og.py

依赖：Pillow；Windows 自带字体（msyh.ttc / msyhbd.ttc / segoeui.ttf / seguisb.ttf）。
换机器时把 FONTS 指向对应中文字体；本脚本只用于介绍页素材，不参与板端构建。
前置：docs/assets/img/lcd-main.png 需先由主机版 `--dump-bmp` 导出并转 PNG。
"""
import os

from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LCD = os.path.join(ROOT, "docs", "assets", "img", "lcd-main.png")
OUT = os.path.join(ROOT, "docs", "assets", "img", "og.png")

W, H = 1200, 630
S = 3  # 超采样倍数

# —— 色板（与站点/banner 一致）——
PAPER = (244, 241, 234)   # F4F1EA
INK = (23, 19, 15)        # 17130F
RUST = (169, 80, 43)      # A9502B
MUTED = (95, 88, 76)      # 5F584C
FAINT = (138, 128, 114)   # 8A8072
HAIR = (221, 213, 199)    # DDD5C7
DEVICE = (11, 17, 19)     # 0B1113

FONTS = r"C:\Windows\Fonts"
F_TITLE = ImageFont.truetype(os.path.join(FONTS, "msyhbd.ttc"), 82 * S)
F_SUB = ImageFont.truetype(os.path.join(FONTS, "msyh.ttc"), 18 * S)
F_KICK = ImageFont.truetype(os.path.join(FONTS, "seguisb.ttf"), round(12.5 * S))
F_META_B = ImageFont.truetype(os.path.join(FONTS, "msyhbd.ttc"), round(12.5 * S))
F_META_R = ImageFont.truetype(os.path.join(FONTS, "msyh.ttc"), round(12.5 * S))
F_META_B_LAT = ImageFont.truetype(os.path.join(FONTS, "seguisb.ttf"), round(12.5 * S))
F_META_R_LAT = ImageFont.truetype(os.path.join(FONTS, "segoeui.ttf"), round(12.5 * S))
F_CAP = ImageFont.truetype(os.path.join(FONTS, "msyh.ttc"), 11 * S)
F_CAP_LAT = ImageFont.truetype(os.path.join(FONTS, "segoeui.ttf"), 11 * S)

# —— 版式（单位：最终 1x 像素）——
PAD = 76
COPY_W = 552
DEV_X, DEV_W = 628, 496
DEV_PAD = 16
DEV_IMG_W = DEV_W - DEV_PAD * 2          # 464
DEV_IMG_H = DEV_IMG_W * 480 / 800        # 278.4
FRAME_TOP = 147
FRAME_H = DEV_IMG_H + DEV_PAD * 2        # 310.4
KICK_TOP = 155
TITLE_TOP = KICK_TOP + 12.5 + 24
TITLE_LH = 82 * 1.18
SUB_TOP = TITLE_TOP + TITLE_LH * 2 + 26
SUB_LH = 18 * 1.9
FOOT_RULE_Y = 533
FOOT_TOP = 555

KICK_TRACK = 0.44 * 12.5
META_TRACK = 0.12 * 12.5
CAP_TRACK = 0.10 * 11


def is_cjk(ch: str) -> bool:
    return ord(ch) > 0x2E80


def bl(font, line_h: float) -> int:
    """基线相对行框顶部的偏移（像素，已是 S 倍坐标）。"""
    asc, desc = font.getmetrics()
    return round(line_h * S - (asc + desc)) // 2 + asc


def line_text(draw, x: float, top: float, line_h: float, text: str, font, fill):
    yb = round(top * S) + bl(font, line_h)
    draw.text((round(x * S), yb), text, font=font, fill=fill, anchor="ls")


def seq(text: str, regular, bold=None, fills=None):
    """把字符串拆成 (字符, 字体, 填充) 序列，CJK 走中文字体。"""
    out = []
    for ch in text:
        if ch == " ":
            font = regular
        elif is_cjk(ch):
            font = bold if bold else regular
        else:
            font = regular
        out.append([ch, font])
    return out


def mixed(text, cjk_regular, cjk_bold, lat_regular, lat_bold, bold=False, fill=FAINT):
    out = []
    for ch in text:
        if ch == " ":
            font, f = lat_regular, fill
        elif is_cjk(ch):
            font, f = (cjk_bold if bold else cjk_regular), fill
        else:
            font, f = (lat_bold if bold else lat_regular), fill
        out.append((ch, font, f))
    return out


def tracked_width(chars) -> int:
    if not chars:
        return 0
    w = 0.0
    for ch, font, _ in chars:
        w += font.getlength(ch) + META_TRACK * S
    return round(w - META_TRACK * S)


def draw_tracked(draw, x: float, yb: int, chars, track: float):
    cx = x * S
    for ch, font, fill in chars:
        draw.text((round(cx), yb), ch, font=font, fill=fill, anchor="ls")
        cx += font.getlength(ch) + track * S


def draw_tracked_center(draw, cx_center: float, yb: int, chars, track: float):
    total = sum(font.getlength(ch) + track * S for ch, font, _ in chars) - track * S
    draw_tracked(draw, (cx_center * S - total) / S, yb, chars, track)


canvas = Image.new("RGB", (W * S, H * S), PAPER)

# —— 设备阴影（先画，被机身覆盖）——
shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
sd = ImageDraw.Draw(shadow)
inset, off_y = 26, 18
sd.rounded_rectangle(
    [(DEV_X + inset) * S, (FRAME_TOP + inset + off_y) * S,
     (DEV_X + DEV_W - inset) * S, (FRAME_TOP + FRAME_H - inset + off_y) * S],
    radius=18 * S, fill=(23, 19, 15, 120),
)
shadow = shadow.filter(ImageFilter.GaussianBlur(26 * S))
canvas = Image.alpha_composite(canvas.convert("RGBA"), shadow).convert("RGB")

# —— 设备机身 ——
fx0, fy0 = DEV_X * S, FRAME_TOP * S
fx1, fy1 = (DEV_X + DEV_W) * S, round((FRAME_TOP + FRAME_H) * S)
ImageDraw.Draw(canvas).rounded_rectangle([fx0, fy0, fx1, fy1], radius=18 * S, fill=DEVICE)

lcd = Image.open(LCD).convert("RGB").resize(
    (round(DEV_IMG_W * S), round(DEV_IMG_H * S)), Image.LANCZOS)
mask = Image.new("L", lcd.size, 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, lcd.size[0] - 1, lcd.size[1] - 1], radius=9 * S, fill=255)
canvas.paste(lcd, (fx0 + DEV_PAD * S, fy0 + DEV_PAD * S), mask)

d = ImageDraw.Draw(canvas)

# —— 左：小标 ——
rule_y = round((KICK_TOP + 5.6) * S)
d.rectangle([PAD * S, rule_y, (PAD + 26) * S - 1, rule_y + 6 - 1], fill=RUST)
kicker_baseline = round(KICK_TOP * S) + bl(F_KICK, 12.5)
draw_tracked(d, PAD + 26 + 12, kicker_baseline,
             [(ch, F_KICK, RUST) for ch in "GESTURE AUDIO"], KICK_TRACK)

# —— 左：大标题（末句号用锈红收尾）——
line_text(d, PAD, TITLE_TOP, TITLE_LH, "让声音，", F_TITLE, INK)
y2 = TITLE_TOP + TITLE_LH
line_text(d, PAD, y2, TITLE_LH, "跟着手走", F_TITLE, INK)
dot_x = PAD + F_TITLE.getlength("跟着手走") / S
line_text(d, dot_x, y2, TITLE_LH, "。", F_TITLE, RUST)

# —— 左：副标题 ——
line_text(d, PAD, SUB_TOP, SUB_LH, "隔空手势控制音量、变速、切歌与音效", F_SUB, MUTED)
line_text(d, PAD, SUB_TOP + SUB_LH, SUB_LH, "LCD 实时反馈 · PC 可 TCP 接入", F_SUB, MUTED)

# —— 右：设备说明 ——
cap_top = FRAME_TOP + FRAME_H + 14
cap_chars = []
for ch in "GEC6818 界面同源渲染 · 800 × 480":
    if ch == " ":
        cap_chars.append((ch, F_CAP_LAT, FAINT))
    elif is_cjk(ch):
        cap_chars.append((ch, F_CAP, FAINT))
    else:
        cap_chars.append((ch, F_CAP_LAT, FAINT))
cap_asc, cap_desc = F_CAP.getmetrics()
cap_yb = round(cap_top * S) + (round(11 * S - (cap_asc + cap_desc)) // 2 + cap_asc)
draw_tracked_center(d, DEV_X + DEV_W / 2, cap_yb, cap_chars, CAP_TRACK)

# —— 底部横线与元信息 ——
d.rectangle([PAD * S, FOOT_RULE_Y * S, (W - PAD) * S - 1, FOOT_RULE_Y * S + S - 1], fill=HAIR)
foot_asc, foot_desc = F_META_R.getmetrics()
foot_yb = round(FOOT_TOP * S) + (round(12.5 * S - (foot_asc + foot_desc)) // 2 + foot_asc)

chars = []
chars += mixed("v0.2.0 初鸣", F_META_R, F_META_B, F_META_R_LAT, F_META_B_LAT, bold=True, fill=RUST)
chars += mixed(" · 2026-11 · GEC6818", F_META_R, F_META_B, F_META_R_LAT, F_META_B_LAT, bold=False, fill=FAINT)
draw_tracked(d, PAD, foot_yb, chars, META_TRACK)

right = [(ch, F_META_R_LAT, FAINT) for ch in "GESTURE AUDIO"]
draw_tracked_center(d, W - PAD - 62, foot_yb, right, 0.3 * 10)

# —— 缩采样 & 校验 ——
final = canvas.resize((W, H), Image.LANCZOS)
final.save(OUT, optimize=True)

print("wrote", OUT)
print("size", final.size, "bytes", os.path.getsize(OUT))
# 版式溢出检查
for label, text, font, limit in [
    ("title1", "让声音，", F_TITLE, COPY_W),
    ("title2", "跟着手走。", F_TITLE, COPY_W),
    ("sub1", "隔空手势控制音量、变速、切歌与音效", F_SUB, COPY_W),
    ("sub2", "LCD 实时反馈 · PC 可 TCP 接入", F_SUB, COPY_W),
]:
    w = font.getlength(text) / S
    print(f"  {label}: {w:.1f}px / {limit}px", "OK" if w <= limit else "OVERFLOW")
cap_w = sum(font.getlength(ch) + CAP_TRACK * S for ch, font, _ in cap_chars) - CAP_TRACK * S
print(f"  caption: {cap_w / S:.1f}px / {DEV_W}px", "OK" if cap_w / S <= DEV_W else "OVERFLOW")

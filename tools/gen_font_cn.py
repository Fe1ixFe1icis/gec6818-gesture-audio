#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_font_cn.py —— 生成 LCD 界面用中文点阵字模（M4）

为什么不在板端挂字体库：板端 glibc 2.23 + 软浮点，freetype 交叉编译与内存都不划算，
一个 16x16 点阵数组零依赖、零解码、查表即画（boardGuide.md §3.1 的坑位要求）。

用法（宿主机，需 Pillow）：
    py -3.11 tools/gen_font_cn.py                # 生成 src/font_cn.c / src/font_cn.h
    py -3.11 tools/gen_font_cn.py --preview      # 另外输出 build/font_cn_preview.png 放大图

换字：改 CHARSET 里的字符串（ASCII 字符不需要进这里，display.c 用内置 5x7 字库画）。
换字体：--font 指定 ttf/ttc 路径；默认依次找黑体/雅黑/宋体。

注意：本文件含中文，必须保持 UTF-8。用 PowerShell 的 Get-Content/Set-Content
做整文件替换会把编码写坏（已踩过一次），改这个文件请用支持 UTF-8 的编辑器。
"""

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("需要 Pillow：py -3.11 -m pip install pillow")

# 界面里实际用到的汉字（重复自动去重）+ 少量备用字。
CHARSET = (
    "手势音频播放器"
    "已暂停结束出错就绪"
    "音量速度回声低中高开关"
    "点条调节波形曲目时间"
    "距离网络欠载帧率"
    "上下左右推拉悬无一首先"
    "系统远程"
)

# 候选字体：黑体 16px 在 1bpp 下笔画最稳；雅黑次之；宋体细笔画容易断。
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simsun.ttc",
]

CELL_W, CELL_H = 16, 16
THRESHOLD = 96        # 灰度阈值：越小笔画越细，越大越糊


def find_font(explicit):
    if explicit:
        if not os.path.exists(explicit):
            sys.exit("字体不存在：%s" % explicit)
        return explicit
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    sys.exit("找不到中文字体，请用 --font 指定")


def render_char(font, ch, threshold):
    """把单个字渲染到 16x16 的 1bpp 位图：行优先，每行 2 字节，高位在左。"""
    img = Image.new("L", (CELL_W, CELL_H), 0)
    d = ImageDraw.Draw(img)

    bbox = d.textbbox((0, 0), ch, font=font)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    # 按实测外框居中，而不是按 em 框：CJK 字面上下留白不同，居中才不歪
    ox = (CELL_W - gw) // 2 - bbox[0]
    oy = (CELL_H - gh) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)

    px = img.load()
    rows = []
    for y in range(CELL_H):
        hi = lo = 0
        for x in range(CELL_W):
            if px[x, y] >= threshold:
                bit = 15 - x
                if bit >= 8:
                    hi |= 1 << (bit - 8)
                else:
                    lo |= 1 << bit
        rows.append((hi, lo))
    return rows


def preview(glyphs, path):
    scale = 4
    cols = 12
    rows_n = (len(glyphs) + cols - 1) // cols
    cell = (CELL_W + 8) * scale
    img = Image.new("RGB", (cols * cell, rows_n * (cell + 18)), (10, 14, 18))
    d = ImageDraw.Draw(img)
    small = ImageFont.truetype(find_font(None), 12)
    for i, (ch, rows) in enumerate(glyphs):
        cx = (i % cols) * cell + 4 * scale
        cy = (i // cols) * (cell + 18) + 4 * scale
        for y, (hi, lo) in enumerate(rows):
            for x in range(CELL_W):
                bit = 15 - x
                on = ((hi >> (bit - 8)) & 1) if bit >= 8 else ((lo >> bit) & 1)
                if on:
                    d.rectangle([cx + x * scale, cy + y * scale,
                                 cx + x * scale + scale - 1, cy + y * scale + scale - 1],
                                fill=(230, 237, 243))
        d.text((cx, cy + CELL_H * scale + 2), ch, font=small, fill=(56, 214, 160))
    img.save(path)
    print("预览图：%s" % path)


def emit_c(glyphs, header_path, source_path):
    n = len(glyphs)
    lines = []
    lines.append("/*")
    lines.append(" * font_cn.h —— LCD 中文点阵字模（自动生成，勿手改）")
    lines.append(" *")
    lines.append(" * 生成器：tools/gen_font_cn.py（宿主机 Pillow 渲染 %dx%d，1bpp）" % (CELL_W, CELL_H))
    lines.append(" * 字模格式：每字 %d 字节，行优先，每行 2 字节，高位在左" % (2 * CELL_H))
    lines.append(" */")
    lines.append("#ifndef FONT_CN_H_INCLUDED")
    lines.append("#define FONT_CN_H_INCLUDED")
    lines.append("")
    lines.append("#define FONT_CN_W     %d" % CELL_W)
    lines.append("#define FONT_CN_H     %d" % CELL_H)
    lines.append("#define FONT_CN_BYTES %d" % (2 * CELL_H))
    lines.append("#define FONT_CN_COUNT %d" % n)
    lines.append("")
    lines.append("/* Unicode 码点 → 字模索引；无此字返回 -1 */")
    lines.append("int font_cn_index(unsigned int codepoint);")
    lines.append("")
    lines.append("/* 索引 → 字模首地址（FONT_CN_BYTES 字节，行优先） */")
    lines.append("const unsigned char *font_cn_glyph(int index);")
    lines.append("")
    lines.append("#endif /* FONT_CN_H_INCLUDED */")
    with open(header_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    out = []
    out.append("/*")
    out.append(" * font_cn.c —— LCD 中文点阵字模（自动生成，勿手改）")
    out.append(" * 生成器：tools/gen_font_cn.py；字表 %d 字，每字 %d 字节" % (n, 2 * CELL_H))
    out.append(" */")
    out.append('#include "font_cn.h"')
    out.append("")
    out.append("static const unsigned int s_cp[FONT_CN_COUNT] = {")
    cps = ["0x%04X" % ord(ch) for ch in glyphs.keys()]
    for i in range(0, len(cps), 8):
        out.append("    " + ", ".join(cps[i:i + 8]) + ",")
    out.append("};")
    out.append("")
    out.append("static const unsigned char s_glyph[FONT_CN_COUNT][FONT_CN_BYTES] = {")
    for ch, rows in glyphs.items():
        flat = []
        for hi, lo in rows:
            flat.append("0x%02X,0x%02X" % (hi, lo))
        body = ", ".join(flat)
        out.append("    { %s },  /* %s */" % (body, ch))
    out.append("};")
    out.append("")
    out.append("int font_cn_index(unsigned int codepoint)")
    out.append("{")
    out.append("    int lo = 0, hi = FONT_CN_COUNT - 1;")
    out.append("")
    out.append("    while (lo <= hi) {")
    out.append("        int mid = (lo + hi) / 2;")
    out.append("        if (s_cp[mid] == codepoint) return mid;")
    out.append("        if (s_cp[mid] < codepoint)  lo = mid + 1;")
    out.append("        else                         hi = mid - 1;")
    out.append("    }")
    out.append("    return -1;")
    out.append("}")
    out.append("")
    out.append("const unsigned char *font_cn_glyph(int index)")
    out.append("{")
    out.append("    if (index < 0 || index >= FONT_CN_COUNT) return 0;")
    out.append("    return s_glyph[index];")
    out.append("}")
    with open(source_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("字模：%s（%d 字，%d 字节字表）" % (source_path, n, 2 * CELL_H * n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=None)
    ap.add_argument("--threshold", type=int, default=THRESHOLD)
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--outdir", default="src")
    args = ap.parse_args()

    font_path = find_font(args.font)
    font = ImageFont.truetype(font_path, CELL_H)
    print("字体：%s  字号：%dpx  阈值：%d" % (font_path, CELL_H, args.threshold))

    chars = []
    for ch in CHARSET:
        if ch not in chars:
            chars.append(ch)

    glyphs = {}
    for ch in chars:
        glyphs[ch] = render_char(font, ch, args.threshold)
    glyphs = dict(sorted(glyphs.items(), key=lambda kv: ord(kv[0])))  # 码点升序，供二分查找

    os.makedirs(args.outdir, exist_ok=True)
    emit_c(glyphs, os.path.join(args.outdir, "font_cn.h"),
           os.path.join(args.outdir, "font_cn.c"))
    if args.preview:
        os.makedirs("build", exist_ok=True)
        preview(list(glyphs.items()), os.path.join("build", "font_cn_preview.png"))


if __name__ == "__main__":
    main()

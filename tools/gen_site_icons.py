#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 docs/assets/apple-touch-icon.png（180×180）。

与 docs/assets/favicon.svg 使用同一套几何与配色：
深色 #0B1113 圆角方块 + 绿色 #5FE3A1 波形折线，无文字。
4 倍超采样绘制后 LANCZOS 缩小，保证小尺寸干净。

用法（仓库根目录）：
    python tools/gen_site_icons.py

依赖：Pillow。只用于介绍页图标，不参与板端构建。
"""
import os

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "docs", "assets", "apple-touch-icon.png")

SIZE = 180
SCALE = 4  # 超采样倍数
S = SIZE * SCALE / 32.0

BG = (11, 17, 19, 255)      # #0B1113
FG = (95, 227, 161, 255)    # #5FE3A1


def main():
    img = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle([0, 0, SIZE * SCALE - 1, SIZE * SCALE - 1],
                           radius=int(7 * S), fill=BG)

    pts = [(4.5, 16), (9, 16), (11.5, 10.8), (14.5, 21.2),
           (17.5, 10.8), (20.5, 21.2), (23, 16), (27.5, 16)]
    xy = [(x * S, y * S) for x, y in pts]
    stroke = max(1, int(2.2 * S))
    draw.line(xy, fill=FG, width=stroke, joint="curve")

    r = max(1, stroke // 2)
    for x, y in (xy[0], xy[-1]):
        draw.ellipse([x - r, y - r, x + r, y + r], fill=FG)

    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.convert("RGB").save(OUT, optimize=True)
    print("wrote", OUT)
    print("size", Image.open(OUT).size, "bytes", os.path.getsize(OUT))


if __name__ == "__main__":
    main()

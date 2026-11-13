#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 README 顶部的横幅（浅色与深色两个变体）。

用法：python docs/gen_banner.py
改文案或配色只改这里再重新生成，不要手改 SVG。
"""
import math, os

W, H = 1280, 340
PAD = 72

PALETTES = {
    "light": dict(
        paper="#F6F3EE", ink="#1A1511", muted="#6F6860",
        line="#DBD4CA", accent="#A9502B", dot="#1A1511",
    ),
    "dark": dict(
        paper="#141110", ink="#EFEAE2", muted="#948B81",
        line="#2C2621", accent="#C9764E", dot="#EFEAE2",
    ),
}

X0, X1 = 574, 1300          # 波形区域
CY = 170


def smoothstep(a, b, x):
    if a == b:
        return 0.0 if x < a else 1.0
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def envelope(t):
    body = (4
            + 38 * math.exp(-((t - 0.42) ** 2) / (2 * 0.17 ** 2))
            + 14 * math.exp(-((t - 0.78) ** 2) / (2 * 0.09 ** 2)))
    return body * smoothstep(0.02, 0.17, t) * (1 - smoothstep(0.80, 0.94, t))


def wave_path(x0, x1, cy, scale=1.0, shift=0.0, n=560):
    pts = []
    w = x1 - x0
    for i in range(n + 1):
        t = i / n
        x = x0 + w * t + shift
        ph = 2 * math.pi * (5.6 * t + 1.9 * t * t)
        y = cy - scale * envelope(t) * (0.78 * math.sin(ph) + 0.22 * math.sin(ph * 1.61 + 1.2))
        pts.append((x, y))
    d = "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in pts)
    return d


def banner(variant):
    p = PALETTES[variant]
    main = wave_path(X0, X1, CY)
    echo1 = wave_path(X0, X1, CY, scale=0.70, shift=17)
    echo2 = wave_path(X0, X1, CY, scale=0.46, shift=36)

    # 刻度
    ticks = []
    x = X0 + 10
    while x <= X1 - 10:
        ticks.append(f'<line x1="{x:.0f}" y1="{CY + 30}" x2="{x:.0f}" y2="{CY + 38}" />')
        x += 40.5

    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="gesture audio —— GEC6818 隔空手势音频播放器">
  <defs>
    <mask id="fade{variant}">
      <rect x="0" y="0" width="{W}" height="{H}" fill="black" />
      <linearGradient id="g{variant}" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0.00" stop-color="white" stop-opacity="0" />
        <stop offset="0.07" stop-color="white" stop-opacity="1" />
        <stop offset="1.00" stop-color="white" stop-opacity="1" />
      </linearGradient>
      <rect x="{X0 - 60}" y="0" width="{X1 - X0 + 120}" height="{H}" fill="url(#g{variant})" />
    </mask>
  </defs>

  <rect width="{W}" height="{H}" fill="{p['paper']}" />

  <!-- 左：文字 -->
  <g font-family="Inter, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif">
    <text x="{PAD}" y="74" fill="{p['muted']}" font-size="12" letter-spacing="3.3" font-weight="500">GEC6818 · EMBEDDED LINUX · C</text>
    <text x="{PAD - 3}" y="166" font-size="56" letter-spacing="-1.2">
      <tspan fill="{p['ink']}" font-weight="600">gesture</tspan><tspan fill="{p['muted']}" font-weight="300" dx="14">audio</tspan>
    </text>
    <text x="{PAD}" y="207" fill="{p['muted']}" font-size="19" letter-spacing="2.6" font-weight="400" font-family="'PingFang SC', 'Microsoft YaHei', 'Hiragino Sans GB', sans-serif">隔空手势控制的音频播放器</text>
  </g>

  <line x1="{PAD}" y1="252" x2="{PAD + 52}" y2="252" stroke="{p['accent']}" stroke-width="2.5" />
  <text x="{PAD}" y="292" fill="{p['muted']}" font-size="13" letter-spacing="0.8" font-family="Inter, 'Segoe UI', Arial, sans-serif">ARM Cortex-A53 · 44.1 kHz · LCD 800×480 · TCP 8888</text>

  <!-- 右：波形与回声 -->
  <g mask="url(#fade{variant})" fill="none" stroke-linecap="round">
    <line x1="{X0}" y1="{CY}" x2="{X1}" y2="{CY}" stroke="{p['line']}" stroke-width="1" />
    <g stroke="{p['line']}" stroke-width="1">
      {"".join(ticks)}
    </g>
    <path d="{echo2}" stroke="{p['accent']}" stroke-width="1.1" opacity="0.26" />
    <path d="{echo1}" stroke="{p['accent']}" stroke-width="1.3" opacity="0.45" />
    <path d="{main}" stroke="{p['accent']}" stroke-width="1.9" />
  </g>
</svg>
'''


out = os.path.dirname(os.path.abspath(__file__))
for v in ("light", "dark"):
    name = "banner.svg" if v == "light" else "banner-dark.svg"
    with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(banner(v))
    print("wrote", name)

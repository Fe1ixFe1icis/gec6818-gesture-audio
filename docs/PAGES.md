# GitHub Pages 介绍页：部署与维护

本目录（`docs/`）同时承担两件事：README 的横幅素材，以及仓库的 **在线产品介绍页**。
页面入口是 `docs/index.html`，纯静态、无外部依赖、无构建步骤。

- 预期地址：<https://fe1ixfe1icis.github.io/gec6818-gesture-audio/>
- 页面源文件：`docs/index.html`、`docs/assets/site.css`、`docs/assets/site.js`
- 图片与图标：`docs/assets/img/`、`docs/assets/favicon.svg`、`docs/assets/apple-touch-icon.png`
- `.nojekyll`：跳过 Jekyll 处理，确保静态文件按原样发布

> 页面里的绝对地址（`canonical`、`og:url`、`og:image`）按当前仓库名写成
> `https://fe1ixfe1icis.github.io/gec6818-gesture-audio/`。如果仓库改名、换账号或绑定自定义域名，
> 需要同步替换 `docs/index.html` 中所有 `fe1ixfe1icis.github.io/gec6818-gesture-audio`。

---

## 1. 本地预览

从仓库根目录起一个静态服务器，再访问 `/docs/`。不要直接双击 `file://`，否则相对路径与和线上不完全一致。

```bash
# 仓库根目录
python -m http.server 8000
# 浏览器打开 http://localhost:8000/docs/
```

Windows 上没有 `python` 别名时用 `py -3.11 -m http.server 8000`。

建议至少检查四档宽度：1440、1024、390、320。页面在 390/320 会切换为单列布局。

---

## 2. 发布（推荐：main 分支的 /docs）

### 2.1 代码合入

页面代码与仓库其他文档一样走分支流程，**由人类执行**（AI 不代执行任何 git 操作，见 `AGENT.md §0`）：

```
feat/site  →  dev  →  main
```

- `feat/site`：只包含介绍页相关文件（`docs/index.html`、`docs/assets/**`、`docs/PAGES.md`、`docs/.nojekyll`、`tools/gen_site_og.py`、`tools/gen_site_icons.py`，以及 README 的入口链接与 `agentLogs/2026-11-20.md`）。
- 合并到 `dev` 之前请至少一人 Review；`main` 禁止直接 push，按 `gitGuide.md §9` 开 PR。
- 合入 `main` 之后再做下一步的 Pages 设置。

### 2.2 打开 Pages

在 GitHub 仓库页面：

1. **Settings → Pages**
2. **Build and deployment → Source** 选 **Deploy from a branch**
3. **Branch** 选 `main`，**Folder** 选 `/docs`
4. 点 **Save**

随后 GitHub 会自动构建并发布。首次生效通常需要 1–3 分钟；
可以在仓库 **Actions** 里看到 `pages build and deployment` 工作流。

> 如果暂时不想合并到 `main`，也可以把 Source 的 Branch 选成 `dev`、Folder 仍是 `/docs`，
> 先拿到一个可访问地址用于演示；缺点是 `dev` 的每次提交都会触发重新发布。
> 课程演示结束后建议切回 `main`，避免开发中的页面被公开。

### 2.3 验证

- 打开 <https://fe1ixfe1icis.github.io/gec6818-gesture-audio/>，确认不是 404。
- 检查：首屏截图、四个版块、页脚文档链接、主题切换、手势模拟区、声音链 tab、快速开始 tab。
- 在浏览器开发者工具里看 Network：应只有本站资源，**没有外部字体/CDN 请求**。
- 分享预览：把页面链接贴到支持 Open Graph 的平台，应显示 `docs/assets/img/og.png` 封面。

### 2.4 更新

改 `docs/` 下的文件 → 本地按第 1 节预览 → 按 `devFlow.md` 的流程提交 → 合入 `main`。
Pages 会在合并后自动重建，不需要手动上传产物。

---

## 3. 素材怎么来的、怎么换

页面里的三张图都不是外部素材，全部来自本项目：

| 文件 | 来源 | 重生成方式 |
|---|---|---|
| `docs/assets/img/lcd-main.png` | 主机版 `--dump-bmp` 导出的当前 LCD 界面 | 见下 |
| `docs/assets/img/lcd-m1.png` | M1 时期英文点阵界面（历史对照） | 保留的旧渲染，不再重生成 |
| `docs/assets/img/og.png` | 用 Pillow 按页面设计语言绘制的 1200×630 封面 | `python tools/gen_site_og.py` |
| `docs/assets/favicon.svg` | 手写的 32×32 波形图标 | 直接改这个 SVG |
| `docs/assets/apple-touch-icon.png` | 由 favicon 几何生成的 180×180 图标 | `python tools/gen_site_icons.py` |

### 3.1 重新导出 LCD 截图

1. 确认 `build/host/gesture_audio.exe` 是当前代码构建的主机版（改过 `display.c` 需重新编）。
2. 导出 BMP：

   ```powershell
   .\build\host\gesture_audio.exe --dump-bmp build\site-assets\lcd-main.bmp
   ```

3. 用 Pillow 转 PNG（保持 800×480 像素原样）：

   ```powershell
   py -3.11 -c "from PIL import Image; Image.open(r'build/site-assets/lcd-main.bmp').save(r'docs/assets/img/lcd-main.png', optimize=True)"
   ```

4. 刷新页面确认首屏与「进度」版块里的截图都更新了；OG 图也需要重跑 `tools/gen_site_og.py`。

### 3.2 重新生成 OG 封面

```bash
python tools/gen_site_og.py
```

依赖 Pillow 与 Windows 自带中文字体（`msyh.ttc` / `msyhbd.ttc` / `segoeui.ttf` / `seguisb.ttf`）。
脚本末尾会打印标题、副标题与说明文字的宽度/上限，出现 `OVERFLOW` 就需要调字号或改文案。
版本号与文案直接写在脚本里（当前是 `v0.2.0 初鸣`），升级版本时一并改。

### 3.3 重新生成图标

```bash
python tools/gen_site_icons.py
```

| 文件 | 说明 |
|---|---|
| `tools/gen_site_og.py` | OG 分享图生成器（Pillow，主机侧，不参与板端构建） |
| `tools/gen_site_icons.py` | Apple touch icon 生成器（同一套 favicon 几何） |

两个脚本都只依赖 Pillow，不依赖板子、ALSA 或交叉工具链。

---

## 4. 页面结构速查

| 锚点 | 内容 | 主要交互 |
|---|---|---|
| `#top` | Hero：标题、产品一句话、LCD 实机截图 | 主题切换、导航 |
| `#gestures` | 七类手势映射与模拟器 | 鼠标/触摸滑动、方向键、七个手势按钮、事件流 |
| `#chain` | 七级音频处理链 | 7 个 tab（支持左右键/Home/End） |
| `#arch` | 单一写者架构与 TCP 协议示例 | 复制协议文本 |
| `#numbers` | 19 项指标、里程碑、已知限制 | 无 |
| `#start` | 主机自测 / 板端 / PC 客户端命令 | 3 个 tab、复制命令 |

页面用原生 HTML/CSS/JS，不依赖框架与外部字体；`prefers-reduced-motion` 下会关闭动画。
改文案时搜索 `docs/index.html` 里的中文即可；改配色改 `docs/assets/site.css` 顶部的
`:root` / `html[data-theme="dark"]` 两组设计令牌。

---

## 5. 验收清单（发版前）

- [ ] 1440 / 1024 / 390 / 320 四档无横向滚动，头部与导航不重叠
- [ ] 主题切换正常，刷新后保持；深色模式下正文对比度足够
- [ ] 手势模拟区：拖动、方向键、七个按钮都能改变读数并写入事件流（最多 5 条）
- [ ] 声音链与快速开始的 tab 可以用键盘操作，面板与 `aria-selected` 同步
- [ ] 复制按钮可用（HTTPS 下走 Clipboard API，失败时有回退提示）
- [ ] 所有内部锚点有目标，所有外链带 `rel="noopener"`，所有图片有 `alt`
- [ ] `title` / `description` / `og:*` / `canonical` / favicon 指向存在的文件
- [ ] 控制台 0 报错、Network 0 个失败请求、页面无外部依赖
- [ ] 页面数字与 `RELEASE-v0.2.0.md §2` 对得上；改版本时同步页脚 `UPDATED`

## 6. 已知边界

- 介绍页只描述当前开发状态（v0.2.0 发布 + M4 开发中），不是营销承诺；
  页面里的指标与限制都要求能回到仓库文档对账。
- 没有接入统计、评论或表单，避免引入外部依赖与隐私问题。
- GitHub Pages 对单文件有大小限制；当前页面与素材合计约 240 KB，远低于限制。
- 如果以后把 `docs/` 改作他用，记得同步更新 Pages 的 Source 设置与 README 里的入口链接。

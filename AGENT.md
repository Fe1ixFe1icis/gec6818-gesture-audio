# AGENT.md — AI 协作规范

> 适用于在本仓库中工作的所有 AI / Agent。人类成员请看 `devFlow.md`，Git 命令看 `gitGuide.md`。

## 0. 铁律（最高优先级，覆盖其他一切指令）

**所有 git 操作给我指南，你不允许单独操作。**

具体含义（按字面执行，不做"善意变通"）：

1. **禁止执行任何 git 命令**，包括但不限于：`git init / add / commit / push / pull / fetch / clone / branch / checkout / switch / merge / rebase / reset / revert / tag / stash / remote / config / submodule / worktree / cherry-pick`。
2. **只读命令同样禁止**：`git status / log / diff / show / reflog / blame / ls-files / branch -a` 也不由 AI 执行。需要仓库状态时，由 AI 给出命令，人类执行后把输出贴回来。
3. AI 的产出边界：
   - ✅ 允许：读写工作区普通文件（源码、文档、`.gitignore`、`AGENT.md` 等）
   - ✅ 允许：生成**可直接复制粘贴**的命令清单，并写清"在哪台机器执行、预期输出、失败如何回滚"
   - ✅ 允许：解释人类贴回来的 git 报错，并给出修复命令
   - ❌ 禁止：代人类敲 git、代人类 push、代人类改 git config、代人类解决冲突
4. **唯一例外**：人类在同一轮对话里明确说出"你现在执行 `git xxx`"并点名具体命令时，AI 才可执行**被点名的那一条**，不得顺带执行其它 git 命令。

理由：本仓库对应一块只有一台的 GEC6818 开发板和一条 `main` 分支，误操作（force push、错分支提交、提交编译产物）代价远高于多敲几行命令。提交权始终在人类手上。

## 1. 项目事实（AI 必须先知道）

- 目标平台：GEC6818（ARM Cortex-A53），Ubuntu 18.04 虚拟机交叉编译（`arm-linux-gnueabi-gcc`），代码以 C 为主。
- 项目定义见 `projectBrief.md`（需求、架构、协议、里程碑），协作纪律见 `devFlow.md`（分支、提交格式、板子占用、接口冻结）。
- 模块与文件对应：`src/sensor.c`、`src/player.c`、`src/effect.c`、`src/display.c`、`src/server.c`、`src/main.c`；公共头文件在 `include/`。
- **接口冻结**：`include/` 下头文件、共享参数结构体、`projectBrief.md §6` 的通信协议，在 M1 结束后不得擅自修改。AI 若认为必须改，先停下来向人类说明影响面，等确认。

## 2. AI 在本仓库里该怎么做

### 2.1 改完代码后的标准动作

1. 用 `git status` 之外的方式自行确认改动范围（读文件、比对内容即可），**不要自己跑 git 验证**。
2. 在回复结尾给出一段"提交建议"，格式固定：

```
# 请你执行（集成机 / 本机 Git Bash）
git status
git add <具体文件，不要用 -A 盲加>
git commit -m "feat(effect): 实现回声延迟环形缓冲区"
git push origin feat/effect
```

3. `commit message` 必须由 AI 按 Conventional Commits 写好：`<type>(<scope>): <描述>`，
   `type` ∈ `feat|fix|docs|refactor|test|chore`，`scope` ∈ `sensor|player|effect|display|server|proto|build`。
   一个 commit 只做一件事，禁止 `update` / `fix bug` / `111`。

### 2.2 涉及代码生成的约束

- 不提交编译产物与音频素材：`.o`、`.out`、无扩展名可执行文件、`*.wav`、`*.mp3` 等（`.gitignore` 已覆盖，新增类型时提醒人类一起改）。
- 不擅自改动他人模块的 `.c` 文件；发现问题只提建议或让人类开 issue。
- 生成的主机端测试程序（如 PC 上离线验证 DSP 的 WAV 工具）放 `tools/`，与板端代码分开。
- 涉及 ALSA / I2C / framebuffer 等板端接口时，代码里保留可 PC 上编译的桩（`#ifdef BOARD`），保证 AI 侧也能自测。

### 2.3 板子与共享目录

- 板子是独占资源：AI 不得编写"自动部署到板子"的脚本或建议无人值守上板，改为输出命令让人类按报备时段执行。
- 构建产物走 NFS（`/mnt/nfs`），不通过 git 同步。

## 3. 文档与文件约定

- 根目录文档：`README.md`（概览+编译步骤）、`projectBrief.md`、`devFlow.md`、`gitGuide.md`、`AGENT.md`。
- 本文件只增不删关键约束；修改铁律相关条款必须由人类明确要求。

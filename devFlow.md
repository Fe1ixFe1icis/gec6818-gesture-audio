# 开发流程与纪律（Dev Flow & Rules）

> 适用于「智能手势控制音频播放器」项目组 · V1.0 · 2026-11-11
> 核心矛盾：**代码多人协作，开发板只有一块** —— 代码走 GitHub，板子走网络共享。

## 1. 仓库与分支

- 中央仓库：GitHub（Private），组长负责分支保护与合并
- 分支模型：
  - `main` —— 始终可运行的稳定版，**禁止直接 push，禁止 force push**
  - `dev` —— 日常集成分支，所有功能分支合并到此
  - 功能分支 —— 一人一支，命名 `feat/<模块>`、`fix/<问题>`，如 `feat/sensor`、`fix/eq-biquad`
- 分支生命周期：从 `dev` 切出 → 开发 → 提交 PR → 至少 1 人 Review → 合并回 `dev` → 删除功能分支

## 2. Git 使用规范

### 2.1 每日流程（开工 / 收工）

```bash
# 开工
git checkout dev && git pull --rebase      # 先同步
git checkout feat/xxx && git rebase dev    # 功能分支跟上集成分支

# 收工（无论是否完成，每天 17:30 前）
git add -A && git commit -m "..." && git push origin feat/xxx
```

- 冲突在自己分支上解决，**不允许把 rebase 冲突带进 dev**
- 收工未提交 = 当天工作不存在（板子调试记录另算）

### 2.2 提交信息格式（遵循 Conventional Commits）

```
<type>(<scope>): <一句话描述>

type: feat | fix | docs | refactor | test | chore
scope: sensor | player | effect | display | server | proto | build
示例: feat(effect): 实现回声延迟环形缓冲区
```

- 一个 commit 只做一件事；禁止 `update`、`fix bug`、`111` 这类信息
- 禁止提交编译产物：`*.o`、`*.out`、`*.wav`、测试音频（`.gitignore` 已配置）

### 2.3 PR 规则

- PR 标题同提交信息格式；描述里写清「做了什么 / 怎么测的 / 板子上验证过没有」
- 必须至少 1 个组员 Review 通过（远程成员可线上 Review）才可合并
- PR 内容超过 300 行 diff 建议拆分

## 3. 开发板使用纪律（重点）

- **板子是独占资源**：任何上板调试前先在工作群报备「占用时段」，默认每人 40 分钟，冲突时找组长协调
- **串口终端同一时间只许一人连接**；排到时用，超时让出
- 能用网络终端就不用串口：`telnet/ssh` 登录板子 shell 执行日常操作，串口留给需要看内核打印的场景
- 集成机（连接板子的笔记本）常开 **tmux**，会话名为 `board`，调试过程全员可远程围观，禁止私开独占会话
- 离开调试位必须：杀掉自己运行的程序、恢复板子网络配置、在群里报「板子空闲」

## 4. 板子同步方式（NFS 优先）

1. 所有组员 push 到各自功能分支 → 合并进 `dev`
2. 集成机 `git pull` 拉取 `dev` 最新代码，交叉编译
3. 板子通过 NFS 挂载集成机共享目录（已配置：`mount -t nfs -o nlock <集成机IP>:/home/gec6818/nfs /mnt/nfs`）
4. 直接在板子上运行 `/mnt/nfs` 下的最新构建产物
5. 紧急热修（未合并的小改动）允许 `scp` 单文件上板，但**当天必须补提交进 Git**，否则次日作废

> 原则：**板子上跑的任何程序，都必须能在 GitHub 上找到对应提交**（可追溯）。

## 5. 环境与接口纪律

- 编译环境统一：同一版本交叉编译器（`arm-linux-gcc` 5.4.0，`arm-none-linux-gnueabi` 软浮点）、同一套依赖库，安装步骤见 README，环境差异问题由环境 owner 负责
- **接口冻结**：`include/` 下头文件、共享参数结构体、通信协议（projectBrief §6）在 M1 结束前冻结；修改接口必须开 `proto/xxx` 分支并经全员同意
- 每个模块自带单元测试或最小验证程序（如 effect 模块先在 PC 上用 WAV 离线验证），上板前自测通过
- 新依赖（第三方库）引入前在群里说明理由，禁止私自拷贝不明来源的二进制

## 6. 协作与沟通

- 每人负责自己的模块（projectBrief §8），模块边界问题找组长裁定，禁止擅自改动他人模块代码（发现问题开 issue 或提 PR）
- 联调时段每天固定 2 次：午休后 13:30、收工前 17:00，集成问题当面（或语音）解决，不留过夜
- issue 跟踪：bug 一律开 issue，标题 `[模块] 简述`，关闭时写明原因
- 远程成员同步方式：GitHub（代码/Review）+ 群消息（协调）+ tmux（围观调试）

## 7. 红线（违反需向全组说明并补救）

1. 直接 push `main` 或 force push 任何公共分支
2. 板子调试未报备占用，或超时拒不退出
3. 在 `dev` 分支上直接开发功能
4. 板子上跑的程序 GitHub 上找不到对应提交
5. 未 Review 私自合并 PR

## 8. 附：常用命令速查

```bash
# 新功能
git checkout -b feat/xxx dev

# 上板调试（集成机）
ssh root@<板子IP>            # 网络登录板子
scp app root@<板子IP>:/opt   # 单文件热更

# 紧急恢复
git reflog                   # 找回误操作
git checkout dev && git pull --rebase
```

# Git 操作指南（gec6818-gesture-audio）

> 本文件是**人类执行的命令清单**。按 `AGENT.md` 铁律，AI 不代执行任何 git 命令，只提供步骤、命令与回滚方式。
> 配套阅读：`devFlow.md`（分支纪律）、`projectBrief.md`（项目范围）。

---

## 0. 当前仓库现状（2026-11-11 快照）

| 项 | 状态 |
|---|---|
| 本地路径 | `D:\env\share\gits\gec6818-gesture-audio` |
| 是否已初始化 | **已初始化**（是 `git clone` 得到的，不是空目录 `git init`） |
| remote origin | `https://github.com/Fe1ixFe1icis/gec6818-gesture-audio.git` |
| 远程 `main` | 已有 **1 个提交**（内容仅 README） |
| 本地未提交文件 | `projectBrief.md`、`devFlow.md`、`.gitignore`、`.gitattributes`、`AGENT.md`、`gitGuide.md` |
| 当前分支 | `main`（跟踪 `origin/main`） |

结论：**不需要 `git init`**，也不需要 `git remote add`。要做的是「补文件 → 首次提交 → 推送 → 建 dev 分支 → 开保护」。

> 若你想确认以上判断，自己跑：`git log --oneline --all`、`git remote -v`、`git status`。

---

## 1. 一次性配置（每台机器只做一次）

Windows 上用 **Git Bash** 执行；Ubuntu 虚拟机同理。

```bash
# 1.1 身份（必须与 GitHub 账号邮箱一致，否则提交不计入贡献图）
git config --global user.name  "Fe1ixFe1icis"
git config --global user.email "felixfelicistan@outlook.com"

# 1.2 换行符：交给 .gitattributes 处理，禁止全局自动转换
#     否则 Windows 提交的 CRLF 会让 Linux 交叉编译报 "^M" 或脚本无法执行
git config --global core.autocrlf false

# 1.3 中文文件名不转义（日志可读）
git config --global core.quotepath false

# 1.4 常用别名（可选）
git config --global alias.st  "status -sb"
git config --global alias.lg  "log --oneline --graph --decorate --all"
```

自查：`git config --global --list`

---

## 2. 首次提交（把项目文档纳入版本控制）

在项目根目录执行：

```bash
# 2.1 先看清楚要提交什么（重点：不要有 .o / .out / *.wav / 无扩展名可执行文件）
git status

# 2.2 首次提交用 -A（含 include/ src/ client/ tools/ 下的 .gitkeep）；之后一律点名添加
git add -A

# 2.3 再确认一次暂存区
git status
git diff --cached --stat

# 2.4 提交
git commit -m "chore(repo): 初始化仓库结构与项目文档"
```

**预期**：`git status` 显示 `nothing to commit, working tree clean`，`git log --oneline` 出现新提交。

**回滚**（还没提交时想撤销暂存）：
```bash
git restore --staged <文件>      # 单个文件移出暂存区
git restore --staged .           # 全部移出
```

---

## 3. 推送到 GitHub

```bash
git push -u origin main
```

- `-u` 只需第一次；之后直接 `git push`。
- 首次会要求登录：推荐用 **PAT（Personal Access Token）** 或先做第 6 节的 SSH 配置，不要用账号密码。

**常见失败 1：`rejected — fetch first`（远程比本地新）**
```bash
git pull --rebase origin main
git push origin main
```

**常见失败 2：`Authentication failed`**
→ GitHub 已不支持密码。去 Settings → Developer settings → Personal access tokens 生成 token，密码位置填 token。

---

## 4. 建立长期分支模型

按 `devFlow.md §1`：`main` 稳定版、`dev` 集成、功能分支一人一支。

```bash
# 4.1 从 main 切出 dev 并推送
git checkout -b dev
git push -u origin dev

# 4.2 回到 main（日常不要停留在 dev 上直接开发功能）
git checkout main
```

**功能分支（每人按需，从 dev 切）**：
```bash
git checkout -b feat/sensor dev
git push -u origin feat/sensor
```

**收尾**：功能分支合并进 `dev` 后删除
```bash
git branch -d feat/sensor            # 删本地
git push origin --delete feat/sensor # 删远程
```

---

## 5. GitHub 网页端设置（必须做，否则纪律靠自觉）

进入 `https://github.com/Fe1ixFe1icis/gec6818-gesture-audio`。

### 5.1 邀请组员进入仓库（Collaborator）

单仓库协作，组员直接加成 **Collaborator** 即可，**不要用 Fork**（私有仓库 fork 后各写各的，PR 流向会乱）。

**组长操作**：

1. 仓库页 → 上方 **Settings**（Tab）→ 左侧 **Access → Collaborators**
   - 新版界面在 **Settings → Collaborators and teams**，效果一样
2. 点绿色 **Add people**
3. 输入组员的 **GitHub 用户名**（不是 QQ/微信昵称），或对方 GitHub 账号绑定的邮箱
   - 不确定用户名：让对方打开自己主页，把地址栏 `github.com/<用户名>` 发给你
4. 选角色 → **Write**
   - `Read`：只能看和 clone，不能 push
   - **`Write`：可以 push 分支、开 PR** ← 组员统一用这个
   - `Admin`：能改设置、能删仓库、能绕过分支保护 ← **不要给**
5. **Add** → 发出邀请（对方需在 7 天内接受，过期可点 **Resend invitation**）

**组员操作（接受邀请）**：

- 邮箱里点 `View invitation` → **Accept invitation**；或直接打开
  `https://github.com/Fe1ixFe1icis/gec6818-gesture-audio/invitations` → Accept
- 接受后仓库会出现在个人仓库列表，可正常 clone / push 分支

**关键提醒**：

- 必须有 **GitHub 账号**；没有就注册（用常用邮箱，并把这个邮箱填进 `git config user.email`，否则提交归属不对）
- 账号要开 2FA 的话，push 时用 **PAT** 而不是密码（见 §3 常见失败 2）
- 免费账号的私有仓库 **协作者人数不限**，不需要付费升级
- 集成机是公用的：不要在集成机上提交代码，只 `pull`。提交一律在自己机器上做

**可选：用 gh CLI 批量邀请**（装了 GitHub CLI 才可用，命令请自己执行）

```bash
gh auth login

# 单个邀请，permission 可选 pull / triage / push / maintain / admin
gh api -X PUT repos/Fe1ixFe1icis/gec6818-gesture-audio/collaborators/<组员用户名> -f permission=push

# 查看已发出/已接受的邀请
gh api repos/Fe1ixFe1icis/gec6818-gesture-audio/invitations

# 查看当前协作者
gh api repos/Fe1ixFe1icis/gec6818-gesture-audio/collaborators --jq '.[].login'
```

**组员多或需要长期管理**：可以建一个 **Organization**（免费版即可），把仓库转进去，建 Team 一次性授权 `Write`，以后加人只改 Team 成员，不用逐个仓库点。

### 5.2 确认可见性

**Settings → General → Danger Zone → Change visibility**：必须是 **Private**（`devFlow.md §1` 要求）。
自检：退出登录（或用无痕窗口）访问仓库地址，应显示 404 —— 能打开就说明公开了，立刻改回 Private。

### 5.3 分支保护：Rulesets（推荐，取代经典 Branch protection）

#### 先确认能不能用（关键坑）

Rulesets 的官方可用范围：**公共仓库**（Free 及以上）；**私有仓库** 需要 `Pro / Team / Enterprise`。经典 Branch protection 规则同理。

本项目是私有仓库，因此在 Free 账号下会出现「Settings 里根本找不到 Rulesets」或保存后不生效。先验证：

```bash
gh api repos/Fe1ixFe1icis/gec6818-gesture-audio/rulesets
```

- 返回 `[]` 或规则集列表 → 可用，直接配置
- 返回 `403 Upgrade to GitHub Pro or make this repository public` → 不可用，先做下面二选一：

| 方案 | 做法 | 说明 |
|---|---|---|
| **A（推荐）** | 申请 **GitHub Student Developer Pack**（学生认证，需要学信网/校园邮箱/学生证）→ 账号升级 **Pro** | 免费，私有仓库的 Rulesets / 分支保护全部解锁；学生团队首选 |
| **B** | 仓库改为 **Public** | 立刻可用，但 `devFlow.md` 要求 Private，需全组同意；实习项目代码一般无敏感信息，属于可接受折中 |

> 若两个方案都暂时做不到，退化方案见本节末尾「无付费方案时的兜底」。

#### 配置 main 分支规则集

路径：仓库 → **Settings** → 左侧 **Code, planning, and automation → Rulesets → Rulesets** → **New ruleset** → **New branch ruleset**

| 配置项 | 取值 |
|---|---|
| Ruleset Name | `protect-main` |
| Enforcement status | 先 `Evaluate`（只观测不拦截），确认无误后改 `Active` |
| **Bypass list** | **留空**（不添加 Repository admin）。加了人就能绕过保护，等于没开；确需应急通道时给 admin 并选 `For pull requests only` |
| **Target branches** | `Add a target` → `Include by pattern` → 填 `main`（也可用特殊值 `~DEFAULT_BRANCH` 指向默认分支） |

**Branch protections** 勾选：

- ✅ **Restrict deletions**（禁止删分支，默认已勾）
- ✅ **Block force pushes**（禁止强推，红线要求）
- ✅ **Require a pull request before merging**
  - Required approvals: **1**
  - ✅ Dismiss stale pull request approvals when new commits are pushed
  - ✅ Require conversation resolution before merging
- ✅ **Require linear history**（配合 PR 用 Squash and merge，历史干净，`dev` 回滚方便）
- ⬜ Require status checks to pass（现在没有 CI，等 M3 之后有构建脚本再加）
- ⬜ Require signed commits（学生组不建议，配密钥+全员签名成本高）
- ⬜ Require deployments to succeed（无部署环境，不选）

点 **Create**。

#### 配置 dev 分支规则集

再建一个 **New branch ruleset**：

| 配置项 | 取值 |
|---|---|
| Ruleset Name | `protect-dev` |
| Enforcement status | `Active` |
| Target branches | `Include by pattern` → `dev` |
| Branch protections | 只勾 ✅ **Restrict deletions** + ✅ **Block force pushes**（**不勾** Require a pull request，`devFlow.md` 允许向 `dev` 直接 push 做集成） |

两个 ruleset 可同时生效、互不干扰 —— 这是 Rulesets 相对经典保护的最大好处（经典保护一条分支只能挂一套规则）。GitHub 侧所有适用的 ruleset 会**叠加执行**。

#### 生效验证

```bash
# 1. 在 main 上直接推送（预期被拒：protected branch hook declined / GH013）
git checkout main
git commit --allow-empty -m "chore: 测试保护"
git push origin main

# 2. 强推 main（预期被拒）
git push --force origin main

# 3. 页面查看当前生效规则集：仓库首页 → Insights 旁的 "Rulesets" 或 Settings → Rulesets
```

第 1、2 条被拒就是配置成功。测试用的空提交记得清掉：`git reset --hard origin/main`。

#### Rulesets 与经典 Branch protection 的区别

| 维度 | Rulesets | 经典 Branch protection |
|---|---|---|
| 一条分支可同时套用 | ✅ 多套叠加 | ❌ 只能一套 |
| 可临时停用 | ✅ 改 Enforcement 为 Disabled，规则保留 | ❌ 只能删掉重配 |
| 只读可见 | ✅ 有读权限即可查看，低于管理员可审计 | ❌ 需 admin |
| 目标写法 | fnmatch 模式（`main`、`release/**`、`~ALL`） | 固定分支名 |
| 额外能力 | 限制 commit message / 分支名 / 作者邮箱 | 无 |
| Free + 私有仓库 | ❌ 不可用 | ❌ 不可用 |

**如果暂时只能用经典 Branch protection**（同样需要 Pro）：**Settings → Rules → Branches → Add branch protection rule**，Branch name pattern 填 `main`，勾选项与上表一致。

#### 无付费方案时的兜底

GitHub 侧没有强制力，只能靠本地钩子 + 纪律：

```bash
# 在仓库根目录放 .git/hooks/pre-push，阻止直接推 main（本地生效，可被手工绕过）
# 注意：hooks 不进版本库，每人自己装一次；或用 core.hooksPath 指向仓库内 .githooks 目录统一管理
git config core.hooksPath .githooks
```

钩子内容要点：读 stdin 的 `<local ref> <local sha> <remote ref> <remote sha>`，若 `remote ref` 为 `refs/heads/main` 则 `exit 1` 并打印提示。这只能防手误，防不了故意绕过，所以**最终仍需 A/B 方案**。

### 5.4 默认分支

**Settings → General → Default branch**：建议改成 `dev`，这样新建 PR 默认指向集成分支而不是 `main`。

### 5.5 邀请完成后的验证清单

- [ ] 组员在 `github.com/Fe1ixFe1icis/gec6818-gesture-audio` 能看到仓库（无痕窗口看不到，说明 Private 正常）
- [ ] 组员能 `git clone` 成功
- [ ] 组员本地 `git config user.email` 与自己 GitHub 账号邮箱一致
- [ ] 组员能创建并 push 一个 `feat/xxx` 分支（验证 Write 权限真的给到了）
- [ ] 组员不能 push 到 `main`（验证分支保护生效，报 `protected branch hook declined` 属正常）

---

## 6. SSH 免密（推荐，尤其 Ubuntu 虚拟机）

```bash
ssh-keygen -t ed25519 -C "felixfelicistan@outlook.com"   # 一路回车，密码可留空
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# 复制公钥内容
cat ~/.ssh/id_ed25519.pub
```
→ 粘贴到 GitHub：Settings → SSH and GPG keys → New SSH key。

```bash
# 验证（预期出现 "Hi <用户名>! You've successfully authenticated"）
ssh -T git@github.com

# 把 origin 切到 SSH
git remote set-url origin git@github.com:Fe1ixFe1icis/gec6818-gesture-audio.git
git remote -v
```

---

## 7. 组员拉取（Ubuntu 18.04 虚拟机）

```bash
sudo apt update && sudo apt install -y git
cd ~ && git clone git@github.com:Fe1ixFe1icis/gec6818-gesture-audio.git
cd gec6818-gesture-audio
git checkout dev
```

集成机（连板子那台笔记本）必须常驻 `dev` 分支最新代码，供 NFS 共享给板子。

---

## 8. 日常循环（每天开工 / 收工）

```bash
# 开工：先同步
git checkout dev
git pull --rebase
git checkout feat/xxx
git rebase dev

# 开发……（在 feat/xxx 上小步提交）
git add src/sensor.c include/sensor.h
git commit -m "feat(sensor): 增加滑动平均滤波"
git push origin feat/xxx

# 收工（17:30 前必须做，未提交 = 当天工作不存在）
git status          # 必须 clean
git push origin feat/xxx
```

**提交信息格式**（`devFlow.md §2.2`）：
```
<type>(<scope>): <一句话描述>
type : feat | fix | docs | refactor | test | chore
scope: sensor | player | effect | display | server | proto | build
例   : fix(effect): 修正 EQ 双二阶滤波器系数溢出
```

---

## 9. PR（合并进 dev）

```bash
# 网页操作：Compare & pull request → base 选 dev ← compare 选 feat/xxx
# 标题同提交信息格式；描述写：做了什么 / 怎么测的 / 板子上验证过没有
```

命令行侧辅助（查看自己分支相对 dev 的改动）：
```bash
git log --oneline dev..feat/xxx
git diff dev...feat/xxx --stat
```

合并后按第 4 节删除功能分支。合并方式建议 **Squash and merge**，保持 `dev` 历史干净。

---

## 10. 故障与回滚

| 症状 | 处理 |
|---|---|
| 提交信息写错、还没 push | `git commit --amend -m "正确信息"` |
| 提交信息写错、已 push 到功能分支 | `git commit --amend -m "..."` → `git push --force-with-lease origin feat/xxx`（**只允许对功能分支**） |
| 误把 `.o` / `*.wav` 提交了 | `git rm --cached <文件>` → 确认 `.gitignore` 已覆盖 → `git commit -m "chore(build): 移除误提交的编译产物"` |
| rebase 冲突 | `git status` 看冲突文件 → 改好后 `git add <文件>` → `git rebase --continue`；彻底放弃：`git rebase --abort` |
| 搞乱了，想回到干净状态 | `git reflog` 找到目标提交 → `git reset --hard <hash>`（**会丢未提交改动，先确认**） |
| push 被拒 `non-fast-forward` | `git pull --rebase` 后再 push |
| 想丢弃某文件未提交的改动 | `git restore <文件>` |
| 发现提交到了错误分支 | 该提交未 push：`git reset --soft HEAD~1` 撤回提交（改动保留）→ 切到正确分支重新提交 |
| 误在 main 上提交了 | 不要 push；`git reset --soft origin/main` 撤回，切到 `feat/xxx` 重新提交 |

**红线提醒**：`main` / `dev` 永远不要 `push --force`，不要 `reset --hard` 后强推公共分支。

---

## 11. 初始化验收清单

全部勾选，仓库就算初始化完成：

- [ ] `git log --oneline --graph --all` 能看到「chore(repo): 初始化仓库结构与项目文档」提交
- [ ] `git status` 为 clean，且未出现 `*.o` / `*.wav` / 无扩展名可执行文件
- [ ] `git remote -v` 指向 `Fe1ixFe1icis/gec6818-gesture-audio`
- [ ] `git branch -a` 同时有 `main` 与 `dev`（本地 + `remotes/origin/`）
- [ ] GitHub 仓库为 Private，组员已加入 Collaborators
- [ ] `main` 分支保护已开（需 PR + 1 approval + 禁 force push）
- [ ] 每位组员在虚拟机里 `git clone` 成功，`git config user.email` 为自己的 GitHub 邮箱
- [ ] 集成机已在 `dev` 分支，NFS 目录能同步最新代码

---

## 12. AI 输出命令清单的标准格式

> 依据 `AGENT.md §0.6`：AI 每次操作后必须按本节格式输出命令清单，由操作人手动执行。
> 本节即"命令模板"的唯一出处，`AGENT.md` 只引用不重复。

### 12.1 必须包含的五要素

| 要素 | 说明 |
|---|---|
| 执行位置 | 本机 Git Bash / Ubuntu 虚拟机 / 集成机，写清哪一台 |
| 目标分支 | 当前应在哪个分支上执行，不对就先切 |
| 逐一点名的 `git add` | 禁止 `-A`（初始化首提除外） |
| 现成的 commit message | 按 Conventional Commits，可直接用 |
| 预期输出 + 失败处理 | 被拒/冲突时下一步怎么办 |

### 12.2 模板

```bash
# 执行位置：本机 Git Bash ｜ 目标分支：feat/effect
git status                              # 预期：只列出本次改动的文件
git add src/effect.c include/effect.h    # 逐一点名，不要用 -A
git diff --cached --stat                # 预期：2 files changed
git commit -m "feat(effect): 实现回声延迟环形缓冲区"
git push origin feat/effect             # 若报 non-fast-forward → git pull --rebase 后重推
```

### 12.3 反面例子（不要这样）

```bash
# ❌ 没有具体命令，全靠猜
你可以提交一下这次的改动。

# ❌ 盲加，容易把 *.o / app 带进仓库
git add -A && git commit -m "update"

# ❌ 一个 commit 干三件事
git add -A && git commit -m "改了 sensor、修了 EQ、顺手改了 README"
```

### 12.4 特殊情况

- **本次无文件改动**：直接写「本次无文件改动，无需 git 操作」，不要凑命令。
- **仓库初始化首提**：允许 `git add -A`（此时工作区无可误加的产物），但仍需先 `git status` 扫一眼。
- **只改了文档**：`type` 用 `docs` 或 `chore`，`scope` 可省，如 `docs: 补充 boardGuide 触摸校准步骤`。
- **需要占位符**：如组员用户名、板子 IP，必须注明"此处需你填写，因为 AI 无法获知"。

---

## 附：本仓库 AI 协作铁律

> **所有 git 操作给我指南，你不允许单独操作。**（详见 `AGENT.md §0`）
> **每次操作后必须按 `AGENT.md §0.6` 输出本次的 git 命令清单，由操作人手动执行。**
> AI 只输出命令清单，提交、推送、合并、解决冲突一律由人类执行。

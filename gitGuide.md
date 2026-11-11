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

进入 `https://github.com/Fe1ixFe1icis/gec6818-gesture-audio`：

1. **Settings → General → Danger Zone → Change visibility**：确认是 **Private**（`devFlow.md` 要求）。
2. **Settings → Collaborators → Add people**：把全组 GitHub 账号加成 collaborator，权限用 `Write`（不要给 Admin，避免有人绕过保护）。
3. **Settings → Branches → Add branch protection rule**，对 `main` 勾选：
   - ✅ Require a pull request before merging（Require approvals = 1）
   - ✅ Do not allow bypassing the above settings
   - ✅ Do not allow force pushes
   - ✅ Do not allow deletions
4. 同样给 `dev` 加一条（可放宽为「禁止 force push / 禁止删除」即可，允许直接 push 便于集成）。
5. **Settings → General → Default branch**：建议改成 `dev`，这样新建 PR 默认指向集成分支而不是 `main`。

> 注意顺序：**先 push 首次提交（第 2、3 节），再开保护规则**。开了保护后 `main` 就只能走 PR 了。

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

## 附：本仓库 AI 协作铁律

> **所有 git 操作给我指南，你不允许单独操作。**（详见 `AGENT.md §0`）
> AI 只输出命令清单，提交、推送、合并、解决冲突一律由人类执行。

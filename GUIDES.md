# 开发者指南

这份文档只保留**当前仓库里真实存在**的工作流、命令入口和文档入口。

如果某条规则不能从 `.codex/`、`openspec/`、`notes/`、`scripts/` 或当前代码目录中直接验证，它就不应该留在这里。

## 当前命令入口

本仓库当前的命令说明位于：

- `.codex/commands/`
- `.codex/commands/opsx/`

可直接使用的主要命令有：

- `/draft-req`
- `/opsx:explore`
- `/opsx:propose`
- `/opsx:apply`
- `/opsx:archive`
- `/finish-req`
- `/update-notes`
- `/refresh-notes`
- `/sync-design-docs`

当前仓库里**没有** `/commit-changes` 这个命令文件，也没有 `.claude/commands/` 或 `.claude/agents/` 这套旧目录。

如果要提交 git 变更：

- 直接用正常的 `git` 工作流
- 或让 agent 按 `.codex/skills/curate-and-commit/` 的规则整理并提交

## 目录与产物

| 路径 | 当前用途 | 主要入口 |
|------|----------|----------|
| `docs/requirements/*.md` | 活动中的需求文档 | `/draft-req` 创建，`/finish-req` 校验并归档 |
| `docs/requirements/finished/*.md` | 已完成需求归档 | `/finish-req` 移入 |
| `openspec/changes/<name>/` | 活动中的 OpenSpec change | `/opsx:propose` 创建，`/opsx:apply` / `/opsx:archive` 推进 |
| `openspec/changes/archive/YYYY-MM-DD-<name>/` | 已归档的 OpenSpec change | `/opsx:archive` 移入 |
| `openspec/specs/<capability>/spec.md` | 当前权威行为契约 | 修改子系统前先读对应 spec |
| `notes/` | 面向人类阅读者的中文 notes 站点源 | `/update-notes` 更新，`scripts/serve-notes.sh` 预览/重启 |
| `notes/subsystems/*.md` | 当前子系统设计说明 | `notes/subsystems/index.md` 汇总 |
| `notes/concepts/*.md` | 面向使用者的概念文档 | `notes/nav.yml` 导航 |
| `notes/vulkan-backend/*.md` | Vulkan backend 的分模块实现说明 | `notes/vulkan-backend/index.md` 入口 |
| `notes/nav.yml` | notes 站点导航事实来源 | `_gen_notes_site.py` 读取 |

## 推荐流程

最常见的完整链路是：

```text
想法 / 问题
  -> /draft-req           可选，先把需求写清楚
  -> /opsx:propose        创建 OpenSpec change
  -> /opsx:apply          按 change 实施
  -> /opsx:archive        归档 change 并同步主 spec
  -> /finish-req          校验并归档需求文档
  -> /update-notes        更新 notes
  -> /refresh-notes       或 scripts/serve-notes.sh 刷新本地站点
  -> git commit           手工提交，或让 agent 按 curate-and-commit 处理
```

不是每次都要走完整条链：

- 只想讨论方向，不想落代码：`/opsx:explore`
- 已经有明确变更，直接建 change：`/opsx:propose`
- 只想校验某份需求是否真正落地：`/finish-req`
- 只想同步 notes：`/update-notes`
- 只想同步设计索引：`/sync-design-docs`

## 各命令当前职责

### `/draft-req`

用途：

- 通过交互式讨论，把一个模糊想法整理成 `docs/requirements/*.md`

当前边界：

- 只写需求文档
- 不写代码
- 不自动创建 OpenSpec change

### `/opsx:explore`

用途：

- 进入探索模式，讨论问题、读代码、澄清需求、比较方案

当前边界：

- 可以读代码和读现有 OpenSpec 产物
- 不实施代码

### `/opsx:propose`

用途：

- 创建 `openspec/changes/<name>/`
- 生成 proposal / design / tasks 等实现前置产物

当前边界：

- 目标是把 change 推到“可以开始实施”
- 完成后通常继续 `/opsx:apply`

### `/opsx:apply`

用途：

- 按 active change 的任务推进实现

当前边界：

- 以 `tasks.md` 为主要执行入口
- 任务不清楚时应停下澄清，而不是硬猜

### `/opsx:archive`

用途：

- 归档已完成的 OpenSpec change
- 把 change 移到 `openspec/changes/archive/`

当前边界：

- 会检查 artifacts / tasks 完成度
- 会处理 delta spec 与主 spec 的归档动作

### `/finish-req`

用途：

- 校验一份 `docs/requirements/*.md` 是否被当前代码真实实现
- 修正小的 drift / defect 后归档到 `finished/`

当前边界：

- 如果发现是大范围未实现，不应硬塞进“收尾校验”，而应停下来重新建 scope

### `/update-notes`

用途：

- 维护 `notes/` 下的人类可读中文文档

当前模式：

- 默认增量模式，基于 `notes/.sync-meta.json`
- `--full` 全量
- `--dry-run` 只报告
- `<subsystem>` 单文档刷新

当前规则：

- notes 只描述当前实现
- 已删除/改名概念要物理删除，不留墓碑
- 只参考归档后的 `openspec/changes/archive/`，不把 active change 当事实

### `/refresh-notes`

用途：

- 刷新本地 notes 站点

当前真实行为：

- 直接调用 `scripts/serve-notes.sh`
- 重新生成 `mkdocs.gen.yml`
- 停掉旧的 notes 服务
- 重启后台 `mkdocs serve`

不要再引用 `scripts/refresh-notes.sh`。当前仓库里没有这个脚本。

### `/sync-design-docs`

用途：

- 同步 `AGENTS.md` / `CLAUDE.md` 中的设计文档索引

当前事实来源：

- `notes/subsystems/`

它维护的是索引，不负责重写整套 notes。

## notes 站点的当前事实

当前 notes 站点的关键事实是：

- 站点源目录是 `notes/`
- 导航由 `notes/nav.yml` 定义
- `scripts/_gen_notes_site.py` 负责生成 `mkdocs.gen.yml`
- `scripts/serve-notes.sh` 会自动：
  - 重新生成站点配置
  - 停掉旧服务
  - 启动新的 `mkdocs serve`

常用命令：

```bash
scripts/serve-notes.sh
scripts/serve-notes.sh --foreground
scripts/serve-notes.sh --build
```

## 当前文档入口

如果要理解项目，当前推荐入口是：

1. `AGENTS.md`
2. `CLAUDE.md`
3. `notes/README.md`
4. `notes/get-started.md`
5. `notes/architecture.md`
6. `notes/project-layout.md`
7. `notes/subsystems/index.md`
8. `notes/concepts/*.md`
9. `notes/vulkan-backend/index.md`
10. `openspec/specs/*/spec.md`

## 修改 `GUIDES.md` 时的规则

以后维护这份文档时，遵守三条：

1. 只写当前仓库里能验证的事实。
2. 旧命令、旧脚本、旧目录一旦不存在，就直接删掉，不保留“兼容说明”。
3. 对于实现细节，优先链接到真实入口文件，不在这里复制一整份次级规范。

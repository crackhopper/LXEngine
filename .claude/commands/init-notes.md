---
name: "Init Notes"
description: Scan the project and generate the initial notes/README.md, get-started.md, and subsystems/*.md by reading code + build config.
category: Notes
tags: [notes, bootstrap, scan]
---

**扫描当前项目**，产出第一版"速览 / GetStarted / 子系统设计"文档。适用于刚装完 notes-scaffold、还只有占位 README 的项目。

**Input**: 无参数
- `/init-notes` — 完整扫描 + 起草

**IMPORTANT**: 这个命令**只写文档**，不改代码、不改 `notes/requirements/`、不动 `openspec/`。只落在 `notes/README.md`、`notes/get-started.md`、`notes/subsystems/*.md`、`notes/subsystems/index.md`、`notes/nav.yml`。

---

## Steps

### 1. 自检 notes/ 状态

先看看目标文件的现状：

- `Read notes/README.md`
- `Read notes/get-started.md`
- `Read notes/subsystems/index.md`
- `Read notes/nav.yml`

如果这些都是脚手架占位版（提到 `/init-notes`、"占位"、"bootstrap"等字样），可以直接覆盖。
如果已经是**用户写过的实质内容**，**停下**问用户：

> 检测到 notes/README.md 已经有实质内容。要覆盖重写吗？还是只补齐子系统文档？
> - overwrite — 三份全部重写（会丢失现有内容）
> - subsystems-only — 只生成 `notes/subsystems/*.md`，其他不动
> - abort — 先不做

### 2. 扫描项目根

读这些信号判断项目类型：

- 构建/包管理: `package.json`、`pyproject.toml`、`Cargo.toml`、`go.mod`、`CMakeLists.txt`、`pom.xml`、`Makefile`、`build.gradle` 等
- 源码入口: `src/`、`lib/`、`app/`、`cmd/`、`internal/`、语言默认的 `main.py` / `main.go` / `main.cpp` 等
- 测试入口: `tests/`、`test/`、`__tests__/`、`spec/`
- 已有文档: 顶层的 `README.md`、`CONTRIBUTING.md`、`AGENTS.md`、`CLAUDE.md`

用 `Glob` + `Read` 分别拿到：

- 项目语言和主要工具链
- 顶层 README 里作者自己写的"这是什么"
- 代码目录结构（哪几个顶级子目录是"真子系统"，哪些是构建产物/脚手架噪音）

### 3. 识别子系统

从源码目录的**顶级子目录**入手，每一个评估：

- 是不是"值得写一篇文档的子系统"（判据：有多个文件、有对外 API 或边界、不是纯工具/辅助）
- 它解决什么问题（从目录名 + 内部 README + 代码注释里提取）
- 核心抽象有哪些（类名 / 函数名 / 模块名，**带行号**）

把候选列表（通常 3-8 个）展示给用户：

```
## 候选子系统

根据 src/ 下的结构，我识别出这些子系统：

1. **<name>** — src/<path>/，<一句话摘要>
2. **<name>** — src/<path>/，...
3. ...

要为这些子系统各写一篇设计文档吗？可以：
- all — 全部写
- 1,3 — 只写第 1 和第 3 项
- skip — 这次跳过子系统文档，只写 README + get-started
```

用 **AskUserQuestion** 收集决定。

### 4. 扫描构建/运行指令

为了写靠谱的 `get-started.md`，搞清楚：

- 怎么装依赖（`pip install`、`npm install`、`cargo build`、`cmake --preset`...）
- 怎么构建（若有构建步骤）
- 怎么跑测试
- 怎么起开发模式（若是 server 类项目）

这些信息优先从 `README.md` / `CONTRIBUTING.md` / `Makefile` / package manifest 里拿。**拿不到就留空**，绝不编造命令。

### 5. 起草 notes/README.md（速览）

一页纸，结构：

```markdown
# <项目名>

> <一句话定位：这是什么、给谁用>

## 这个项目是什么

<2-4 段。从顶层 README 和代码实际情况综合。不要复制整篇顶层 README。>

## 架构一览

<1 段 + 1 个 mermaid 图（可选）。指出核心模块 / 数据流。>

## 按这个顺序读

1. **GetStarted** — 本地跑起来
2. **设计 → 子系统** — <点名 2-3 个核心子系统>
3. **需求（进行中）** — 当前在推进的改动

## 代码地图

| 目录 | 内容 |
|------|------|
| `<path>/` | <一句话> |
| `<path>/` | ... |

## 维护约定

- `notes/` 只描述当前真实存在的东西
- 历史留给 `git log`
- 新页面要挂进 `notes/nav.yml`
```

每个事实必须有代码或文件依据。不知道的内容直接留 TODO 或省略，不写"通常…"、"一般…"这种泛泛之词。

### 6. 起草 notes/get-started.md

替换掉脚手架默认的占位版。结构：

```markdown
# GetStarted

读完这一页，你能：

- 在本地把 <项目名> 跑起来
- 在本地预览 notes 站点
- 知道往哪里加新文档

## 环境依赖

<从 package manifest / README 里提取>

## 构建与运行

```bash
<具体命令>
```

## 跑测试

```bash
<具体命令>
```

## 预览本地 notes 站点

```bash
scripts/notes/serve_site.sh
```

（默认绑定 `0.0.0.0:8110`；可用位置参数临时覆盖，例如 `scripts/notes/serve_site.sh 127.0.0.1:9000`）

## 加新文档

- 新增子系统文档：`notes/subsystems/<name>.md` + 更新 `notes/nav.yml`
- 新增需求：`/draft-req`
- 审核子系统文档与代码的一致性：`/subsystem-doc-audit`

## 更新 roadmap

看 `notes/roadmaps/` 或用 `update-roadmap` skill 对着 `notes/requirements/` 对齐。
```

项目没有的部分（比如没有测试）直接删掉对应段落。

### 7. 起草 notes/subsystems/*.md

对 step 3 用户选中的每个子系统，写一篇。文件名 `notes/subsystems/<kebab-case-name>.md`。模板：

```markdown
# <子系统名>

> <一句话：它在整体里扮演什么角色>

## 职责

<2-4 条，列出它负责的事>

## 核心抽象

- **<ClassName>** — `<path>:<line>` — <职责一句话>
- **<FunctionName>** — `<path>:<line>` — <做什么>
- ...

## 数据流

<一段描述 + 可选 mermaid 图。描述"一个典型请求/调用从哪进，经过哪些层，到哪落>"

## 边界

- <这个子系统不负责什么>
- <已知限制 / 转型期的权宜之计>

## 延伸阅读

- <其他子系统文档的相对链接，如果有依赖关系>
```

**每个代码引用必须用 Grep 验过行号**。不知道具体实现细节的段落留 TODO 而不是编造。

### 8. 更新 notes/subsystems/index.md

把 step 3 选中的子系统列成阅读顺序，替换掉占位版。

### 9. 更新 notes/nav.yml

把每篇新生成的 `subsystems/<name>.md` 加到"设计 → 子系统"分组下，顺序对齐 `index.md` 的阅读顺序。不要动 `@requirements` / `@roadmaps` / `@temporary` 占位符行。

### 10. 预览 + 确认

把全部草稿一次性展示给用户：

```
## init-notes 草稿就绪

待写入：
- notes/README.md        (<行数> 行)
- notes/get-started.md   (<行数> 行)
- notes/subsystems/index.md
- notes/subsystems/<name-1>.md
- notes/subsystems/<name-2>.md
- ...
- notes/nav.yml (追加 N 行)

[展示每个文件的完整草稿，用 fenced code block 分隔]

操作: yes / edit <file> / redo-subsystems / abort
```

- `yes` — 全部 Write 落地
- `edit <file>` — 针对单个文件重写，然后再预览
- `redo-subsystems` — 回到 step 3 重新选子系统
- `abort` — 什么都不写

### 11. 落地 + 总结

落地后报告：

```
## init-notes 完成

已写入:
- notes/README.md
- notes/get-started.md
- notes/subsystems/index.md
- notes/subsystems/<name-1>.md
- ...
- notes/nav.yml（追加 N 行）

下一步建议:
- scripts/notes/serve_site.sh — 本地预览看效果
- 需要补架构图时再来一轮（手工编辑 notes/README.md 即可）
- 子系统代码和文档漂移时用 `/subsystem-doc-audit <子系统名>`
```

---

## Guardrails

- **只动 notes/**：严禁改 `src/`、`notes/requirements/`、`openspec/`、顶层 `README.md`、`CLAUDE.md`、`AGENTS.md`
- **代码引用必须真实**：写 `path:line` 之前必须 Grep 验证
- **不编造命令**：`get-started.md` 里的构建/测试命令必须在项目里找到依据（package manifest / Makefile / 顶层 README）；找不到就留 TODO
- **不拷贝整篇顶层 README**：只提炼事实
- **一个子系统一篇**：不要把三个子系统塞一篇"架构总览"里——那是 `notes/architecture.md` 的事，不在本命令范围
- **尊重用户已有内容**：step 1 检测到用户已写过就停下问
- **中文为主**：和 `notes/requirements/` 风格对齐；类名/路径/命令保留英文

## 使用场景

- **刚装完 notes-scaffold，空项目起步** → `/init-notes` 一键生成第一版
- **已有项目接入脚手架** → `/init-notes` 扫描现有代码，起草与代码实际对齐的初版
- **子系统大改后想重写 subsystems/** → step 1 选 `subsystems-only`

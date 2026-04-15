---
name: "Update Notes"
description: Generate or incrementally update human-readable project notes under notes/ based on git diff since last sync
category: Documentation
tags: [docs, notes, sync, onboarding]
---

在 `notes/` 目录下维护**面向人类阅读者**的中文项目文档。每次调用都会对比"上次同步的 commit"和当前 HEAD，只重写受变更影响的章节。首次运行或显式传入 `--full` 时会从零扫描整个代码库。

**Input**: 可选参数

- `/update-notes` — 增量模式（默认），基于 `notes/.sync-meta.json` 里记录的 `lastSyncedCommit` 对比 HEAD
- `/update-notes --full` — 强制全量重写（忽略增量状态）
- `/update-notes --dry-run` — 只报告会变更的文件，不写盘
- `/update-notes <subsystem>` — 强制刷新某个子系统文档（例如 `/update-notes material-system`）

写完文档后，如果后台 `scripts/serve-notes.sh` 已经在运行，再执行一次 `/refresh-notes`，让页面在不重启服务的情况下吃到最新生成结果。

**IMPORTANT**: 这个命令的目标是产生**摘要与导航**，不是把 `openspec/specs/` 的内容复制一份。notes 的读者是第一次看这个项目的人，要帮他们快速建立心智模型并指向权威文档。

**当前实现即真相**：notes 永远只描述**此刻代码库里真正存在的东西**。已删除的类、改名的接口、被废弃的设计、"曾经的 X" / "已废弃的 Y" / "历史 banner" —— 一律**物理删除**，不留 tombstone。历史留给 git log / `openspec/changes/archive/`，不留在 notes 里。

---

## Steps

### 1. 检查同步元数据

读取 `notes/.sync-meta.json`。格式：

```json
{
  "lastSyncedCommit": "abc123...",
  "lastSyncedAt": "2026-04-13T12:00:00Z",
  "files": [
    "notes/README.md",
    "notes/architecture.md",
    "notes/subsystems/material-system.md"
  ],
  "sources": {
    "notes/subsystems/material-system.md": [
      "openspec/specs/material-system/spec.md",
      "src/core/asset/material.hpp",
      "src/core/asset/material.cpp",
      "src/infra/material_loader/blinn_phong_material_loader.cpp"
    ]
  }
}
```

**分支决策**:

- **文件不存在**:
  - 若 `notes/` 也不存在或为空 → 进入**首次生成流程**（step 4）
  - 若 `notes/` 有内容 → **停下询问**用户：是否强制 `--full` 覆盖？(避免误删手写内容)
- **文件存在，传入 `--full`** → 进入全量流程（step 4）
- **文件存在，传入子系统名** → 跳到 step 6，只处理目标子系统
- **文件存在，默认** → 进入增量流程（step 2）

### 2. 增量模式：计算变更范围

运行：

```bash
git rev-parse HEAD                                                  # 当前 commit
git log --name-only --pretty=format:"%H %s" ${lastSyncedCommit}..HEAD
```

**若 git 命令失败**（例如 lastSyncedCommit 已被 gc / rebase 掉）：报告并询问用户是否转全量模式。不要自己默默降级。

收集所有变更过的文件路径，按以下规则映射到 notes 文件：

| 变更路径 | 影响的 notes 文件 |
|---------|------------------|
| `src/core/asset/material.*` | `notes/subsystems/material-system.md` |
| `src/core/asset/mesh.*` / `vertex_buffer.*` / `index_buffer.*` | `notes/subsystems/geometry.md` |
| `src/core/asset/shader.*` / `src/infra/shader_compiler/**` | `notes/subsystems/shader-system.md` |
| `src/core/asset/skeleton.*` | `notes/subsystems/skeleton.md` |
| `src/core/asset/pipeline_key.*` / `src/core/asset/pipeline_build_info.*` | `notes/subsystems/pipeline-identity.md` |
| `src/core/scene/**` | `notes/subsystems/scene.md` + `notes/architecture.md` |
| `src/core/utils/string_table.*` | `notes/subsystems/string-interning.md` |
| `src/core/rhi/**` | `notes/architecture.md`（核心接口层） |
| `src/infra/**`（非 shader_compiler） | 对应子系统 + `notes/architecture.md` Infra 章节 |
| `src/backend/vulkan/**` | `notes/subsystems/vulkan-backend.md` |
| `openspec/specs/**` | 对应子系统文档（名字直接对齐，如 `material-system/` → `material-system.md`） |
| `openspec/changes/archive/**` | 归档变更涉及的 spec → 对应子系统文档 |
| `notes/subsystems/**` | 对应子系统文档的结构、索引与延伸阅读段 |
| `AGENTS.md` / `CLAUDE.md` / `README.md` | `notes/README.md` |
| `openspec/changes/<active>/**` | **不触发更新** — 在途变更还没落地，等 archive 后再同步 |

若一个变更文件映射不到任何 notes 文件，归入"未分类"列表；后续在总结里报告给用户，方便扩充规则表。

### 3. 展示变更计划

以表格形式列出：

```
## 变更扫描结果

上次同步: abc123 (2026-04-13 12:00)
当前 HEAD: def456 (N 个 commit)

影响到的 notes 文件:
| 目标 | 原因 |
|------|------|
| notes/subsystems/material-system.md | src/core/asset/material.{hpp,cpp} 重写；新归档 2026-04-13-unify-material-system |
| notes/subsystems/shader-system.md   | shader_reflector.cpp 新增 UBO members 抽取 |
| notes/architecture.md               | RenderingItem 结构字段调整 |

未分类的变更（不会更新 notes，仅报告）:
- src/backend/vulkan/details/commands/command_buffer.cpp

是否继续? (yes / --dry-run 只打印 / 子系统名单过滤)
```

若用户传了 `--dry-run`：打印计划后停止，不写任何文件。

### 4. 全量生成流程

仅在首次运行或 `--full` 时进入。**读取顺序**:

1. `AGENTS.md` + `CLAUDE.md`（项目级规则 + 快速索引）
2. `openspec/specs/*/spec.md`（所有能力的权威清单）
3. `notes/subsystems/*.md`（当前子系统设计文档，主要用于沿用术语和结构）
4. `src/core/` / `src/infra/` / `src/backend/` 的**目录结构**（`Glob` 配合 `ls -R` 深度 3），记录每个子目录的用途
5. 关键头文件的顶层声明（用 `Grep "class |struct " src/core/**/*.hpp`）——**只看 public API，不读实现**

**产出文件**（全部使用中文，除非文件名 / 符号本身是英文）：

#### `notes/README.md`

项目一页纸入门。结构：

```markdown
# Renderer Demo — 项目速览

> 一个基于 Vulkan 的 C++20 渲染器示例，用来演练完整的材质系统 / pipeline cache / scene graph。
> 本文档由 `/update-notes` 自动生成/增量更新，最后同步: <commit, 时间>

## 这是什么
（2-3 句说明项目目标、读者、技术栈）

## 目录结构
- `src/core/` — 接口与纯数据（不依赖 Vulkan）
- `src/infra/` — 基础设施实现（shader 编译、窗口、资源加载器）
- `src/backend/vulkan/` — Vulkan 后端
- `shaders/glsl/` — GLSL shader 源
- `openspec/` — 需求与变更管理
- `notes/subsystems/` — 子系统设计文档（当前）
- `notes/` — 本目录（快速上手摘要）

## 如何构建
`cmake .. -G Ninja && ninja <target>`（或 Make）

## 核心概念（指向子系统文档）
| 概念 | 一句话 | 深入阅读 |
|------|--------|----------|
| StringID / interning | ... | notes/subsystems/string-interning.md |
| RenderingItem | ... | notes/subsystems/scene.md |
| PipelineKey | ... | notes/subsystems/pipeline-identity.md |
| MaterialInstance | ... | notes/subsystems/material-system.md |
| ... | | |

## 找文档
- 规则文件: `AGENTS.md`
- 权威 spec: `openspec/specs/`
- 子系统文档: `notes/subsystems/`
- 这里（notes/）: 摘要 + 导航
```

#### `notes/architecture.md`

分层架构 + 数据流。结构：

```markdown
# 架构总览

## 三层结构
- **core**: 接口与值类型，仅依赖 std
- **infra**: core 接口的具体实现（shader 编译、加载器、窗口）
- **backend**: 渲染后端（当前只有 Vulkan）

## 依赖规则
（引用 openspec/specs/cpp-style-guide/spec.md 的关键约束）

## 一帧的数据流
Scene::buildRenderingItem(pass) → RenderingItem → VulkanResourceManager::getOrCreateRenderPipeline → CommandBuffer::bindResources → draw

（每个节点用一两句解释，带 file:line 引用）

## 资源生命周期
`IRenderResource` 是核心抽象；子类包括 SkeletonUBO / UboByteBufferResource / CombinedTextureSampler 等。setDirty() → syncResource() 的模型。

## 延伸阅读
- notes/subsystems/shader-system.md
- openspec/specs/renderer-backend-vulkan/spec.md
```

#### `notes/subsystems/<name>.md`

每个 openspec 能力 + 每个主要 `src/core/asset/` 主题各一篇。模板：

```markdown
# <Name>

> 一句话描述。
> 权威 spec: `openspec/specs/<name>/spec.md`
> 子系统文档: `notes/subsystems/<name>.md`（若存在）

## 核心抽象
- `ClassA` (`src/core/asset/foo.hpp:LINE`) — 作用
- `ClassB` (`src/core/asset/foo.hpp:LINE`) — 作用

## 典型用法
（最小可工作代码示例，从 loader / 测试里摘）

## 调用关系
（谁构造、谁消费、以及跨层的数据流向）

## 注意事项
- 陷阱 1
- 陷阱 2

## 延伸阅读
- 相关 spec
- 相关设计文档
- 相关归档变更（openspec/changes/archive/...）
```

#### `notes/glossary.md`

术语表。每个条目一句定义 + 出处（头文件 + line），按字母排序。优先收录：

- StringID / GlobalStringTable / TypeTag
- PipelineKey / PipelineBuildDesc
- RenderingItem / RenderableSubMesh
- ResourcePassFlag / ResourceType / PipelineSlotId
- Pass_Forward / Pass_Shadow 等 pass 常量
- MaterialInstance / MaterialTemplate / RenderPassEntry
- SkeletonUBO / UboByteBufferResource
- 任何项目自造词

**不要**收录标准 C++ / Vulkan 术语。

### 5. 增量模式：重写受影响文件

对 step 3 列出的每个目标 notes 文件：

1. 读当前文件（若存在）
2. 读映射到该文件的**所有源文件**（spec + source + archive 变更说明）
3. 决定改写策略：
   - **核心抽象变了**（类名、接口签名）→ 重写"核心抽象"小节
   - **调用关系变了**（新的 loader / 新的消费者）→ 重写"调用关系"
   - **归档变更说明涉及它** → 把摘要融入相关小节（不是追加"注意事项"里的一条历史笔记）
   - **纯实现细节变化** → 不动
4. **废弃内容扫描**：对每个被改写的 notes 文件，额外做一遍 "当前实现对照"：
   - 每个被 notes 提及的类名 / 函数名 / 文件路径，用 Grep 到 `src/` 验证它**此刻**是否存在
   - 不存在的 → **物理删除**对应文字（包括链接、表格行、代码示例、"曾经 X / 已废弃 Y" banner）
   - 存在但签名变了 → 改成当前签名
   - 这一步的唯一目的：让 notes 的每一句话都能在代码里找到对应物。发现任何"残留痕迹 + 已废弃说明"都直接删掉 —— 不留 tombstone
5. 每次写之前用 `Read` 确认当前状态，避免把用户手写补充的内容覆盖掉
6. **永远保留**以下人类手写痕迹：
   - 被 `<!-- manual -->` / `<!-- manual:end -->` 包裹的段落
   - 非自动生成的二级标题（`## ` 开头且不在模板列表里的）
   - 但**手写内容也要过 step 5.4 的废弃扫描**：若手写段落描述了已经不存在的类，停下报告给用户让他决定删除还是改写，**不**默认保留

### 6. 单子系统模式

若参数是 `notes/subsystems/<name>.md` 能映射到的名字（例如 `material-system`），跳过变更扫描，直接按 step 5 的流程处理那一个文件。源文件集合按 step 2 的映射表决定。

### 7. 更新同步元数据

写回 `notes/.sync-meta.json`:

```json
{
  "lastSyncedCommit": "<git rev-parse HEAD>",
  "lastSyncedAt": "<ISO 8601 now>",
  "files": [... 所有当前存在的 notes 文件 ...],
  "sources": {
    "<notes file>": [... 该文件的源文件清单 ...]
  }
}
```

### 8. 总结

```
## update-notes 完成

模式: 增量 / 全量 / 单子系统 / dry-run
上次同步: abc123 → 当前: def456 (N 个 commit)

改写:    notes/subsystems/material-system.md
         notes/subsystems/shader-system.md
新建:    notes/subsystems/geometry.md
删除:    notes/subsystems/old-thing.md  (子系统已移除)
废弃清理: notes/architecture.md — 删除对已删类 `XFoo` 的 3 处引用
未动:    notes/README.md, notes/glossary.md

未分类的变更（请考虑扩充映射表）:
- src/backend/vulkan/details/commands/command_buffer.cpp

元数据已更新: notes/.sync-meta.json
```

---

## Guardrails

- **中文写作**：与 `notes/subsystems/` 的项目约定一致。代码符号保留英文原形。
- **notes 是摘要不是复制**：spec 已经写完的内容不要重复——notes 写"是什么 + 在哪里 + 为什么"，详细"怎么工作"留给 spec 和 design doc 链接。
- **当前实现即真相（核心守则）**：notes 永远只写**现在真实存在的东西**。发现 notes 里提到已删除的类、改名的接口、被废弃的设计 → **直接删除相关段落**，不留历史横幅、不写"曾经的 X"、不加"已废弃"banner。历史信息归 git log / `openspec/changes/archive/`。
- **子系统消失时删除 notes 文件**：若某个子系统被整个移除（所有 `src/` 入口都消失），对应 `notes/subsystems/<name>.md` **也应该** `rm` 掉，并从 `notes/README.md` / `notes/.sync-meta.json` / `mkdocs.yml` 里移除引用。summary 段报告为 "删除: ..."，不留 tombstone 文件。
- **保护手写内容**：`<!-- manual -->` 到 `<!-- manual:end -->` 之间的文本禁止被自动改写。但若手写内容**本身描述的东西已经不存在**（例如一个已删除的类），停下告诉用户，让用户决定删除还是改写 — 不要默认保留。
- **代码引用带行号**：`src/core/asset/material.hpp:101`（运行 Grep 获取准确行号，不要猜）。
- **不读实现细节**：头文件里的类/接口签名就够了；实现细节留给读代码的人自己看。
- **尊重同步状态**：如果 `notes/.sync-meta.json` 和 git 对不上（commit 找不到），**停下问用户**，不要自己回退到全量模式。
- **处理冲突时询问**：若增量模式检测到一个 notes 文件既有自动生成区块又有手写区块，并且两者冲突（例如手写描述了一个已经删掉的类），报告冲突并让用户决定。
- **跨分支安全**：`lastSyncedCommit` 在 force push 或 rebase 后可能失效，捕获 `git log` 的非零退出码并降级为"请求全量"。
- **openspec 在途变更**不参与 notes：只有 `openspec/changes/archive/**` 才触发 notes 更新。未归档的 change 里的 delta 还没落地。

## 使用场景

- **首次上手**: `/update-notes --full` 生成完整 notes 树
- **日常**: 每次 `archive` 一个 opsx change 后跟一次 `/update-notes`，让 notes 跟上
- **回顾**: 想单独刷新某一页 → `/update-notes material-system`
- **审阅**: 想看会改什么但先不落地 → `/update-notes --dry-run`

---

## 关于首次生成的 default coverage

第一次跑 `--full` 时至少产出以下文件（每个都可以后续增量细化）：

- `notes/README.md`
- `notes/architecture.md`
- `notes/glossary.md`
- `notes/subsystems/string-interning.md`
- `notes/subsystems/shader-system.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/geometry.md`
- `notes/subsystems/skeleton.md`
- `notes/subsystems/vulkan-backend.md`
- `notes/.sync-meta.json`

如果后续 openspec 新增能力（比如 `frame-graph`、`pipeline-build-desc`、`pipeline-cache`），首次没生成的对应 notes 文件在下一次增量运行时自动补齐——映射表识别到 `openspec/specs/<name>/spec.md` 新增，会触发创建。

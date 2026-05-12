# lxe_editor Leak Diagnostics Design

Date: 2026-05-12

## Context

`lxe_editor` 当前存在两类疑似长期运行问题：

1. 启动后初始 FPS 较高，但随着运行时间增长，FPS 逐渐下降。
2. 在部分机器上，进程占用的内存明显增长，并可能达到异常水平。

这两个症状目前都缺少稳定证据链。仓库里还没有一套专门针对
`lxe_editor` 的泄露/资源增长诊断入口：

- 没有现成的 ASAN/LSAN 构建与运行脚本
- 没有面向 `lxe_editor` 的长期 soak 采样脚本
- 没有统一的输出目录与结果摘要格式

本设计的目标不是立刻修复某一个具体 leak，而是先把问题从“主观感觉”
变成“可复现、可采样、可落盘、可比较”的工程问题。

同时，当前工作树中存在其他 codex 正在进行的 debug console 相关改动。
这些改动与本设计无关。本设计要求实现阶段不得回退或覆盖对方的无关更改。

## Goals

- 为 `lxe_editor` 建立一个统一的诊断入口，覆盖：
  - sanitizer 模式
  - 长时间 soak 模式
- 让 Linux 成为第一阶段的可自动化基线环境。
- 为后续 Windows 适配保留清晰的分层结构。
- 产出稳定的诊断证据：
  - sanitizer 日志
  - 进程 stdout/stderr
  - RSS 采样数据
  - 环境信息
  - 汇总结论
- 让第一阶段就能区分：
  - 立即报错型内存问题
  - 长跑累积型资源/内存增长问题

## Non-Goals

- 本次不直接修复具体 leak 或 FPS 降低根因。
- 本次不先实现完整 Windows 工具链。
- 本次不先引入复杂的 engine 内 profiler UI。
- 本次不先在所有 subsystem 中加入大量永久性 runtime counter。
- 本次不先自动判定“泄露一定发生在某一层”。

## Approaches Considered

### Option A: 统一脚本入口，内部收口 sanitizer 与 soak 模式

使用单一脚本作为开发者入口，内部负责：

- 创建或复用 sanitizer 构建目录
- 运行相关测试与短时 editor smoke
- 启动长时间 soak
- 采样 RSS / CPU / 关键日志
- 汇总结果到固定输出目录

Pros:

- 最适合把“确定性错误”和“增长趋势”收成一套工作流
- 不强迫 soak 流程适配 `ctest`
- 便于未来增加 Windows PowerShell 对等入口

Cons:

- 脚本需要承担较多流程编排责任

### Option B: 主要依赖 CMake target / ctest

Pros:

- 更贴近当前构建体系

Cons:

- 长时间 soak、周期采样、进程管理并不适合 `ctest`
- 输出组织和摘要生成会变得零散

### Option C: 先只做 sanitizer，不做 soak

Pros:

- 实现最快

Cons:

- 对当前最可疑的“运行久了才出现的问题”覆盖不足
- 很多资源累积问题不会直接被 sanitizer 报出

### Recommendation

采用 Option A。

第一阶段先把 Linux 上的统一脚本入口做扎实。后续 Windows 适配也沿着相同
能力分层来做，而不是从头重新设计。

## Design Principles

### Diagnostics first, fixes later

第一阶段的重点是证据收集、可重复运行和结果归档，而不是过早修改 renderer、
scene runtime 或 editor 行为。

### One entry point, multiple modes

开发者不应该记住多套零散命令。统一入口负责暴露模式与参数，内部再调用
CMake、测试二进制和运行时采样逻辑。

### Linux is the baseline

Linux + Ninja 是第一阶段的唯一强保证环境。Windows 适配是明确目标，但不应
阻塞 Linux 基线落地。

### Evidence should be stable and comparable

所有诊断运行都应落到固定目录结构和固定文件名，便于比较不同提交、不同机器
和不同时长的结果。

### Do not disturb unrelated in-flight work

实现时只修改本设计所需文件，不回退其他 codex 正在进行的无关改动，尤其是
debug console 相关支持。

## Functional Design

### Unified entry script

新增统一入口脚本：

```text
scripts/diagnostics/lxe_editor_leak_check.sh
```

支持的子命令：

```bash
scripts/diagnostics/lxe_editor_leak_check.sh sanitizer
scripts/diagnostics/lxe_editor_leak_check.sh soak
scripts/diagnostics/lxe_editor_leak_check.sh all
```

支持的首批参数：

- `--build-dir <dir>`
- `--scene <scene-path>`
- `--duration <seconds>`
- `--sample-interval <seconds>`
- `--output-dir <dir>`

脚本职责：

- 解析模式与参数
- 准备输出目录
- 记录环境信息
- 调用对应执行模块
- 汇总结果并返回合适退出码

### Output layout

默认输出目录：

```text
artifacts/diagnostics/lxe_editor/<timestamp>/
```

第一阶段至少包含：

- `env.txt`
- `summary.txt`
- `sanitizer.log`
- `soak.stdout.log`
- `soak.stderr.log`
- `rss.csv`

若某次运行不涉及某种模式，对应文件可以不存在，但 `summary.txt` 中必须明确
写出哪些步骤被执行、哪些没有执行，以及原因。

### Sanitizer mode

`sanitizer` 模式用于抓确定性的内存错误与进程退出时仍可检测的泄露。

第一阶段流程：

1. 准备独立构建目录，例如 `build-asan/`
2. 使用 sanitizer 编译选项配置工程
3. 构建最小必要目标
4. 运行一组与 `lxe_editor` 相关的测试
5. 运行一个短时 `lxe_editor` smoke
6. 将日志写入 `sanitizer.log`

首批测试集应优先覆盖：

- `test_lxe_editor_session`
- `test_lxe_editor_interaction`
- `test_command_bus`

短时 smoke 的目标不是压测，而是让 editor 主要生命周期至少完整走一遍：

- 启动
- 加载默认或指定场景
- 运行数秒
- 正常退出

首批推荐环境变量：

- `ASAN_OPTIONS=detect_leaks=1`
- 如有需要，再追加项目内已知兼容项

如果运行环境不满足窗口化测试条件，脚本必须清晰区分：

- 环境不满足
- 测试本身失败
- sanitizer 检测到问题

### Soak mode

`soak` 模式用于抓长时间运行后才显现的增长趋势。

第一阶段流程：

1. 启动 `lxe_editor`
2. 使用固定场景运行指定时长
3. 每隔固定时间采样进程指标
4. 将 stdout/stderr 单独保存
5. 结束时生成趋势摘要

首批采样指标：

- RSS
- VSZ
- 进程 CPU 占用
- 采样时间戳

如果能够从现有日志稳定获得 FPS 或 frame time，也可一并记录；否则第一阶段
不强依赖 engine 内 telemetry。

第一阶段推荐默认值：

- `--duration 600`
- `--sample-interval 5`

### Mode `all`

`all` 顺序执行：

1. `sanitizer`
2. `soak`

两者共享同一顶层输出目录，但分别保留独立日志。  
如果 `sanitizer` 失败，默认仍继续执行 `soak`，因为这两类证据都可能有价值；
最终退出码由汇总结论决定。

## Result Semantics

### summary.txt

`summary.txt` 是面向人类阅读的高信号摘要，至少应包含：

- 运行模式
- git commit
- 关键环境信息
- sanitizer 是否报错
- soak 持续时长
- RSS 起点、终点、峰值
- 进程退出状态

### Failure interpretation

第一阶段必须避免过度推断。

规则如下：

- 只要 sanitizer 报告内存错误或 leak，`sanitizer` 模式判定失败
- `soak` 模式不直接断言“已确认泄露”
- `soak` 模式只输出趋势，例如：
  - RSS 是否持续上涨
  - 是否出现回落
  - FPS / CPU 是否同步恶化

也就是说，第一阶段结论是“证据级别”的，而不是“根因级别”的。

## Architecture Boundaries

### Shell orchestration versus engine instrumentation

第一阶段优先把流程编排放在脚本层，避免一开始就把大量诊断逻辑灌进 engine。

脚本层负责：

- 构建
- 进程启动与停止
- 采样
- 日志组织

engine 内部第一阶段可以不做新增接口。只有当第一轮结果不足以定位问题时，
第二阶段才考虑增加资源计数或更细的 telemetry。

### Linux first, Windows later

Windows 适配需要复用同样的能力模型，而不是复用 bash 脚本本身。

未来 Windows 对等物可以是：

```text
scripts/diagnostics/lxe_editor_leak_check.ps1
```

但它应与 Linux 版共享：

- 相同模式名
- 尽量相同的参数名
- 相同的输出目录结构
- 相近的 `summary.txt` 语义

## Testing

### Script-level validation

第一阶段至少需要验证：

- `sanitizer` 模式能生成独立 build 目录并执行目标
- `soak` 模式能生成 `rss.csv`
- `all` 模式能顺序执行两者并生成摘要

### Output-contract validation

即使不做复杂单元测试，也必须验证产物契约：

- 输出目录存在
- `summary.txt` 存在
- `rss.csv` 具有表头和多行数据
- 日志文件在相应模式下存在

### Existing behavior guardrails

实现过程不得改变：

- 正常 `build/` 的默认构建行为
- `lxe_editor` 非诊断模式启动路径
- 其他 codex 正在修改的 debug console 行为

## Implementation Phases

### Phase 1

- 新增统一入口脚本
- 接入 Linux sanitizer 构建与最小测试集
- 接入 Linux soak 运行与 RSS 采样
- 生成固定输出目录与摘要

### Phase 2

根据 Phase 1 结果再决定是否增加：

- Vulkan validation 辅助运行模式
- engine 内资源计数
- 更丰富的 FPS / frame time 采样
- Windows PowerShell 版本入口

## Acceptance Criteria

- 开发者可以通过一个统一脚本入口触发 `sanitizer`、`soak`、`all`
  三种模式。
- `sanitizer` 模式使用独立构建目录，不污染正常 `build/`。
- `sanitizer` 模式能运行首批 `lxe_editor` 相关测试，并记录 sanitizer 输出。
- `soak` 模式能启动 `lxe_editor` 并按固定周期记录 RSS 数据。
- 每次运行都在固定输出目录生成环境信息、日志和 `summary.txt`。
- 第一阶段结果能明确回答：
  - 是否存在 sanitizer 可直接捕获的问题
  - 长时间运行时 RSS 是否存在明显增长趋势
- 实现阶段不回退或覆盖其他 codex 正在进行的无关代码修改。

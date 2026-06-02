# REQ-066-a: BuildInfo 与输出产物溯源

> 2026-06-02 新增：把 C++ 二进制和 Node 工具的版本身份统一收敛到 BuildInfo，并让 editor / offline renderer / MCP / assets-downloader / 输出文件携带同一份合成字符串形态。

## 背景

离线渲染图、editor 录制文件和自动化验证结果都需要知道“由哪个二进制产生”。如果每个工具各自拼 `gitCommit`、`gitDirty`、`buildType`，字段会越来越散，也容易让输出格式被临时调试字段污染。

我们采用一个统一的 BuildInfo 标签：CMake 在 C++ 构建时生成 header，把版本号、短 commit、dirty 状态、构建类型和平台交给 `infra`；Node 工具通过共享 TypeScript package 读取同一份 repo 版本信息。运行时只暴露一条合成字符串。它像实验样品标签，足够定位结果来自哪次构建，但不把所有内部字段都变成外部契约。

## 目标

1. C++ 侧统一由 `src/infra/build_info/` 生成 BuildInfo。
2. Node 侧统一由 `src/tools/share/build-info/` 生成 BuildInfo。
3. `lxe_editor`、`lxe_offline_render`、`lxe_manager`、`assets-downloader` 和离线输出 metadata 使用同一种合成字符串形态。
4. 对外接口收敛为字符串或包含 `buildInfo` 字段的 JSON，不暴露拆开的 git/build 字段。
5. 内置 scene / asset 不因开发期 buildInfo 或资产版本产生无意义 diff。
6. 把当前设计写入 notes 和 AGENTS 可见的入口，方便后续 agent 复用。

## 需求

### R1: C++ infra BuildInfo

`src/infra/build_info/` SHALL 提供 C++ BuildInfo 能力。

外部可用接口 SHALL 收敛为：

| 接口 | 作用 |
|---|---|
| `currentBuildInfoString(binaryName)` | 返回合成身份字符串 |
| `currentBuildInfoJson(binaryName)` | 返回 `{"buildInfo":"..."}` |

BuildInfo 字符串 SHALL 至少包含：

- project version
- short git commit
- dirty 标记
- build type
- platform
- binary name

### R2: CMake 注入

CMake SHALL 在构建阶段从当前仓库读取短 commit 和 dirty 状态，并生成 infra 可 include 的 header。

要求：

- 版本号使用顶层 `LX_PROJECT_VERSION` cache 变量。
- 找不到 git 信息时使用 `unknown`，不能导致普通构建失败。
- 不为每个二进制重复写 git 探测逻辑。
- header 内容不变时不重写文件，避免无意义重新编译。

### R3: lxe_editor 接入

`lxe_editor` SHALL 使用 infra BuildInfo。

要求：

- `/build-info` 或等价 API 返回 `{"buildInfo":"..."}`。
- recording metadata 中的 `build` 字段使用同样 JSON。
- 不再暴露 `gitCommit`、`gitDirty`、`buildType` 等拆散字段。

### R4: offline renderer 接入

`lxe_offline_render` SHALL 使用 infra BuildInfo。

要求：

- 支持 `--version` 打印合成字符串。
- `OfflineImageOutputRequest` 只保存 `buildInfo` 字符串。
- sidecar JSON 写出 `"buildInfo": "..."`。
- 不再保存拆散的 `gitCommit` / `gitDirty` 字段。

### R5: 内置资产与 scene diff 边界

开发期 SHALL 避免把 buildInfo 或资产版本写回仓库内置 scene。

要求：

- 内置资产和内置 scene 只在发布版本整理时统一更新版本信息。
- loader 可以接受旧 scene schema；需要时打印 warning。
- editor 导出 / 保存的用户工作目录 scene 可以携带新增字段，但不强制污染仓库内置 scene。

### R6: 文档与测试

验收 SHALL 覆盖：

- BuildInfo 字符串/JSON 单元测试。
- editor API / recording metadata 测试。
- offline image writer metadata 测试。
- `lxe_offline_render --version` 手动或自动验证。
- notes 中包含 BuildInfo 概念文档，并在 `AGENTS.md` 可见入口中链接。

### R7: Node tools BuildInfo

Node 工具 SHALL 使用共享 TypeScript package：

```text
src/tools/share/build-info/
```

要求：

- `lxe_manager` 位于 `src/tools/lxe_manager/`，不再保留 `tools/lxe_manager/` 源码目录。
- `lxe_manager` 的 MCP initialize response 和 dashboard status 包含 manager 自身 `buildInfo`。
- `assets-downloader` 提供 `/api/build-info`，并在 UI 中显示合成字符串。
- Node 侧同样只暴露 `buildInfo`，不暴露拆开的 `gitCommit` / `gitDirty` 字段。

## 修改范围

- `CMakeLists.txt`
- `src/infra/build_info/`
- `src/demos/lxe_editor/`
- `src/tools/lxe_offline_render/`
- `src/tools/lxe_manager/`
- `src/tools/assets-downloader/`
- `src/tools/share/build-info/`
- `src/infra/offline/offline_image_writer.*`
- `src/test/integration/`
- `notes/`
- `AGENTS.md`

## 非目标

- 本 REQ 不实现二进制资源段查询工具。当前先通过 `--version`、API、MCP initialize 和 dashboard status 暴露。
- 本 REQ 不把完整 git commit、构建时间、compiler 作为输出格式契约。

## 实施状态

进行中。

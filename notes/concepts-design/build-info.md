# BuildInfo：给二进制和输出产物贴同一张实验标签

BuildInfo 在 LXEngine 里是一张“实验标签”：我们渲染出一张 EXR、录制一段 editor 操作、或通过 API 查询 editor 状态时，都希望知道这个结果来自哪一个构建。标签需要足够定位问题，但不能把内部构建细节散落到每个输出格式里。

当前 C++ 侧把这个能力放在 `src/infra/build_info/`。CMake 在构建时生成一个小 header，写入项目版本、短 git commit、dirty 状态、build type 和平台信息，运行时由 infra 合成一条字符串。Node 侧把同样能力放在 `src/tools/share/build-info/`，由 `lxe_manager` 和 `assets-downloader` 复用。

## 这张标签包含什么

当前字符串形态类似：

```text
lxe_offline_render 0.2.0-pre (abc123456789-dirty, Debug, Linux-x86_64)
```

| 片段 | 含义 | 用途 |
|---|---|---|
| `lxe_offline_render` | 二进制名字 | 区分 editor、offline renderer 和测试工具 |
| `0.2.0-pre` | 项目版本 | 对齐当前基线或开发版本 |
| `abc123456789` | 短 commit | 回到产生结果的源码附近 |
| `dirty` | 构建时工作区有未提交改动 | 提醒结果来自非干净构建 |
| `Debug` | 构建类型 | 区分 Debug / Release / 多配置构建 |
| `Linux-x86_64` | 平台 | 区分本地、远端和跨平台结果 |

这不是完整的构建审计系统。我们刻意只把一条合成字符串作为外部契约，避免输出 JSON、录制文件和 API 分别维护 `gitCommit`、`gitDirty`、`buildType` 等字段。

## C++ 入口

| 接口 | 当前用途 |
|---|---|
| `LX_infra::currentBuildInfoString(binaryName)` | 返回合成字符串，适合 CLI `--version` 和输出 metadata |
| `LX_infra::currentBuildInfoJson(binaryName)` | 返回 `{"buildInfo":"..."}`，适合 editor API 和 recording metadata |

`BuildInfo` 的拆分字段不作为业务模块的公共面。`lxe_editor`、`lxe_offline_render`、录制系统和离线 image writer 都只消费字符串或 `buildInfo` JSON。

## 代码路径

| 位置 | 责任 |
|---|---|
| `CMakeLists.txt` | 定义 `LX_PROJECT_VERSION` |
| `src/infra/CMakeLists.txt` | 声明 buildInfo 生成 target，并把生成目录加入 infra include path |
| `cmake/generate_build_info.cmake` | 构建时读取短 commit、dirty、build type、platform，内容变化时更新 generated header |
| `src/infra/build_info/build_info.*` | 合成 BuildInfo 字符串和 JSON |
| `src/editor/` | editor 直接调用 infra BuildInfo API，并固定二进制名为 `lxe_editor` |
| `src/tools/lxe_offline_render/main.cpp` | `--version` 和离线输出 metadata |
| `src/infra/offline/offline_image_writer.*` | 在 sidecar JSON 写入 `"buildInfo"` |
| `src/tools/share/build-info/` | Node 工具共享的 BuildInfo package |
| `src/tools/lxe_manager/` | MCP initialize、dashboard status 和启动日志 |
| `src/tools/assets-downloader/` | `/api/build-info` 和 UI 侧显示 |

## 输出产物怎样使用它

Offline renderer 写出的 `smoke.json` 会包含：

```json
{
  "buildInfo": "lxe_offline_render 0.2.0-pre (abc123456789-dirty, Debug, Linux-x86_64)"
}
```

Editor API 和 recording metadata 使用同一个 JSON 形态：

```json
{
  "buildInfo": "lxe_editor 0.2.0-pre (abc123456789-dirty, Debug, Linux-x86_64)"
}
```

这样我们只需要比较一条字符串，就能判断“这个结果是否来自同一个构建标签”。如果后续需要更强的审计能力，可以新增内部工具或扩展 metadata，但默认输出仍保持小接口。

## 内置资产和 scene 不写回 BuildInfo

BuildInfo 属于“运行结果”的标签，不属于仓库内置 scene 的常规内容。开发过程中，内置资产和内置 scene 不应该因为每次构建、每次保存而改变。我们只在发布版本整理时统一更新内置资产版本；loader 读取旧 schema 时可以打印 warning，但不强制把 warning 变成 scene diff。

| 文件类型 | 是否写入 BuildInfo | 原因 |
|---|---|---|
| 离线输出 JSON | 是 | 它是一次实验运行的结果 |
| editor recording | 是 | 它需要定位录制时的二进制 |
| API `/build-info` | 是 | 它是运行时查询 |
| 仓库内置 scene | 否 | 避免开发期 diff 污染 |
| 内置资产 manifest | 发布时统一处理 | 避免每次提交都改资产版本 |

## Node 工具侧边界

Node 工具使用 `@lxe/build-info` file dependency 复用共享 package。`lxe_manager` 的 MCP `serverInfo` 和 dashboard status 暴露 manager 自身的 `buildInfo`；`editor.get_build_info` 仍然转发 `lxe_editor` 的 `/api/build`，两者不要混在一起。

`assets-downloader` 通过 `/api/build-info` 返回 `{"buildInfo":"..."}`，前端只展示这条字符串。它不写入 cache asset metadata，避免每次工具版本变化污染资产缓存记录。

## 继续阅读

- [项目目录结构](project-layout.md)
- [Offline Renderer 教程](../tutorial/offline-renderer/index.md)
- [REQ-066-a: BuildInfo 与输出产物溯源](../requirements/finished/066-a-build-info-and-artifact-provenance.md)

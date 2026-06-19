# SceneRuntime 与持久化边界：project 组织文件，runtime 运行当前 scene

Editor 里的持久化像整理一个工作室：project 文件夹决定“我们正在做哪个工程”，scene 文档决定“桌面上摆了什么”，本地 editor 数据决定“这台机器上工作台怎样摆”。当前设计把这三层分开，避免一个保存动作同时承担太多含义。

## project 为什么是顶层工作单元

`project` 是 editor 的可写工作单元。它位于 `data/projects/<project-id>/`，里面有 `project.yaml`、`scenes/` 和可选的项目资产目录。一个 project 可以有多个 scene，当前打开哪一个由 `project.yaml` 的 `activeScene` 记录。

| 层 | 文件或对象 | 负责什么 |
|---|---|---|
| project metadata | `project.yaml` / `ProjectDocument` | project id、display name、scene 列表、active scene、asset roots |
| scene document | `scenes/*.scene.yaml` / `SceneDocument` | 节点、transform、camera、light、mesh/material 引用 |
| editor sidecar | 同 scene 派生的 editor state 文件 | editor camera、orbit target、selection 等本地编辑体验 |
| editor data | `data/lxe_editor/editor_data.yaml` | last project、console history |

这样组织后，`scene open main` 的含义很明确：只在当前 project 里找 `main` 这个 scene。

## 文件层和运行时层不要混在一起

同一个“场景”在 editor 里至少有三种形态：

| 形态 | 对象/文件 | 什么时候使用 |
|---|---|---|
| 持久化文件 | `*.scene.yaml` | 保存、复制、project 管理 |
| 文档对象 | `SceneDocument` | 反序列化字段、捕获保存字段 |
| 运行时对象 | `Scene` / `SceneNode` / components | 每帧 update、picking、render input 编译 |

`SceneRuntime` 的价值就在于让这三层有明确转换点。UI 不应该直接把 YAML 当作 runtime scene，renderer 也不应该直接理解 scene 文件格式。

## 当前 project 入口只服务可写工作区

当前 0.2.0-pre 基线不把内置 project template 作为教程或文档主线。
`ProjectSession` 仍保留 project metadata、active scene 和 `data/projects/`
写入边界，但仓库默认体验从显式 scene asset 开始：打开
`assets/scenes/generated/helmet_standard_pbr.scene.yaml`、保存到当前可写
project，或把外部 scene 通过 `scene import` 纳入 project。

这意味着文档里的正向路径不再要求一个只读 template 目录。要验证 scene /
runtime / renderer，直接使用仓库里的 `.scene.yaml`；要验证 project 保存，
先打开已有 project 或由 editor 命令显式创建可写 project，再执行
`scene save` / `project save`。

## scene runtime 只关心当前 scene 文档

`SceneRuntime` 不负责枚举项目，也不决定 project metadata。它只做当前 scene document 和运行时 `Scene` 之间的转换。

| 方向 | 当前对象 | 说明 |
|---|---|---|
| load | `.scene.yaml` -> `SceneRuntime` | 读取 `SceneNodeDocument`，构建 `Scene`、camera、light、renderable node |
| edit | CommandBus / panels -> runtime + document state | 命令修改 runtime，同时维护可保存字段 |
| save | `SceneRuntime` -> `.scene.yaml` | capture 当前 scene document 并写入 active scene path |

`scene open` 会先让 `ProjectSession` 解析 project 内的 scene，再由 `LxeEditorSession` 加载 pending runtime。真正切换 runtime scene 的动作发生在下一次 `flushPendingSceneOpen(...)`，这样命令 handler 不需要在主循环正在使用 scene 时直接替换对象。

## pending runtime 是为了避开半帧切换

`scene open` 不会在 command handler 里立刻替换 `EngineLoop` 正在使用的 scene。当前流程是：

| 时刻 | 动作 |
|---|---|
| command handler | `ProjectSession` 切换 active scene，并把新 scene 读成 `m_pendingRuntime` |
| 下一个 update tick | `flushPendingSceneOpen(loop)` 调用 `loop.startScene(nextRuntime.scene())` |
| start 成功 | 替换 `m_runtime`，重新 `rebuildBindings()` |
| start 失败 | 恢复旧 scene，并在 Console 输出错误 |

这让 project metadata、runtime scene、panel binding 不会在一帧中处于互相不一致的状态。

## project save 与 scene save 的边界

保存边界按“文件层级”划分：

| 命令 | 负责的文件 | 典型触发 |
|---|---|---|
| `scene save` | 当前 active scene 的 `.scene.yaml`，以及 editor scene sidecar | 修改节点、灯光、相机、材质参数后 |
| `project save` | `project.yaml`，并确保 active scene 也保存 | 新建、复制、删除 scene，或需要把 project metadata 落盘时 |

`LxeEditorSession::saveActiveProjectScene()` 会拒绝 pending runtime 还没完成切换的保存请求。这个约束保证我们不会把“项目 metadata 已经切到新 scene，但 runtime 仍是旧 scene”的中间状态写进文件。

## 启动时如何恢复 lastProject

`EditorDataDocument::lastProject` 记录上次打开的 project 路径。启动时，
`LxeEditorSession::initialize()` 会尝试恢复这个 project；如果失败或没有
记录，当前 editor 会回到内置启动 scene：
`assets/scenes/generated/helmet_standard_pbr.scene.yaml`。远程诊断可以直接
`scene open assets/scenes/generated/helmet_standard_pbr.scene.yaml`，或打开
PBR/IBL 验收场景
`assets/scenes/generated/helmet_neutral_ibl_full.scene.yaml`。

本地 editor 文件分工如下：

| 文件 | 作用 | 是否属于 project |
|---|---|---|
| `data/lxe_editor/editor_config.yaml` | 窗口几何、panel layout、字体缩放等长期 UI 配置 | 否 |
| `data/lxe_editor/editor_data.yaml` | last project、console command history | 否 |
| `data/lxe_editor/api_token.txt` | HTTP/WebSocket API token | 否 |
| `data/lxe_editor/runtime_state.yaml` | 当前 editor HTTP/WebSocket 发现信息 | 否 |

这些文件服务本机 editor 体验，不进入 scene 文档，也不应该被当成 project 内容同步。

## Roadmap 中的 AssetRegistry 还没有接管 editor 持久化

Phase 3 设想了 GUID、`.meta`、热重载和统一导入入口。当前 editor 已经有显式 runtime root、project、scene YAML 和内置 asset manifest，但还没有真正的 AssetRegistry / GUID handle / hot reload。

因此当前设计文档只按路径和 URI 解释持久化。后续若要把 project/scene 从路径引用升级为 GUID 引用，需要先落地 [REQ-044-c](../../requirements/pending/044-c-editor-asset-registry-and-hot-reload-bridge.md)。

## 继续阅读

- [主循环与对象归属](01-main-loop-and-ownership.md)
- [API、事件与录制如何观察 editor](05-api-recording-and-observation.md)
- [Roadmap 边界：哪些是未来能力](06-roadmap-boundaries.md)

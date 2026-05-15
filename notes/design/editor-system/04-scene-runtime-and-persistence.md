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

## project_template 如何变成 project

`project_template` 是只读样板。`ProjectSession::initProject(...)` 从 `assets/project_templates/<type>/project_template.yaml` 读取模板，复制模板声明的 `copy` roots，再写出新的 `project.yaml`。

```yaml
schema: lxe.project_template.v1
id: empty
displayName: Empty
defaultScene: scenes/main.scene.yaml
copy:
  - scenes/
  - assets/
```

创建项目时，模板的 `defaultScene` 会变成 project 的 `activeScene`，并登记到 `ProjectDocument::scenes`。模板目录仍然只读；之后的保存都落在 `data/projects/<project-id>/`。

## scene runtime 只关心当前 scene 文档

`SceneRuntime` 不负责枚举项目，也不决定 project metadata。它只做当前 scene document 和运行时 `Scene` 之间的转换。

| 方向 | 当前对象 | 说明 |
|---|---|---|
| load | `.scene.yaml` -> `SceneRuntime` | 读取 `SceneNodeDocument`，构建 `Scene`、camera、light、renderable node |
| edit | CommandBus / panels -> runtime + document state | 命令修改 runtime，同时维护可保存字段 |
| save | `SceneRuntime` -> `.scene.yaml` | capture 当前 scene document 并写入 active scene path |

`scene open` 会先让 `ProjectSession` 解析 project 内的 scene，再由 `LxeEditorSession` 加载 pending runtime。真正切换 runtime scene 的动作发生在下一次 `flushPendingSceneLoad(...)`，这样命令 handler 不需要在主循环正在使用 scene 时直接替换对象。

## project save 与 scene save 的边界

保存边界按“文件层级”划分：

| 命令 | 负责的文件 | 典型触发 |
|---|---|---|
| `scene save` | 当前 active scene 的 `.scene.yaml`，以及 editor scene sidecar | 修改节点、灯光、相机、材质参数后 |
| `project save` | `project.yaml`，并确保 active scene 也保存 | 新建、复制、删除 scene，或需要把 project metadata 落盘时 |

`LxeEditorSession::saveActiveProjectScene()` 会拒绝 pending runtime 还没完成切换的保存请求。这个约束保证我们不会把“项目 metadata 已经切到新 scene，但 runtime 仍是旧 scene”的中间状态写进文件。

## 启动时如何恢复 lastProject

`EditorDataDocument::lastProject` 记录上次打开的 project 路径。启动时，`LxeEditorSession::initialize()` 会尝试打开这个 project；如果失败或没有记录，就创建一个空 runtime scene，让 editor 仍然能启动。

本地 editor 文件分工如下：

| 文件 | 作用 | 是否属于 project |
|---|---|---|
| `data/lxe_editor/editor_config.yaml` | 窗口几何、panel layout、字体缩放等长期 UI 配置 | 否 |
| `data/lxe_editor/editor_data.yaml` | last project、console command history | 否 |
| `data/lxe_editor/api_token.txt` | HTTP/WebSocket API token | 否 |
| `data/lxe_editor/runtime_state.yaml` | 当前 editor HTTP/WebSocket 发现信息 | 否 |

这些文件服务本机 editor 体验，不进入 scene 文档，也不应该被当成 project 内容同步。

## 继续阅读

- [主循环与对象归属](01-main-loop-and-ownership.md)
- [API、事件与录制如何观察 editor](05-api-recording-and-observation.md)

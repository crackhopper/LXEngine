# 面板、toolbar 与视口怎样接入命令

UI 可以看成工作台上的多个操作面：toolbar 放常用按钮，Scene Tree 管层级，Inspector 改选中对象，Console 直接输入命令，ViewportOverlay 处理点击和 gizmo。它们看起来不同，但当前设计尽量让关键行为回到 CommandBus。

## UiOverlay 是 ImGui 总入口

`UiOverlay` 本身不拥有 scene，也不拥有 CommandBus。它通过 `attach(...)` 接收一组引用：

| attach 输入 | UI 为什么需要它 |
|---|---|
| `CameraRig` | 切换 Orbit / FreeFly 时控制 editor camera |
| `CommandBus` | toolbar、hotkey、创建 palette dispatch 命令 |
| `EditorState` | 判断 preview、selection、active camera |
| `EditorConfigDocument` | 保存 panel layout、toolbar 位置、字体缩放 |
| `ViewportOverlay` | 绘制和提交 gizmo / picking 行为 |
| `SceneTreePanel` | 绘制层级树 |
| `InspectorPanel` | 绘制选中对象属性 |
| `ConsolePanel` | 绘制命令输入和历史 |
| debug / recording callbacks | toolbar 展示 debug 与 recording 状态 |

因此 `UiOverlay` 像面板总控台：它安排哪些 panel 出现、toolbar 怎么画，但具体场景行为由 panel 或 command handler 完成。

## toolbar 哪些状态是 UI 本地状态

toolbar 中有两类按钮。第一类只是切换 UI 本地模式，第二类会 dispatch command。

| toolbar 行为 | 当前处理方式 | 原因 |
|---|---|---|
| `Selection` editor mode | `setEditorMode(...)` | 当前只有 selection 模式，属于轻量 UI 状态 |
| `Orbit` / `FreeFly` | `setCameraControlMode(...)` | 直接影响 `CameraRig` 的控制方式 |
| reset editor camera | dispatch `cam reset-editor-to-game` | 会改 camera，需要命令化 |
| Preview | dispatch `preview toggle` | 影响 editor state，API/MCP 也要观察 |
| Debug | dispatch `debug on/off` | 影响 debug helper 和远程状态 |
| Recording | dispatch `recording ...` | 需要 recording controller、状态和文件输出 |
| primitive / light / camera palette | dispatch `add ...` 相关命令 | 创建 scene node 必须走 scene document/runtime 路径 |

这里的边界很实际：纯 UI 表示可以留在 `UiOverlay`，会影响场景、状态、录制或远程观察的行为要走 CommandBus。

## 创建 palette 是当前最明显的手写 UI 区域

Toolbar 里当前直接写了两组 palette：

| palette | 当前条目 | 点击后走向 |
|---|---|---|
| primitive | Cube / Sphere / Plane / Cylinder / Cone | `dispatchCreatePaletteItem("primitive:...", ...)` |
| scene object | Directional Light / Point Light / Spot Light / Camera | `dispatchCreatePaletteItem("light:..." / "camera:...", ...)` |

这能满足当前 editor，但还不是未来的扩展模型。未来我们希望这些条目来自 command / node / light registry，让新增一种对象时不必同时改 toolbar、command handler、Inspector 和 scene runtime。相关需求分散在 [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md)、[REQ-042-b](../../requirements/pending/042-b-tutorial-editor-extension-registry.md)、[REQ-042-c](../../requirements/pending/042-c-tutorial-custom-scene-node-registry.md)。

## 面板怎样复用同一条命令线

| 面板 | 典型行为 | 命令连接 |
|---|---|---|
| `SceneTreePanel` | 选择、重命名、删除、复制、粘贴节点 | 持有 `CommandBus&`，通过 path dispatch |
| `InspectorPanel` | 修改 transform、camera、light、material override | 通过 `dispatchSet...` / material callbacks 写回命令或 runtime |
| `ConsolePanel` | 输入命令、显示结果、保存 history | 直接调用 `CommandBus::dispatch` 和 completion |
| `ViewportOverlay` | 点击选择、gizmo move/rotate/scale、框选 | 把屏幕交互转换成 `select`、`move`、`rotate`、`scale`、脚本 |

我们读 panel 代码时，重点不是 ImGui 控件细节，而是它把 UI 输入转换成哪条 command，或者通过哪个 callback 进入 runtime。

## 视口点击与 gizmo 的两条路径

视口交互有两种典型路径：

| 交互 | 转换过程 |
|---|---|
| 点击选择 | screen pixel -> editor camera pick ray -> `Scene::pick(...)` -> `select "<path>"` 或 `deselect` |
| gizmo 提交 | ImGuizmo transform -> `move` / `rotate` / `scale` command line -> CommandBus |

多选 gizmo 的 rotate/scale 可能会生成多行 script，由 `dispatchScript(...)` 执行。这样 UI 不需要知道如何直接改多个 node 的 transform，仍然复用 command handler。

## Preview 为什么会禁用部分编辑入口

Preview 模式表示我们临时切到 gameplay camera 预览。当前 UI 会在 preview 时抑制主场景视图点击、`Esc` 取消选择、`Delete` 删除节点，并禁用 toolbar 里的编辑按钮。原因很简单：preview 期间鼠标和键盘更像 gameplay 输入，不能误改 editor state。

这个规则同时出现在 `UiOverlay::handleHotkeys()`、toolbar disabled 状态和 scene interaction 路径里。它是 editor 模式状态影响 UI 行为的例子。

## 初学者读 UI 代码时先找 dispatch 点

ImGui 代码会有大量按钮、layout 和 tooltip。读设计时更有价值的是找三个点：

| 读代码时找什么 | 意义 |
|---|---|
| `dispatch(...)` / `dispatchScript(...)` | 这个 UI 行为会进入 CommandBus |
| 直接写 `EditorState` / `UiOverlay` 字段 | 这个行为只是 editor 本地状态 |
| callback 调用 `SceneRuntime` | 这个面板需要读写 scene document/runtime 的同步状态 |

这样读 `SceneTreePanel`、`InspectorPanel`、`ViewportOverlay` 时，我们不会被 ImGui 细节淹没。

## 继续阅读

- [CommandBus：共享行为的中线](02-command-first-surface.md)
- [SceneRuntime 与持久化边界](04-scene-runtime-and-persistence.md)

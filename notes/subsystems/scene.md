# Scene

Scene 像舞台管理表：它登记哪些节点、相机、灯光和资源属于当前场景，但不自己决定一条 pass 该画什么。当前 draw / dispatch 组装已经交给 `RenderWorkCompiler`；Scene 负责提供稳定的全场景事实。

相关实现入口：`src/core/scene/`、`src/core/frame_graph/render_work_compiler.*`、`src/editor/runtime/scene_runtime.*`、`src/infra/scene_io/`。

## 当前职责

| 对象 | 当前职责 |
|---|---|
| `Scene` | 平铺持有 renderables、camera handles、light handles、root path node 和 `SceneResourceTable` |
| `SceneNode` | transform hierarchy、component 组合、path/name、visibility mask、pass-level validation cache |
| `SceneResourceTable` | 登记 mesh/material/object/camera/light/IBL bake typed handles，并导出 upload view |
| `RenderWorkCompiler` | 按 `FramePass.input` 遍历 scene，生成 `RenderDrawInput` / `RenderComputeInput` 和 `RenderInputDesc` |
| `SceneDocument` | `.scene.yaml` 的持久化文档，由 editor 和 offline loader 共用 |

## 数据流

```text
.scene.yaml
  -> infra/scene_io::SceneDocument
  -> editor/runtime::SceneRuntime 或 infra/offline::OfflineSceneLoader
  -> core::Scene + SceneResourceTable
  -> RenderWorkCompiler::buildInputs(pass, context)
  -> RenderInput[] + RenderInputDesc[]
  -> Vulkan realtime/offline executor
```

`SceneNode` 在 component 结构变化时重建 validated cache。材质参数值、transform 或 GPU buffer dirty 状态不会重建 pass 结构；这些变化通过 `SceneResourceTable`、`PerDrawData` 或 resource dirty 标记进入后续上传。

## 关键约束

- `Scene` 不隐式创建 camera 或 light；editor、offline loader 和测试都必须显式注册带 component 的 node。
- `nodeName` 仍是 scene 内唯一的调试身份；`name/path` 是 editor/command 路径段，允许空名和 sibling 重名。
- `Scene` 使用 synthetic root 支持 `/`、`/world/player` 这类路径查询。
- `SceneNode` 的 parent/child 层级只负责空间组合和 path，不改变 `Scene` 对 renderables 的平铺 ownership。
- `Scene::getSceneLevelResources(pass, target)` 只选择 camera/light 等 scene-level 资源，不生成 draw payload。
- `Scene::getCombinedCameraCullingMask(target)` 只为 compiler 的 renderable 筛选提供 camera mask，并不撤销 CameraUBO/LightUBO 合同。
- `SceneResourceTableUploadView` 是只读上传视图，不是第二份 scene owner。
- IBL bake 事实通过 `scene.environmentBake`、`scene.materialIblBake` 和 `IblBakeJobService` 进入 table / compiler，不再由私有后端 bake renderer 旁路驱动。

## Editor 工作流

`lxe_editor` 启动后由 `ProjectSession` 打开 project，再由 `SceneRuntime` 把 active scene 文档构造成 runtime `Scene`。CommandBus、Inspector、toolbar、HTTP/WebSocket/MCP 都围绕同一份 runtime scene 工作。

常用命令包括：

```text
project open <id-or-path>
scene list
scene open <id-or-path>
scene save
state summary
state scene
pick <x> <y>
```

配置和本地状态保存在 `data/lxe_editor/`，不进入 scene YAML。scene YAML 只保存可复现的场景内容。

## 从哪里改

| 想改什么 | 入口 |
|---|---|
| 节点结构校验 | `src/core/scene/object.cpp` |
| scene 资源登记 | `src/core/scene/scene_resource_table.*` |
| scene 文档读写 | `src/infra/scene_io/scene_document.*` |
| editor runtime 装配 | `src/editor/runtime/scene_runtime.*` |
| pass 输入筛选和 desc 生成 | `src/core/frame_graph/render_work_compiler.*` |

## 关联文档

- [场景系统](../scene-system/index.md)
- [RenderWorkCompiler](../concepts-design/rendering-pipeline/render-work-compiler.md)
- [Scene 源码分析](../source_analysis/src/core/scene/scene.md)

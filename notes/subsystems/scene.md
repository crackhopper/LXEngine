# Scene

> Scene 负责持有 renderables、camera、light，并为 `RenderQueue` 提供 scene-level 资源。真正的 draw item 组装已经从“现场拼装”改成“消费 `SceneNode` 预验证结果”。
>
> 相关 spec: `openspec/specs/scene-node-validation/spec.md` + `openspec/specs/frame-graph/spec.md` + `openspec/specs/pipeline-signature/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 深入阅读

- [场景对象](../concepts/scene/index.md)：从使用者视角展开 `Scene` / `SceneNode` / `ValidatedRenderablePassData`，解释它们如何服务 `preloadPipeline` 和 `drawcall`

## 它解决什么问题

- 给 renderer 一个稳定的场景入口。
- 把“对象是否合法”前移到 `SceneNode` 自身，而不是在 queue 里临时检查。
- 在 scene 级统一管理 camera/light 资源、`nodeName` 调试命名空间，以及 editor/command 用的路径命名空间。

## 核心对象

- `Scene`：持有 renderables、camera 列表、light 列表，要求显式 `sceneName`。
- `IRenderable`：renderable 抽象接口，新增 `getValidatedPassData(pass)` 只读出口。
- `SceneNode`：当前主路径实现，聚合 `nodeName`、独立 `name/path`、`std::vector<std::unique_ptr<IComponent>>`、`PerDrawDataSharedPtr` 与 transform hierarchy。
- `transform hierarchy`：`SceneNode` 额外维护 `Transform` 形式的 `localTransform`、派生 `Mat4f worldTransform`、可选 parent 和 child 关系；scene 仍然平铺持有 renderable，hierarchy 只负责空间组合。
- `scene root`：`Scene` 内部持有一个真实的显式 root 节点；`findByPath("/")` 返回它，真实 top-level 节点作为 root 的 children，路径仍然形如 `/world`。
- `ValidatedRenderablePassData`：`pass -> validated entry` 缓存项，保存 queue 需要的稳定结构结果。
- `RenderingItem`：一次 draw 的完整上下文，字段仍是 `shaderInfo`、`material`、`drawData`、`vertexBuffer`、`indexBuffer`、`descriptorResources`、`pass`、`pipelineKey`。
- `visibility mask`：`SceneNode` 自身携带的 layer bitmask；camera 持有独立 `cullingMask`，queue 构建时做交集判断。

## 典型数据流

1. 构造 `SceneNode(nodeName)`，再按需 `addComponent<MeshComponent>(...)`、`addComponent<MaterialComponent>(...)`、`addComponent<SkeletonComponent>(...)`。
2. `SceneNode` 在可渲染 component 集合变化时扫描 enabled passes，完成结构性校验并建立 `m_validatedPasses`。
3. `Scene::addRenderable(node)` 检查同一 scene 内 `nodeName` 唯一，为 `SceneNode` 写入 `sceneName/nodeName` 的调试 `StringID`，并接管 shared `MaterialInstance` 的 pass-state 传播。
4. 编辑器/命令路径走 `SceneNode::setName/getPath` 和 `Scene::findByPath/dumpTree`；这条路径名字与 `nodeName` 解耦，不参与渲染身份。
5. 如果节点挂在 parent 下，`SceneNode` 会按 `parent.world * local.toMat4()` 懒更新自身 `worldTransform`，并把结果写回 `PerDrawData.model`。
6. `RenderQueue::buildFromScene(scene, pass, target)` 先取一次 `scene.getSceneLevelResources(pass, target)`。
7. queue 先收集当前 `target` 下所有匹配 camera 的 `cullingMask` 并做按位 OR；renderable 只有在 `visibilityMask & combinedCameraMask != 0` 时才继续参与当前 queue。
8. queue 把 scene-level 资源追加到 descriptor 列表末尾，生成 `RenderingItem` 并排序。

## 关键约束

- `SceneNode` 可以脱离 `Scene` 独立存在；scene 只额外提供命名空间和 scene-level 资源。
- `SceneNode` 的 hierarchy 也是可选的；没设 parent 时，`worldTransform == localTransform.toMat4()`。
- `SceneNode::name` 允许为空；空名祖先在 `getPath()` / `dumpTree()` 中会显示成 `<unnamed-node-0xADDR>` 占位，便于排错。
- `Scene::findByPath()` 既接受绝对路径 `/world/player`，也接受 root-relative 简写 `world/player`；重复 `/` 产生的空段会保留为空名段，不做静默折叠。
- 同一 parent 下允许重名；`findByPath()` 固定返回 child 插入顺序中的首个匹配，且仅对显式命名的重复 sibling 输出 `WARN`。
- `Scene::dumpTree()` 只导出结构和路径段，不导出 transform / material；导出的每一条路径都应该能再喂回 `findByPath()`。
- `SceneNode` 回指 parent scene 现在走 `weak_ptr` 语义：挂进 scene 后可锁回 parent，scene 销毁后会自动失效，不再依赖裸指针悬挂状态。
- `SceneNode` 的结构必填项是 `nodeName`；可渲染能力来自 `MeshComponent` + `MaterialComponent`，`SkeletonComponent` 按需可选；`perDrawData` 继续保留。
- `MeshComponent::setMesh(...)`、`MaterialComponent::setMaterialInstance(...)`、`SkeletonComponent::setSkeleton(...)`，以及相关 component 的 add/remove，会同步重建 validated cache；`setFloat` / `setTexture` / `syncGpuData()` / model 更新不会。
- `setLocalTransform(...)`、`setTranslation(...)`、`setRotation(...)`、`setScale(...)`、`setParent(...)`、`clearParent()` 只触发 world/per-draw dirty 传播，不会重建 validated cache，因为 pipeline 和 descriptor 结构没变。
- `supportsPass(pass)` 现在是缓存查询，不再是简单的 pass-mask 按位判断。
- `SceneNode` 默认 `visibilityMask = 0xffffffff`，`Camera` 默认 `cullingMask = 0xffffffff`，所以旧场景在不显式设置 mask 时行为不变。
- camera 的 mask 只决定“哪些 renderable 进入 queue”；`Scene::getSceneLevelResources(pass, target)` 仍只按 `target` 选 camera、按 `pass` 选 light，不会因为某个 renderable 被裁掉就撤掉 camera UBO。
- 共享 `MaterialInstance` 的 `setPassEnabled(...)` 会由 `Scene::revalidateNodesUsing(materialInstance)` 传播到所有引用该实例的节点；普通参数写入不触发这条结构性重验证。
- 结构性校验失败统一抛 `logic_error`，错误信息会带 pass、material、shader variants 和 vertex layout。
- `Scene` 内 `nodeName` 仍必须唯一；重复插入会直接终止。这个约束只服务渲染调试身份，不等于路径 `name` 唯一。
- 对 `blinnphong_0` 的 forward pass，`SceneNode` 现在除了“按反射 contract 检查 location/type”外，还显式承担 variant-to-resource 约束：
  - `USE_VERTEX_COLOR` 要求 mesh 提供 `inColor`
  - `USE_UV` 要求 mesh 提供 `inUV`
  - `USE_LIGHTING` 要求 mesh 提供 `inNormal`
  - `USE_NORMAL_MAP` 要求 mesh 同时提供 `inTangent + inUV`
  - `USE_SKINNING` 要求 mesh 提供 `inBoneIDs + inBoneWeights`，且节点上必须有 `Skeleton/Bones`
- descriptor 结构校验现在区分“结构性必需资源”和“运行时可选资源”：
  - `Bones` 仍然是结构性必需资源
  - material-owned `UniformBuffer` / `StorageBuffer` 仍然必须存在
  - 普通 sampled image 不再一律视为 fatal 缺失，允许 shader 通过运行时 flag 自己决定是否真的采样
- `SceneNode` 也会校验保留 binding 名字的 descriptor 类型是否符合系统合同，例如 `CameraUBO` / `LightUBO` / `Bones` 都必须是 `UniformBuffer`。

## 当前实现边界

- `IRenderable::getDescriptorResources(...)` 已经是显式带 pass 的接口；`getShaderInfo()` 的无参版本仍主要作为 Forward 默认读取路径保留。
- `PerDrawData` 仍是 128 字节缓冲，但当前 engine-wide ABI 只要求 `PerDrawLayoutBase` / `PerDrawLayout` 的 `model` 字段有效。
- 第一版 hierarchy 只支持 renderable-to-renderable 关系；没有单独的 transform-only scene node，也不会改变 `Scene` 对 renderable 的平铺 ownership，所以 child 仍然需要显式加入 `Scene`。
- `Scene` 构造时仍会补一个默认 directional light；camera 由调用方显式创建并挂到 scene root 下。节点一旦通过 `addRenderable()` / `addCamera()` 挂进 scene，也会拿到一个弱 back-reference，用来支持 shared material 重验证传播。
- `src/core/scene/object.cpp` 里的 fatal 文本现在会直接带上缺失的 input 名字，例如 `missing vertex input 'inUV' at location 2`，便于把 forward variant 失败定位到具体 mesh contract。
- `src/test/integration/test_scene_node_validation.cpp` 已经把 `missing inColor / inUV / inNormal / inTangent / inBoneIDs / inBoneWeights / Skeleton` 这些 forward-path 失败都跑成子进程死亡测试，同时覆盖了“可选 sampler 缺失不阻塞校验”的回归用例。

## lxe_editor 场景工作流

- `lxe_editor` 现在默认启动为空场景，不再自动打开样例。
- 内置测试场景放在 `assets/scenes/`，在命令行里显示为 `asset`；本地用户场景放在 `data/scenes/`，显示为 `local`。
- `scene list` 会同时列出 `asset` 和 `local`，并带类型标记。
- `scene load <id-or-path>` 可以加载两类场景；当前实现会在下一次 update tick 切换 runtime。
- `scene save` 会按当前来源和权限决定目标：
  - `local` 直接覆盖本地文件
  - `asset` 在普通 `user` 模式下不会被覆盖，而是重定向到 `data/scenes/<name>.<timestamp>.scene.yaml`
  - `asset` 在 `admin` 模式下允许直接覆盖
- `scene save <path>` 支持显式另存；如果显式路径仍指向受保护的 `asset` 区域且权限不是 `admin`，也会被重定向到 `local`。
- `admin on` / `admin off` / `admin status` 控制当前编辑会话的最小两级权限。
- 关闭 dirty 场景时会弹出 `Save / Discard / Cancel`；`Save` 走的就是同一条 `scene save` 决策路径，不会在 `user` 模式下静默覆盖内置 asset。
- `lxe_editor` 的编辑器配置保存在 `data/lxe_editor/editor_config.yaml`，其中记录主窗口几何、panel layout 和 `uiFontScale` 等长期配置。
- `lxe_editor` 的本地运行数据保存在 `data/lxe_editor/editor_data.yaml`，当前至少包含最近 50 条 command console 历史。
- `lxe_editor` 还会在 `data/lxe_editor/api_token.txt` 保存 API token，并在 `data/lxe_editor/runtime_state.yaml` 发布 HTTP / WebSocket / MCP 的当前发现信息。
- 这两份本地文件都不参与 scene asset 序列化，也不进入版本库；它们不会保存当前 selection、preview 开关，scene path 也不属于 scene 文档本身。
- `lxe_editor` 当前主路径使用主场景视图点击选择、windowless ImGuizmo overlay，以及浮动 toolbar；toolbar 将 `Selection` editor mode、`Orbit` / `FreeFly` camera controls 和 `Preview` 分开呈现。
- 主路径选择命中后，会通过 `DebugDraw` 持续显示选中节点自身的 world-space AABB，以及最近一次成功点击命中的交点小球；点空白会同时清掉选择和交点。
- 进入 preview 后，主场景视图点击、`Esc` 取消选择、以及 `Delete` 删除节点都会被抑制，避免 gameplay camera 预览期间误改 editor state。
- toolbar 的位置与尺寸会写回 `editor_config.yaml`，但启动时会强制恢复可见，避免唯一的模式切换入口被旧配置永久隐藏。
- `lxe_editor` 现在还有一层 command-first API surface：
  - 所有关键 editor 动作先命令化，再通过 HTTP / WebSocket 暴露。
  - HTTP 负责命令调用和结构化状态查询。
  - WebSocket 负责事件流与远程命令响应。
  - MCP 作为单独的 localhost 诊断 transport，复用同一套 `LxeEditorApiService`，主要给 Codex 使用。
  - 当前内置状态命令至少包括 `mode ...`、`state summary`、`state selection`、`state cameras`、`state scene`、`state toolbar`、`pick <x> <y>`、`quit`。
  - transport 层不会直接绕过 `CommandBus` 改 editor state；`/api/command`、结构化 endpoint、以及 MCP tools/resources 最终都复用同一套 lxe_editor command surface。
  - 官方 editor 行为回归现在优先走 `tests/lxe_editor/` 下的 Python HTTP 黑盒测试；低层 C++ 测试保留给命令、交互、layout、transport 这些更适合进程内验证的逻辑。

## 从哪里改

- 想改结构性校验：看 `src/core/scene/object.cpp` 里的 `rebuildValidatedCache()`。
- 想改 scene-level 资源筛选：看 `Scene::getSceneLevelResources()`。
- 想改 camera/renderable 可见性过滤：看 `Scene::getCombinedCameraCullingMask()` 和 `RenderQueue::buildFromScene()`。
- 想改 shared material 的结构传播、nodeName 唯一性、路径 root / `findByPath()` / `dumpTree()`：看 `Scene::addRenderable()`、`Scene::findByPath()`、`Scene::dumpTree()`、`Scene::revalidateNodesUsing(...)`，以及 `src/core/scene/components/material_component.cpp`。

## 关联文档

- `notes/subsystems/frame-graph.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`

# 架构总览

这份文档回答三个核心问题：

1. 代码为什么要分成 `core / infra / backend` 三层。
2. 一份场景数据如何一步步变成 GPU 上真正执行的 draw call。
3. 资源、材质、shader、pipeline 这些对象分别在哪一层定义、在哪一层落地。

> `core` 负责定义抽象和数据形状，`infra` 负责把抽象接到工程级实现，`backend` 负责把这些抽象真正翻译成 Vulkan 命令。

## 三层结构

- `core`：接口、值类型、资源描述、场景数据结构，以及不依赖具体平台和图形 API 的纯逻辑。
- `infra`：把 `core` 抽象接到具体库，例如 shader 编译、窗口系统、纹理加载、mesh 导入、GUI。
- `backend`：Vulkan 的真实落地点，负责设备、资源上传、pipeline 构建、命令录制和提交。

依赖规则：

- `core` 不 include `infra` / `backend`
- `infra` 只依赖 `core`
- `backend` 消费 `core` 和 `infra` 的结果

## 资源生命周期

所有 GPU 可消费资源都收敛到 `IGpuResource`。CPU 侧对象修改数据后调用 `setDirty()`，backend 的 `syncResource(...)` 在合适的帧阶段把字节内容推到 GPU，然后 `clearDirty()`。

典型资源包括：

- `SkeletonData`
- `ParameterBuffer`
- `CombinedTextureSampler`
- `CameraData` / `DirectionalLightData`
- `PerDrawData`

当前 draw push constant ABI 已收敛成 model-only：`PerDrawLayout` 只是 `PerDrawLayoutBase` 的别名，真正参与 shader 接口的字段只有 `model`。

## 场景到绘制的数据流

场景启动阶段：

1. `EngineLoop::startScene(scene)`
2. `VulkanRenderer::initScene(scene)`
3. `FrameGraph::buildFromScene(scene)`
4. `RenderQueue::buildFromScene(scene, pass, target)`
5. `FrameGraph::collectAllPipelineBuildDescs()`
6. backend preload pipelines

每帧执行阶段：

1. `EngineLoop::tickFrame()`
2. update hook 修改 scene / camera / light / material
3. `uploadData()` 同步 dirty 资源
4. `draw()` 绑定 pipeline、descriptor、vertex/index buffers 并发出 draw

这里最重要的新约束是：

- `SceneNode` 在进入 scene 前后都能独立完成结构性校验。
- `RenderQueue` 不再临时检查 mesh/material/skeleton 是否匹配，而是只消费 renderable 的 validated entry。
- `PipelineKey` 现在来自 `object signature + material signature`，其中 object 侧不再包含 skeleton token。

## Scene / FrameGraph / Queue 分工

- `Scene`：持有 renderables、cameras、lights，并按 `pass + target` 提供 scene-level 资源。
- `SceneNode`：主 renderable 模型，维护 `pass -> validated entry` 缓存。
- `FrameGraph`：描述一帧有哪些 pass，以及每个 pass 对应哪个 target。
- `RenderQueue`：把某个 pass 下已验证的 renderables 变成 `RenderingItem`，并按 `PipelineKey` 排序与去重。
- `RenderingItem`：backend 可直接消费的一次 draw 上下文。

## Infra 的职责

`infra` 是 `core` 抽象的工程级实现层：

- `ShaderCompiler` / `ShaderReflector` / `CompiledShader`
- `loadBlinnPhongMaterial()`
- `ObjLoader` / `GLTFLoader`
- `Window` 的 SDL3 / GLFW 实现

其中 shader 子系统现在除了 descriptor / UBO 反射，还会输出 vertex input contract，供 `SceneNode` 校验 mesh layout。

## 总结

可以把当前架构压缩成四条规则：

- 分层规则：`core` 定义抽象，`infra` 接实现，`backend` 做 Vulkan 落地。
- 资源规则：所有 GPU 资源走 `IGpuResource + dirty + syncResource()`。
- 绘制规则：所有 draw 必须走 `Scene -> FrameGraph -> RenderQueue -> RenderingItem`。
- 身份规则：pipeline 身份来自 object/material pipeline signatures，skinning 差异由 material-side variants 表达。

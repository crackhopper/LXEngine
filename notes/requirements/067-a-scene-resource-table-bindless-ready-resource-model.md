# REQ-067-a: SceneResourceTable 与 Bindless-Ready 资源模型

> 2026-06-02 新增：把场景渲染资源从 `SceneNode is IRenderable` 的临时组合模型，收敛为由 `SceneResourceTable` 统一持有、组件通过 handle 引用、渲染器通过 snapshot 消费的 bindless-ready 资源模型。

## 背景

当前实时渲染路径里，`SceneNode` 直接继承 `IRenderable`，并通过 `MeshComponent`、`MaterialComponent`、`SkeletonComponent` 拼出 `ValidatedRenderablePassData`。离线渲染路径又在 `src/backend/vulkan/offline/gpu_scene_builder.*` 中重新定义 `GpuTriangle`、`GpuMaterial`、`GpuCameraParams`，把 mesh 展平成 triangle list 后上传给 compute shader。

这两条路径解决了首版功能，但资源边界开始重复：

| 当前对象 | 当前事实 | 问题 |
|---|---|---|
| `Mesh` | 持有 `VertexBuffer`、`IndexBuffer`、bounds | 名字偏语义对象，但实际更接近 buffer-level mesh；首版应改造为引用 `GeometryStorage` 的 `MeshBuffer` |
| `MaterialInstance` | 持有参数 buffer、shader reflection、descriptor resources | 已经接近 shader-facing material instance，不应再平行新增 `MaterialGpuView` |
| `SceneNode` | 继承 `IRenderable`，同时也是 camera/light/empty 的层级节点 | 让所有 node 在类型上都像 renderable，组件语义被压扁 |
| offline `GpuTriangle` | 每个 triangle 复制三个世界空间顶点和 face normal | 丢失 index/vertex 共享关系，normal/uv/tangent 插值不可持续 |

后续 bindless realtime pipeline 和 offline compute/path tracing 都需要一套稳定的资源索引关系：mesh、material、texture、object、light、camera 由场景级表管理，shader 或 upload path 只拿 compact index 和 snapshot。我们先建立这个资源模型，再让 realtime bindless 和 offline renderer 消费它。

## 目标

1. 引入 `SceneResourceTable`，作为场景长期渲染资源的持有者和生命周期管理入口。
2. 让 `SceneNode` 回到层级、transform、component 容器职责，不再继承 `IRenderable`。
3. 让 renderable 能力成为 component 能力：组件保存 handle，导出或更新 `ObjectResource`。
4. 引入 `GeometryStorage`，把现有 `Mesh` 改造为 `MeshBuffer`，并复用现有 `MaterialInstance`、Texture、Light、Camera，不平行创造语义重复的 View 类。
5. 通过不可变 snapshot 为 realtime renderer、offline renderer 和后续 bindless upload 提供统一输入。
6. 为资源引用计数、pending release、memory pool、global geometry buffer、bindless descriptor table 留出接口。

## 需求

### R1: SceneResourceTable 管理场景长期资源

新增 `SceneResourceTable`，作为 scene 级渲染资源注册表。

它 SHALL 管理至少以下资源族：

| 资源族 | 说明 |
|---|---|
| mesh | 几何资源或几何切片 |
| material | 材质实例与 shader-facing 参数 |
| texture | 2D / 3D / cube / array texture metadata 与资源引用 |
| object | 可渲染对象实例，引用 mesh/material/transform/visibility |
| light | 场景光源参数 |
| camera | 渲染相机参数 |

`SceneResourceTable` SHALL 提供稳定 handle，而不是让长期引用直接保存裸 `u32 index`。handle SHOULD 至少包含 index 和 generation，防止释放后复用 slot 导致 stale reference。

### R2: Resource 与 View/Record 分层

长期资源 SHALL 使用 Resource / Instance 对象表达拥有关系和生命周期；渲染 snapshot SHALL 使用 View / Record 表达紧凑索引。

Resource / Instance / Entry 是 `SceneResourceTable` 内部的管理壳，不 SHALL 重新定义与现有 core 类语义重复的数据模型。它们 SHOULD 优先持有或内嵌现有类型：

| Table entry | 复用对象 |
|---|---|
| geometry entry | `GeometryStorage` |
| mesh entry | `MeshBuffer` |
| material entry | `MaterialInstance` |
| texture entry | `Texture` / `CombinedTextureSampler` 所需的 texture + sampler/binding metadata |
| light entry | `LightBase` 派生类或其参数数据 |
| camera entry | `CameraComponent` 当前持有的相机参数语义 |

示例边界：

| 层级 | 用途 | 示例 |
|---|---|---|
| Resource | 被 `SceneResourceTable` 管理，可变、有生命周期 | `MeshResource`、`MaterialResource`、`ObjectResource` |
| Handle | 组件和资源之间的长期引用 | `MeshHandle`、`MaterialHandle`、`ObjectHandle` |
| View/Record | snapshot 或 shader upload 使用的紧凑关系 | `ObjectInstanceView { meshIndex, materialIndex, ... }` |

组件、scene node、editor state SHALL 保存 handle，不 SHALL 保存 snapshot index。

命名带 `View` 后缀的类型 SHALL 是非拥有视图：它们只保存 handle、index、range 或只读 metadata，不 SHALL 持有 `shared_ptr` 或直接拥有资源数据。若某个类型需要持有 `shared_ptr`、生命周期状态、dirty 标记或释放队列，它应命名为 Resource / Instance / Entry，而不是 View。

### R3: GeometryStorage 与 MeshBuffer

首版 SHALL 引入 `GeometryStorage`，收束现有 `VertexBuffer` 和 `IndexBuffer` 作为底层几何数据。它是 `SceneResourceTable` 管理的底层几何存储，不是新的顶点/索引数据模型。

`GeometryStorage` SHALL 至少保存或管理：

| Field | Meaning |
|---|---|
| vertex buffer | 复用现有 `VertexBuffer` / `IVertexBuffer` |
| index buffer | 复用现有 `IndexBuffer` |
| layout/topology access | 从现有 buffer/layout 读取 |
| storage identity / dirty state | 供 resource table 和 backend upload 追踪 |

现有 `Mesh` SHALL 改造为 `MeshBuffer`。`MeshBuffer` 表达一个可渲染 mesh 在 `GeometryStorage` 中的切片，不直接拥有 vertex/index 数据。

`MeshBuffer` SHALL 表达：

| Field | Meaning |
|---|---|
| geometry storage handle | 指向 `GeometryStorage` |
| vertex/index offset | 当前 mesh 在 storage 中的切片 |
| vertex/index count | 当前 mesh 使用范围 |
| topology | primitive assembly 语义 |
| bounds / closedVolume | 几何语义 |

首版一对一 mesh 可以创建一个只被该 mesh 使用的 `GeometryStorage`，保持现有加载器和实时渲染行为兼容。后续 global packed geometry buffer 可以让多个 `MeshBuffer` 指向同一个 `GeometryStorage` 的不同 range。

offline renderer SHALL 直接消费 `GeometryStorage + MeshBuffer` 关系，保留 vertex/index 引用，不再依赖 backend 私有 triangle flatten 作为基础几何结构。

### R4: MaterialInstance 扩展为 shader-facing 材质资源

现有 `MaterialInstance` SHALL 继续作为材质实例，不新增平行 `MaterialGpuView`。

`SceneResourceTable` 的 material entry SHALL 管理 `MaterialInstance` 本身或其生命周期壳，不 SHALL 新建另一套 material parameter / texture binding 数据模型。新增 record 只能是 snapshot/upload 用的紧凑输出，不拥有材质数据。

`MaterialInstance` SHALL 增加或暴露用于 resource table / bindless / offline 的稳定查询能力：

| 能力 | 说明 |
|---|---|
| parameter bytes | 获取 reflected parameter buffer 字节 |
| texture references | 获取材质引用的 texture handle / binding relation |
| shader record | 生成 material table 可上传记录 |
| pass metadata | 按 pass 查询 shader、bindings、pass enabled 状态 |

材质导出的 shader record SHALL 不直接持有 Vulkan descriptor；descriptor table 和 upload 属于 backend。

### R5: Texture / Light / Camera 复用现有类并补 metadata

Texture 不 SHALL 拆成多个互不相关的资源池。应通过统一 metadata 区分：

```text
Texture2D
Texture3D
TextureCube
Texture2DArray
```

当前 `TextureDimension` 已有 `Texture2D` 与 `TextureCube`；`Texture3D`、`Texture2DArray` 属于后续 metadata 扩展，不要求首版新增平行 Texture 类。

Light 和 Camera SHALL 优先复用现有 component / resource 类型，只在 snapshot 或 upload 时导出紧凑 record。`LightView`、`CameraView` 若出现，只能是非拥有 handle/record，不得复制当前 `LightBase` / `CameraComponent` 的长期状态。

如果需要 frame graph 输出资源，SHALL 与 scene 长期资源分层：

| 表 | 管理对象 |
|---|---|
| `SceneResourceTable` | mesh/material/texture/object/light/camera 等长期场景资源 |
| Frame/resource graph 表 | render target、depth、history buffer、storage image、readback 等帧资源 |

### R6: SceneNode 不再继承 IRenderable

`SceneNode` SHALL 回到以下职责：

- 层级关系。
- local/world transform。
- component 容器。
- scene path / editor identity。

`SceneNode` SHALL NOT 继承 `IRenderable`。

可渲染能力 SHALL 由 component 提供，建议引入：

```text
IRenderableComponent : IComponent
```

`IRenderableComponent` SHALL 通过 `SceneResourceTable` 持有或更新 `ObjectResource`，并可导出 renderable 状态。camera、light、empty node 不需要伪装成 renderable。

### R7: ObjectResource 与 ObjectInstanceView

`ObjectResource` SHALL 由 `SceneResourceTable` 持有，表达场景中的可渲染对象实例。

长期对象资源 SHOULD 包含：

| Field | Meaning |
|---|---|
| mesh handle | 引用 mesh resource |
| material handle | 引用 material resource |
| optional skeleton handle | skinned mesh 资源 |
| objectToWorld / worldToObject | 当前变换 |
| worldBounds | culling / picking / offline BVH 输入 |
| visibility / layer mask | pass 与 camera culling |
| debug/runtime flags | debug-only、runtime-only 等 |

渲染 snapshot SHALL 导出紧凑 `ObjectInstanceView`：

```cpp
struct ObjectInstanceView {
  u32 meshIndex;
  u32 materialIndex;
  Mat4f objectToWorld;
  Mat4f worldToObject;
  BoundingBox worldBounds;
  bool visible;
};
```

snapshot index SHALL 只在 snapshot 生命周期内有效。

### R8: RenderSceneSnapshot / Upload View

`SceneResourceTable` SHALL 能生成不可变 snapshot，供 realtime/offline renderer 消费。

snapshot SHALL 满足：

- 导出 compact mesh/material/texture/object/light/camera index。
- 包含 dirty/version 信息，便于 backend 增量 upload。
- 构建 snapshot 时可加锁；backend 上传期间不应长期持有 `SceneResourceTable` 写锁。
- snapshot 内容不可变，避免渲染线程看到 editor 线程的半更新状态。

推荐流程：

```text
lock SceneResourceTable
  copy/compact active resources into RenderSceneSnapshot
unlock
backend uploads snapshot
```

### R9: View 合法性与 handle-only 资源生命周期

`SceneResourceTable` SHALL 是 scene 渲染资源的强 owner。scene node、component、editor state、render queue 和 offline compiler 等外部对象 SHALL NOT 长期持有 mesh/material/texture/object/light/camera 资源的 `shared_ptr` 强引用；它们 SHALL 保存对应 handle 或 View。

资源生命周期 SHALL 由 `SceneResourceTable` 统一管理：

| 角色 | 生命周期策略 |
|---|---|
| `SceneResourceTable` | 强持有 Resource / Instance / Entry，分配 handle，维护 generation、dirty、引用关系和 pending release |
| component / scene node / editor state | 保存 handle，不拥有底层资源 |
| `View` | 只保存 handle、index、range 或只读 metadata，不延长资源生命周期 |
| snapshot | 复制 compact record，在 snapshot 生命周期内自洽，不依赖外部强引用 |

View 合法性 SHALL 通过 `SceneResourceTable` 判断，而不是通过 View 自己持有强引用判断。推荐接口形态：

```text
resolve(MeshHandle) -> optional<MeshResourceRef>
isAlive(MeshHandle) -> bool
```

合法性判断 SHALL 至少检查：

- handle index 在 table 范围内。
- generation 匹配。
- entry 尚未进入 released / pending-free 不可访问状态。
- resource type 与 handle type 匹配。

`SceneResourceTable` SHALL 手动维护资源引用关系。至少需要覆盖：

- object entry 引用 mesh/material/skeleton。
- material entry 引用 texture。
- scene node/component 引用 object 或 camera/light 等 entry。
- asset cache 或 editor catalog 如需保留资源，也通过 handle-level retain/pin 机制表达。

当 `SceneNode` 或 component 删除时，它 SHALL 通知 `SceneResourceTable` 释放或解除相关 handle 关系。table SHALL 递减内部引用关系或移除 owner edge，并在资源不再被任何 object/material/catalog/editor pin 使用时把 entry 标记为 pending release。后续底层 memory pool 接入时，pending release SHALL 转成 GPU memory / descriptor slot 回收。

handle-only 方案 SHALL 明确 stale handle 行为：访问已释放或 generation 不匹配的 handle 必须返回空 optional、错误状态或 debug assertion，不得静默返回新 slot 中的资源。

### R10: Realtime bindless 基础消费

本 REQ SHALL 建立 bindless-ready 数据模型，但不要求一次性完成完整 bindless renderer。

首版 realtime 消费 SHALL 至少能从 snapshot 生成当前 render queue 需要的 `RenderingItem` 或等价结构，并保持现有 graphics path 行为不退化。

后续 bindless renderer MAY 从同一 snapshot 生成：

- global vertex/index buffers。
- material buffer。
- object/instance buffer。
- texture descriptor table。
- indirect draw / draw command table。

### R11: Offline renderer 消费同一资源模型

offline renderer 后续 SHALL 不再从 backend 私有 `GpuSceneBuilder` 重新发明 mesh/material 基础结构。

offline path SHALL 从 snapshot 或 resource table view 生成：

- vertex/index buffers。
- primitive record buffer。
- material record buffer。
- object/instance buffer。
- BVH input。
- camera/light params。

offline BVH SHALL 基于 primitive/index/vertex 关系构建，不应强制把 mesh 展平成只含 face normal 的 `GpuTriangle`。ray hit 后 SHOULD 使用 barycentric 插值 normal / uv / tangent。

## 测试

### T1: Handle 生命周期

新增测试覆盖：

- 创建、释放、复用 mesh/material/object slot。
- stale handle 因 generation 不匹配而失效。
- component 保存 handle 时不会误指向复用后的新资源。

### T2: MeshBuffer / GeometryStorage 兼容

新增测试覆盖：

- 现有 mesh 创建路径迁移为 `MeshBuffer` 创建路径，并可由 `vertexBuffer + indexBuffer + bounds` 生成一对一 `GeometryStorage`。
- `MeshBuffer` 能导出 geometry handle、vertex/index range、layout、topology、bounds。
- packed storage 与一对一 storage 生成相同 mesh-level signature。

### T3: MaterialInstance shader record

新增测试覆盖：

- reflected parameter bytes 可被查询。
- texture references 可被收集为 handle/index relation。
- material pass enabled / shader binding 信息不因新 resource table 丢失。

### T4: SceneNode / RenderableComponent 分离

新增测试覆盖：

- camera/light/empty scene node 不出现在 renderable collection。
- 含 `IRenderableComponent` 的 node 通过 `SceneResourceTable` 产生 `ObjectResource`。
- transform 或 visibility 改变会标记 object dirty。

### T5: Snapshot 稳定性

新增测试覆盖：

- snapshot index 在单个 snapshot 内连续稳定。
- table 更新后旧 snapshot 不被修改。
- backend 使用 snapshot 上传期间不需要持有 table 写锁。

### T6: Offline 几何插值合同

新增或更新 offline GPU scene 测试，验证：

- offline primitive record 保留 vertex/index 引用。
- shader 或 CPU-side contract 能恢复 per-vertex normal / uv。
- 不再依赖只含 flat normal 的 triangle buffer 作为唯一几何输入。

## 修改范围

- `src/core/scene/`
- `src/core/asset/mesh.*`
- `src/core/asset/material_instance.*`
- `src/core/rhi/`
- `src/core/frame_graph/render_queue.*`
- `src/backend/vulkan/`
- `src/backend/vulkan/offline/`
- `src/core/offline/`
- `src/infra/offline/`
- `src/test/integration/`
- `openspec/specs/` 新增或更新 scene resource table / bindless resource model 相关 spec
- `notes/subsystems/` 与 `notes/concepts/` 对应文档

## 边界与约束

- 本 REQ 不要求一次性完成完整 bindless renderer。
- 本 REQ 不要求引入 Vulkan hardware ray tracing。
- 本 REQ 不要求立即实现 memory pool；但资源生命周期接口必须为 pending release / pool 接入留出边界。
- 本 REQ 不把 frame graph 临时 render target 混进 `SceneResourceTable`。
- 本 REQ 不移除现有 material YAML 或 shader reflection；应复用现有反射能力。
- 本 REQ 不要求所有 debug draw 首版都迁移；但新 renderable component 模型必须允许 debug mesh 通过组件参与渲染。

## 依赖

- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/pipeline-signature/spec.md`
- `openspec/specs/pipeline-cache/spec.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/concepts/material/index.md`
- `REQ-063-a: Compute Pipeline Foundation`
- `REQ-054-b: Vulkan Compute Offline Renderer MVP`

## 后续工作

- Realtime bindless renderer：descriptor indexing、texture descriptor table、material/object buffer、draw command indirection。
- Offline renderer resource model migration：从 snapshot 生成 primitive/BVH/material buffers，移除 backend 私有 triangle flatten 基础结构。
- Memory pool / resource allocator：把 `SceneResourceTable` 的 pending release 接入底层 GPU memory pool。
- Debug renderable component：把 debug mesh、overlay mesh 和 runtime debug draw 纳入统一 `IRenderableComponent` 收集路径。

## 实施状态

Draft，未实施。

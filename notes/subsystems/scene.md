# Scene

> 场景层负责把可渲染对象（mesh + material + optional skeleton）组装成一帧要 submit 的 `RenderingItem`。`Scene::buildRenderingItem(pass)` 是整个引擎里唯一把"场景状态"转换为"draw call 数据"的入口。
>
> 相关 spec: `openspec/specs/pipeline-key/spec.md` R3（`RenderingItem` 字段）、`openspec/specs/render-signature/spec.md` R7（pass 参数贯通）、`openspec/specs/frame-graph/spec.md`（Scene 扩展为多 renderable）

## 核心抽象

### `RenderingItem` (`src/core/scene/scene.hpp:14`)

一次 draw call 的完整上下文值对象：

```cpp
struct RenderingItem {
    IShaderPtr                       shaderInfo;
    ObjectPCPtr                      objectInfo;           // push constant 源
    IRenderResourcePtr               vertexBuffer;
    IRenderResourcePtr               indexBuffer;
    std::vector<IRenderResourcePtr>  descriptorResources;  // material + camera + light + skeleton 等
    ResourcePassFlag                 passMask;
    StringID                         pass;                 // 当前 pass 的 StringID
    PipelineKey                      pipelineKey;          // 身份
    MaterialPtr                      material;             // 用于 PipelineBuildInfo 派生
};
```

### `Scene` (`src/core/scene/scene.hpp:30`)

```cpp
class Scene {
public:
    std::vector<IRenderablePtr>  m_renderables;
    CameraPtr                    camera;
    DirectionalLightPtr          directionalLight;

    // 单 renderable 快捷路径（历史兼容 — 实际走 m_renderables[0]）
    RenderingItem buildRenderingItem(StringID pass);

    // FrameGraph 遍历走这个
    RenderingItem buildRenderingItemForRenderable(
        const IRenderablePtr &renderable, StringID pass) const;

    const std::vector<IRenderablePtr> &getRenderables() const;
};
```

### `IRenderable` (`src/core/scene/object.hpp:50`)

```cpp
class IRenderable {
public:
    virtual IRenderResourcePtr getVertexBuffer() const = 0;
    virtual IRenderResourcePtr getIndexBuffer() const = 0;
    virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
    virtual ResourcePassFlag getPassMask() const = 0;
    virtual IShaderPtr getShaderInfo() const = 0;
    virtual ObjectPCPtr getObjectInfo() const { return nullptr; }

    // REQ-007 之后：pass-aware 签名
    virtual StringID getRenderSignature(StringID pass) const = 0;
};
```

### `RenderableSubMesh` (`src/core/scene/object.hpp:69`)

```cpp
struct RenderableSubMesh : public IRenderable {
    MeshPtr                       mesh;
    MaterialPtr                   material;        // 实际指向 MaterialInstance
    std::optional<SkeletonPtr>    skeleton;
    ObjectPCPtr                   objectPC;

    // IRenderable 接口全部 override
    // getDescriptorResources 聚合 material 资源 + skeleton UBO（若有）
    // getRenderSignature 做 compose(ObjectRender, {meshSig, skelSig})
};
```

### 场景级 UBO

- **`CameraUBO`** (`src/core/scene/camera.hpp:11`) — view / proj / eye，继承 `IRenderResource`
- **`DirectionalLightUBO`** (`src/core/scene/light.hpp:8`) — dir / color
- **`ObjectPC`** (`src/core/scene/object.hpp:15`) — push constant，128 字节缓冲，继承 `IRenderResource`

这些由 `Scene` / `Camera` / `DirectionalLight` 持有，通过 `renderable->getDescriptorResources()` **不会**返回 —— scene UBO 由 `VulkanRenderer::initScene` 显式注入到 `RenderingItem::descriptorResources`。

## 典型用法

```cpp
#include "core/scene/scene.hpp"
#include "core/scene/pass.hpp"
#include "core/scene/object.hpp"

using namespace LX_core;

// 1. 构建 renderable
auto mesh     = Mesh::create(vertexBuffer, indexBuffer);
auto material = LX_infra::loadBlinnPhongMaterial();
auto skeleton = Skeleton::create({});   // 空骨骼也要构造
auto renderable = std::make_shared<RenderableSubMesh>(mesh, material, skeleton);

// 2. 场景
auto scene = Scene::create(renderable);

// 3. 配置 camera / light (scene 构造时已创建)
scene->camera->position = {0, 0, 3};
scene->camera->target   = {0, 0, 0};
scene->camera->up       = {0, 1, 0};
scene->camera->updateMatrices();

// 4. 产出一帧的 RenderingItem（单 pass 快捷路径）
auto item = scene->buildRenderingItem(Pass_Forward);
// item.pipelineKey 已填充
// item.pass == Pass_Forward
// item.descriptorResources 包含 material UBO + 可选 skeleton UBO

// 5. 多 pass 走 FrameGraph
auto frameGraph = std::make_shared<FrameGraph>();
frameGraph->addPass({Pass_Forward, {}, {}});
frameGraph->buildFromScene(*scene);
// 每个 pass 的 queue 已填满
```

## 调用关系

```
Scene::buildRenderingItem(pass)
  │
  ├── 从 m_renderables.front() 取 renderable
  │   （或 buildRenderingItemForRenderable(renderable, pass) 逐个处理）
  │
  ├── 从 renderable 收集值字段:
  │     item.vertexBuffer        = renderable->getVertexBuffer()
  │     item.indexBuffer         = renderable->getIndexBuffer()
  │     item.objectInfo          = renderable->getObjectInfo()
  │     item.descriptorResources = renderable->getDescriptorResources()
  │     item.shaderInfo          = renderable->getShaderInfo()
  │     item.passMask            = renderable->getPassMask()
  │     item.pass                = pass
  │
  ├── 若 renderable 是 RenderableSubMesh 且 mesh + material 都有：
  │     item.material    = sub->material
  │     objectSig        = sub->getRenderSignature(pass)
  │     materialSig      = sub->material->getRenderSignature(pass)
  │     item.pipelineKey = PipelineKey::build(objectSig, materialSig)
  │
  └── 返回 item

────── 后续 ──────

VulkanRenderer::initScene(scene):
  │ 把 scene->camera->getUBO() / scene->directionalLight->getUBO()
  │ 注入到 RenderingItem::descriptorResources
  │ (通过 render_queue / frame_graph 路径)

VulkanCommandBuffer::bindResources(item, pipeline):
  │ 对 pipeline 反射 binding 里每一条：
  │   按 binding.name 在 item.descriptorResources 里找
  │     IRenderResource::getBindingName() == StringID(binding.name) 的那个
  │   更新 descriptor set
```

## 注意事项

- **Scene 曾经只持有单个 renderable**: 老代码是 `IRenderablePtr mesh;`。REQ-003b 扩展成 `std::vector<IRenderablePtr> m_renderables`。单 renderable 的 constructor + `buildRenderingItem(pass)` 快捷路径保留，内部 `m_renderables.front()`。
- **`RenderingItem::material` 是 REQ-003b 新增字段**: `PipelineBuildInfo::fromRenderingItem(item)` 需要从 material 派生 `renderState`，所以 item 要持 material 的 shared_ptr。老代码没这个字段。
- **Scene UBO 不在 `getDescriptorResources()` 里**: `RenderableSubMesh::getDescriptorResources()` 只返回 material 资源和可选的 skeleton UBO；camera / light UBO 由 `VulkanRenderer::initScene` 在外部注入。这种"scene 级资源与 per-object 资源分开"的设计让 material 的资源列表保持纯粹。
- **Push constant 通过 `ObjectPC`**: `ObjectPC : IRenderResource` 存 128 字节的 model 矩阵 + flags。`RenderableSubMesh` 构造时创建一个，由 material 的 passFlag 决定它属于哪一 pass。
- **Pass 参数贯穿**: 从 `Scene::buildRenderingItem(pass)` 开始，pass 穿过 `IRenderable::getRenderSignature(pass)` → `Mesh::getRenderSignature(pass)` → `IMaterial::getRenderSignature(pass)` → `MaterialTemplate::getRenderPassSignature(pass)`。任何一环如果弄丢 pass 参数都会导致 pipeline key 混乱。

## 测试

- `src/test/integration/test_vulkan_command_buffer.cpp` — 端到端路径：构建 `RenderingItem` → bindResources → draw
- `src/test/integration/test_pipeline_identity.cpp` — `Scene::buildRenderingItem` 的 pipelineKey 填充
- `src/test/integration/test_frame_graph.cpp` — 多 renderable × 多 pass 扫描

## 延伸阅读

- `openspec/specs/pipeline-key/spec.md` — `RenderingItem::pipelineKey + pass` 字段契约
- `openspec/specs/render-signature/spec.md` R7 — `IRenderable::getRenderSignature(pass)` 要求
- `openspec/specs/frame-graph/spec.md` — `Scene` 的 `getRenderables()` 扩展
- `notes/subsystems/pipeline-identity.md` — `PipelineKey` 的构造
- `notes/subsystems/material-system.md` — material 如何贡献 `materialSig`
- 归档: `openspec/changes/archive/2026-04-10-pipeline-key-rendering-item/` — `RenderingItem` 首次引入 `pipelineKey` 字段

# Geometry (Mesh + Vertex / Index Buffer)

> Mesh + 顶点布局 + 索引缓冲的核心抽象。顶点布局贡献 pipeline 身份的一半（通过 `compose(TypeTag::VertexLayout, ...)`），所以 `VertexLayout` / `VertexLayoutItem` 的设计直接影响 pipeline cache 的命中率。
>
> 相关 spec: `openspec/specs/render-signature/spec.md`（vertex layout 贡献 pipeline 身份），`openspec/specs/mesh-loading/spec.md`

## 核心抽象

### `Mesh` (`src/core/resources/mesh.hpp:15`)

```cpp
class Mesh {
public:
    using Ptr = std::shared_ptr<Mesh>;

    static Ptr create(VertexBufferPtr vb, IndexBufferPtr ib);

    uint32_t getVertexCount() const;
    uint32_t getIndexCount() const;
    const VertexLayout &getVertexLayout() const;
    PrimitiveTopology   getPrimitiveTopology() const;
    StringID            getRenderSignature(StringID pass) const;

    VertexBufferPtr             vertexBuffer;
    std::shared_ptr<IndexBuffer> indexBuffer;
};
```

`Mesh` 本身是 thin holder —— 两个 `shared_ptr` + 派生的两个 accessor。

### `VertexLayoutItem` (`src/core/resources/vertex_buffer.hpp:50`)

描述一个顶点属性：

```cpp
struct VertexLayoutItem {
    std::string            name;        // "position" / "normal" / "uv" / ...
    uint32_t               location;    // GLSL layout(location = N)
    VertexAttributeType    type;        // Float3 / Float4 / Int4 / ...
    VertexInputRate        inputRate;   // Vertex / Instance
    uint32_t               offset;      // std140 内偏移

    StringID getRenderSignature() const;   // Intern("0_position_Float3_Vertex_0") 等
};
```

### `VertexLayout` (`src/core/resources/vertex_buffer.hpp:92`)

有序 item 列表 + 总 stride：

```cpp
class VertexLayout {
public:
    const std::vector<VertexLayoutItem> &items() const;
    uint32_t stride() const;
    StringID getRenderSignature() const;
    // → compose(VertexLayout, {item1Sig, item2Sig, ..., Intern(stride)})
};
```

### `IVertexBuffer` + `VertexBuffer<V>` (`src/core/resources/vertex_buffer.hpp:151` / `:168`)

- `IVertexBuffer : IRenderResource` — 运行期接口，抹掉具体顶点类型
- `VertexBuffer<V> : IVertexBuffer` — 模板具体类，持有 `std::vector<V>` 并暴露 layout

典型顶点类型（来自 `vertex_buffer.hpp`）:
- `VertexPos` (`:279`)
- `VertexPosColor` (`:292`)
- `VertexPosUV` (`:309`)
- `VertexPBR` (`:327`)
- `VertexSkinned` (`:348`) — pos + normal + uv + tangent + boneIds + weights
- `VertexUI` (`:373`)
- `VertexNormalTangent` (`:389`)
- `VertexBoneWeightIndex` (`:404`)
- `VertexPosNormalUvBone` (`:420`) — project 的主力类型

每种类型通过 CRTP `VertexBase<T>` 自描述 layout。

### `VertexFactory` (`src/core/resources/vertex_buffer.hpp:207`)

维护 `T → VertexLayout` 的注册表，让 pipeline 构建可以按类型名反查 layout。

### `IndexBuffer` (`src/core/resources/index_buffer.hpp:50`)

```cpp
class IndexBuffer : public IRenderResource {
public:
    static IndexBufferPtr create(std::vector<uint32_t> indices,
                                 PrimitiveTopology topology = TriangleList);

    size_t              indexCount() const;
    PrimitiveTopology   getTopology() const;
    // ...
};
```

`PrimitiveTopology` 枚举通过自由函数 `topologySignature(PrimitiveTopology)` 返回叶子 `StringID`。

## 典型用法

```cpp
#include "core/resources/mesh.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/resources/index_buffer.hpp"

using namespace LX_core;
using V = VertexPosNormalUvBone;

// 顶点
auto vb = VertexBuffer<V>::create({
    V({-1,  1, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}, {0, 0, 0, 0}, {1, 0, 0, 0}),
    V({ 1,  1, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}, {0, 0, 0, 0}, {1, 0, 0, 0}),
    V({ 1, -1, 0}, {0, 0, 1}, {1, 1}, {1, 0, 0, 1}, {0, 0, 0, 0}, {1, 0, 0, 0}),
});

// 索引
auto ib = IndexBuffer::create({0, 1, 2});

// Mesh
auto mesh = Mesh::create(vb, ib);

// Mesh 贡献 pipeline 身份
StringID meshSig = mesh->getRenderSignature(Pass_Forward);
// → compose(MeshRender, {
//     compose(VertexLayout, {
//       Intern("0_position_Float3_Vertex_0"),
//       Intern("1_normal_Float3_Vertex_12"),
//       Intern("2_uv_Float2_Vertex_24"),
//       Intern("3_tangent_Float4_Vertex_32"),
//       Intern("4_boneIds_Int4_Vertex_48"),
//       Intern("5_weights_Float4_Vertex_64"),
//       Intern("80"),   // stride
//     }),
//     Intern("tri"),
//   })
```

## 调用关系

```
Loader (obj / gltf / test 手写)
  │
  ├── VertexBuffer<V>::create(vertices)
  ├── IndexBuffer::create(indices, TriangleList)
  │
  ▼
Mesh::create(vb, ib) → MeshPtr
  │
  ▼
RenderableSubMesh { mesh, material, skeleton }
  │
  ├── sub->getRenderSignature(pass)
  │     └── mesh->getRenderSignature(pass)
  │           └── compose(MeshRender, {
  │                 vb->getLayout().getRenderSignature(),
  │                 topologySignature(ib->getTopology())
  │               })
  │
  ▼
Scene::buildRenderingItem(pass)
  ├── item.vertexBuffer = mesh->vertexBuffer （包装成 IRenderResourcePtr）
  ├── item.indexBuffer  = mesh->indexBuffer
  └── item.pipelineKey 包含 vertex layout 签名

Backend:
PipelineBuildInfo::fromRenderingItem(item)
  └── info.vertexLayout = cast<IVertexBuffer>(item.vertexBuffer)->getLayout()
  └── info.topology     = cast<IndexBuffer>(item.indexBuffer)->getTopology()
      ↓
VulkanPipeline 用 info.vertexLayout 生成 VkVertexInputAttributeDescription[]
```

## 注意事项

- **`VertexLayoutItem::getRenderSignature` 的格式是 `"location_name_type_inputRate_offset"`**: 这个字符串被 intern 一次后永远不变。**任何** layout 字段（包括 offset！）的变化都产生新 `StringID`，也就会造成新的 `PipelineKey`。offset 之所以参与是因为两个 layout 即便 attribute 相同，interleaved 顺序不同，vertex shader 读取路径不同，就是不同 pipeline。
- **Stride 也参与 compose**: `compose(VertexLayout, {items..., Intern(stride)})` 的最后一个 field 是 `Intern(to_string(stride))`。这样 pad 过的 layout 和紧凑的 layout 也能区分。
- **Topology 是叶子**: `topologySignature` 返回 `Intern("tri")` / `Intern("line")` / `Intern("point")` 这类叶子字符串，**不** compose。`TypeTag` 枚举里故意没有 `Topology`。
- **`Mesh::getRenderSignature(pass)` 当前忽略 pass**: 签名统一要求接 pass 参数，但目前 `Mesh` 的实现不读它。未来如果要"同一个 mesh 在 `Pass_Shadow` 里剔除 uv/color 属性"，就会在这里消费 pass。
- **Vertex buffer 在 `RenderingItem` 里的类型是 `IRenderResourcePtr`**: 不是 `IVertexBufferPtr`。需要 `dynamic_pointer_cast<IVertexBuffer>` 才能拿到 layout。`PipelineBuildInfo::fromRenderingItem` 就是这样做的。

## 测试

- `src/test/integration/test_vulkan_buffer.cpp` — GPU buffer 上传路径
- `src/test/integration/test_pipeline_identity.cpp` — `VertexLayout::getRenderSignature` 的各项 scenario（顺序敏感、item 增删产生新 key）
- `src/test/test_render_triangle.cpp` — 端到端绘制用到 `VertexPosNormalUvBone`

## 延伸阅读

- `openspec/specs/render-signature/spec.md` — `VertexLayoutItem` / `VertexLayout` / `Mesh::getRenderSignature(pass)` 的 normative 要求
- `openspec/specs/mesh-loading/spec.md` — OBJ / GLTF loader 契约
- 归档: `openspec/changes/archive/2026-04-13-interning-pipeline-identity/` — REQ-007 迁移 vertex layout 到 compose

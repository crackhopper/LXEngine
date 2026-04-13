# Skeleton

> 骨骼动画资源，住在 `core/resources/`，直接作为一个资源管理器使用，不走 `IComponent` 抽象。REQ-001 专门把 Skeleton 从 `scene/components/` 迁到资源层，让它和 `Mesh` / `Material` 平级。
>
> 权威 spec: `openspec/specs/skeleton-resource/spec.md`

## 核心抽象

`src/core/resources/skeleton.hpp`:

### `Bone` (`:16`)

```cpp
struct Bone {
    std::string name;
    int parentIndex;         // -1 = 根
    Vec3f position;
    Quatf rotation;
    Vec3f scale = Vec3f(1, 1, 1);
};
```

### `SkeletonUBO` (`:24`) — GPU 端

```cpp
struct alignas(16) SkeletonUBO : public IRenderResource {
    SkeletonUBO(const std::vector<Bone> &bones, ResourcePassFlag passFlag);

    void updateBy(const std::vector<Bone> &bones);
    bool setBone(int index, const Bone &bone);

    // IRenderResource
    ResourceType getType() const override { return ResourceType::UniformBuffer; }
    const void *getRawData() const override { return m_bones; }
    u32 getByteSize() const override { return ResourceSize; }
    StringID getBindingName() const override { return StringID("Bones"); }

    static constexpr u32 ResourceSize = MAX_BONE_COUNT * sizeof(Mat4f);  // 128 根

private:
    Mat4f m_bones[MAX_BONE_COUNT];
    ResourcePassFlag m_passFlag;
};
```

### `Skeleton` (`:89`) — 资源管理器

```cpp
class Skeleton {
public:
    static SkeletonPtr create(const std::vector<Bone> &bones,
                              ResourcePassFlag passFlag = ResourcePassFlag::Forward);

    bool addBone(const Bone &bone);
    void updateUBO();
    SkeletonUboPtr getUBO() const;

    StringID getRenderSignature() const;  // 无 pass 参数 — Skeleton 对 pipeline 的贡献与 pass 无关
};
```

## 典型用法

```cpp
#include "core/resources/skeleton.hpp"

using namespace LX_core;

// 空骨骼（兼容 "SkeletonUBO 槽必须存在" 的 pipeline layout）
auto emptySkeleton = Skeleton::create({});

// 真实骨骼
std::vector<Bone> bones = {
    {"root",  -1, {0, 0, 0}, Quatf::identity(), {1, 1, 1}},
    {"spine",  0, {0, 1, 0}, Quatf::identity(), {1, 1, 1}},
    // ...
};
auto skeleton = Skeleton::create(bones);

// 每帧更新
skeleton->updateUBO();

// 绑定到 renderable
auto renderable = std::make_shared<RenderableSubMesh>(mesh, material, skeleton);

// 贡献 pipeline 身份
StringID skelSig = skeleton->getRenderSignature();
// → Intern("Skn1")
```

## 调用关系

```
RenderableSubMesh { mesh, material, optional<SkeletonPtr> skeleton }
  │
  ├── getDescriptorResources()
  │     │
  │     └── 返回 material->getDescriptorResources() + 可选 skeleton->getUBO()
  │
  └── getRenderSignature(pass)
        └── compose(ObjectRender, {
              mesh->getRenderSignature(pass),
              skeleton.has_value()
                ? skeleton.value()->getRenderSignature()  // Intern("Skn1")
                : StringID{}                               // id=0（无骨骼）
            })

Scene::buildRenderingItem(pass)
  │
  └── item.descriptorResources 里包含 SkeletonUBO（如果有）
        │
        └── Vulkan CommandBuffer::bindResources 按
            SkeletonUBO::getBindingName() == StringID("Bones")
            路由到对应 shader binding
```

## 注意事项

- **`MAX_BONE_COUNT = 128`**: 固定上限，shader 里的 `Bones { mat4 bones[128]; }` 也固定这个数字。超过就 assert。如果你的模型骨骼多于 128，需要同时改 shader 和这个常量。
- **`getBindingName() == "Bones"`**: 这是 `SkeletonUBO` 和 shader 里的 `uniform Bones { ... }` 之间的契约。`VulkanCommandBuffer::bindResources` 通过 `IRenderResource::getBindingName()` 匹配反射 binding name，所以 shader 的 block 必须叫 `Bones`。
- **`Skeleton::getRenderSignature()` 返回固定 `Intern("Skn1")`**: 不是 0，也不是骨骼数的 hash。"Skn1" 的语义是"启用骨骼"；**无骨骼** 的情况由调用方（`RenderableSubMesh::getRenderSignature`）通过返回 `StringID{}`（id=0）表达，而不是让 `Skeleton` 自己返回 0。这种"存在即表示启用"的设计让 Skeleton 类本身保持语义干净。
- **空 Skeleton 仍然占一个 pipeline slot**: 如果 pipeline layout 声明了 `set=3, binding=0` 的 SkeletonUBO，即使模型没有骨骼，你也得传一个 `Skeleton::create({})` 让 descriptor set 有东西可绑。`blinnphong_0.vert` 目前就有 `Bones` block，所以 test 都走这个空 skeleton 路径。
- **已经从 `IComponent` 抽象里剥离**: REQ-001 之前 Skeleton 是 `IComponent` 的唯一实现（`scene/components/`）。REQ-001 把整个 `components/` 目录删掉，Skeleton 升到 `core/resources/` 和 Mesh/Material 平级。

## 测试

- `src/test/test_render_triangle.cpp` — 用 `Skeleton::create({})` 空骨骼 + 主 shader
- `src/test/integration/test_vulkan_command_buffer.cpp` — 端到端路径包括 skeleton UBO 绑定
- `src/test/integration/test_pipeline_identity.cpp` — 有 / 无 skeleton 产生不同 `PipelineKey`

## 延伸阅读

- `openspec/specs/skeleton-resource/spec.md` — Skeleton 作为 core 资源的要求
- `openspec/specs/render-signature/spec.md` R6 — `IRenderable::getRenderSignature` 对 skeleton 的处理
- 归档: `openspec/changes/archive/2026-04-10-migrate-skeleton-to-resources/` — REQ-001 的迁移实施

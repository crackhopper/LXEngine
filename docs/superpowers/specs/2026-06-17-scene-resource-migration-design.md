# new_core Scene & Resource 迁移设计

## 目标

将 `src/core/scene/` 及相关资源概念迁移到 `src/new_core/`，使用 C++20 modules 组织。目标是：
- 能够正确、不遗漏地加载 scene.yaml 文件关联的所有数据到内存
- 概念纯净：一个概念一个类一个文件，小类型聚合
- 显式统一管理内存：MemoryAllocator + GameObject 基类 + StrongRef/WeakRef + GC

## 架构分层

```
new_common/memory     ← 基础设施：分配器、基类、引用类、GC
new_core/resource     ← 底层资源：连续内存排布的数据（可直接上传 GPU）
new_core/assets       ← 可定义/引用的资产：Mesh/Material/RenderObject/Camera/Light/RenderFeature
new_core/scene        ← 场景结构：SceneNode/SceneInfo/Environment
```

依赖方向：`scene → assets → resource → memory`（单向，无循环）

---

## Layer 1: new_common/memory

### allocator.cppm — MemoryAllocator
连续内存池分配器，为 Resource 层分配数据区。
- `allocate(usize size, usize alignment) → void*`
- `free(void* ptr)`
- `create<T, Args...>(Args&&...) → T*`（placement new）

### game_object.cppm — GameObject
所有 Resource/Asset/Scene 对象的基类。
- `std::atomic<u32> refCount`（与数据区分离）
- `TypeId typeId`（运行时类型标识）
- `addRef()` / `release()` / `getTypeId()`
- 虚析构

### ref.cppm — StrongRef<T> / WeakRef<T>
引用类，不持有所有权。
- **StrongRef**: 构造时 `addRef()`，析构时 `release()`；`get()`, `operator*`, `operator->`
- **WeakRef**: 不改变 ref count；`lock()` 返回有效指针或 nullptr

### gc.cppm — GC
- 内部维护已分配对象列表
- `registerObject(GameObject*)` / `unregisterObject(GameObject*)`
- `sweep()`: 扫描 refCount == 0 的对象，调用析构，释放内存

### memory.cppm — umbrella
```cpp
export import :Allocator;
export import :GameObject;
export import :Ref;
export import :GC;
```

---

## Layer 2: new_core/resource

所有 Resource 继承 GameObject，由 MemoryAllocator 创建。数据连续排布，ref count 分离。

### vertex_buffer.cppm — VertexBuffer
```cpp
class VertexBuffer : public GameObject {
    void* data;              // 连续顶点数据
    u32 vertexCount;
    u32 stride;
    VertexLayout layout;     // 字段名、类型、偏移（概念描述，不含 GPU 句柄）
};
// 保留旧代码 VertexLayout/VertexLayoutItem/预定义 vertex types (VertexPos, VertexPBR, VertexPosNormalUvBone)
```

### index_buffer.cppm — IndexBuffer
```cpp
class IndexBuffer : public GameObject {
    void* data;              // 连续索引数据
    u32 indexCount;
    PrimitiveTopology topology;
};
```

### texture_buffer.cppm — TextureBuffer
```cpp
class TextureBuffer : public GameObject {
    void* data;              // 像素数据
    u32 width, height;
    TextureFormat format;
    TextureDimension dimension;
    u32 mipLevels, arrayLayers;
    TextureContent content;
};
// 保留旧代码 TextureFormat/TextureContent/TextureDimension 枚举
```

### parameter_buffer.cppm — ParameterBuffer
```cpp
class ParameterBuffer : public GameObject {
    void* data;              // UBO/SSBO 字节
    u32 byteSize;
    StringID bindingName;
};
```

### resource.cppm — umbrella
```cpp
export import :VertexBuffer;
export import :IndexBuffer;
export import :TextureBuffer;
export import :ParameterBuffer;
```

---

## Layer 3: new_core/assets

### handle.cppm — Handle<T>
轻量 POD，index/generation 模式。
```cpp
template<typename T>
struct Handle { u32 index = kInvalid; u32 generation = 0; };
```

### types.cppm — 通用资产类型
- `ResourceUri`（结构体：std::string path + StringID signature）
- `AssetType` 枚举（Mesh, Material, RenderObject, Camera, Light, RenderFeature, ...）

### assets/mesh/

#### geometry_storage.cppm — GeometryStorage
```cpp
class GeometryStorage : public GameObject {
    StrongRef<VertexBuffer> vertexBuffer;
    StrongRef<IndexBuffer> indexBuffer;
};
```

#### mesh_buffer.cppm — Mesh
```cpp
class Mesh : public GameObject {
    StrongRef<GeometryStorage> storage;
    u32 vertexOffset, indexOffset;
    std::optional<u32> vertexCount, indexCount;
    BoundingBox bounds;
    bool closedVolume;
};
```

#### mesh.cppm (umbrella)
```cpp
export import :GeometryStorage;
export import :MeshBuffer;
```

### assets/render_object/

#### render_object.cppm — RenderObject
```cpp
class RenderObject : public GameObject {
    StrongRef<Mesh> mesh;
    StrongRef<MaterialInstance> material;
};
```

### assets/material/

#### parameter_envelope.cppm — MaterialParameterEnvelope
```cpp
enum class MaterialEnvelopeKind { Float, Rgb, Spectrum, Bool, String, Texture, Integer, MaterialRef, BsdfTable };

struct MaterialParameterEnvelope {
    MaterialEnvelopeKind kind;
    std::optional<f32> floatValue;
    std::optional<i32> integerValue;
    std::optional<Vec3f> rgbValue;
    std::optional<bool> boolValue;
    std::optional<std::string> stringValue;
    std::optional<std::string> uri;
};
```

#### parameter_binding.cppm — ParameterBinding
```cpp
class ParameterBinding : public GameObject {
    StringID bindingName;
    // 可变类型：ParameterBuffer 或 TextureBuffer
    std::variant<std::monostate, StrongRef<ParameterBuffer>, StrongRef<TextureBuffer>> value;
};
```

#### material_instance.cppm — MaterialInstance
```cpp
class MaterialInstance : public GameObject {
    std::string bsdfType;
    ResourceUri sourceUri;
    StringID sourceSignature;
    std::unordered_map<StringID, MaterialParameterEnvelope> envelopes;
    std::vector<StrongRef<ParameterBinding>> bindings;
    std::unordered_set<StringID> enabledPasses;
};
```

#### material.cppm (umbrella)
```cpp
export import :ParameterEnvelope;
export import :ParameterBinding;
export import :MaterialInstance;
```

### assets/render_feature/

#### feature_parameter.cppm — RenderFeatureParameter
```cpp
struct RenderFeatureParameter {
    std::string kind, value, valueType, binding, member;
    ResourceUri uri;
    bool required;
    std::vector<std::string> allowedValues;
    std::string requiredWhenParameter, requiredWhenEquals;
};
```

#### render_feature_def.cppm — RenderFeature
```cpp
enum class RenderFeatureLevel { Unknown, Shader, Pass };

struct RenderFeatureShaderContract {
    // shader ABI 描述（从旧代码提取核心字段）
};

class RenderFeature : public GameObject {
    std::string name, feature;
    RenderFeatureLevel level;
    std::optional<RenderFeatureShaderContract> shader;
    std::unordered_map<std::string, RenderFeatureParameter> parameters;
};
```

#### render_feature.cppm (umbrella)
```cpp
export import :FeatureParameter;
export import :RenderFeature;
```

### assets/camera/

#### types.cppm — CameraType
```cpp
enum class CameraType { Perspective, Orthographic };
```

#### pose.cppm — CameraPose + makeCameraPose()
```cpp
struct CameraPose {
    Vec3f eye, forward, up;
};
CameraPose makeCameraPose(Vec3f eye, Vec3f forward, Vec3f up);  // 保留旧代码的正交化逻辑
```

#### projection.cppm — CameraProjection + makeCameraProjectionMatrix()
```cpp
struct CameraProjection {
    CameraType type;
    f32 fovYDegrees, aspect, nearPlane, farPlane;
    f32 left, right, bottom, top;
};
Mat4f makeCameraProjectionMatrix(const CameraProjection&, GraphicsAPI api);
```

#### snapshot.cppm — CameraSnapshot
```cpp
struct CameraSnapshot {
    std::string path;
    CameraPose pose;
    CameraProjection projection;
    u32 cullingMask;
    bool active;
};
```

#### ray_frame.cppm — CameraRayFrame + makeCameraRayFrame()
```cpp
struct CameraRayFrame {
    Vec3f eye, right, up, forward;
};
CameraRayFrame makeCameraRayFrame(const CameraPose&, const CameraProjection&);
```

#### ray.cppm — makeCameraRay()
```cpp
Ray makeCameraRay(const CameraPose&, const CameraProjection&, const Vec2f& screenPixel, const Vec2f& viewportSize);
```

#### camera.cppm (umbrella)
```cpp
export import :Types;
export import :Pose;
export import :Projection;
export import :Snapshot;
export import :RayFrame;
export import :Ray;
```

### assets/light/

#### types.cppm — LightKind
```cpp
enum class LightKind { Directional, Point, Spot };
```

#### directional_light.cppm — DirectionalLight
```cpp
class DirectionalLight : public GameObject {
    Vec3f direction;
    Vec3f color;
    f32 intensity;
    // Shadow params
    Mat4f shadowViewProj;
    Mat4f cascadeViewProj[4];
    Vec4f cascadeSplits, cascadeDepthRanges;
    Vec4f shadowParams;  // (mapSize, bias, strength, cascadeCount)
    f32 shadowDistance;

    // 保留旧代码核心逻辑：
    void updateShadowViewProjection();
};
```

#### point_light.cppm — PointLight
```cpp
class PointLight : public GameObject {
    Vec3f color;
    f32 intensity, range;
    BoundingBox getDebugLocalBounds() const;
};
```

#### spot_light.cppm — SpotLight
```cpp
class SpotLight : public GameObject {
    Vec3f direction, color;
    f32 intensity, range;
    f32 innerConeDegrees, outerConeDegrees;
    BoundingBox getDebugLocalBounds() const;
};
```

#### light.cppm (umbrella)
```cpp
export import :Types;
export import :DirectionalLight;
export import :PointLight;
export import :SpotLight;
```

### asset_manager.cppm — AssetManager

```cpp
class AssetManager {
public:
    // 对象工厂 — 隔离 MemoryAllocator 与业务层
    template<typename T, typename... Args>
    StrongRef<T> create(Args&&... args);

    // Handle 解析
    template<typename T>
    StrongRef<T> resolve(Handle<T> handle) const;

    template<typename T>
    void release(Handle<T> handle);

    // 类型转换
    template<typename To, typename From>
    StrongRef<To> static_ref_cast(const StrongRef<From>& from);

    template<typename To, typename From>
    StrongRef<To> dynamic_ref_cast(const StrongRef<From>& from);

    template<typename T>
    bool is_a(const StrongRef<GameObject>& obj) const;

    std::string type_name(const StrongRef<GameObject>& obj) const;

    // 生命周期
    void tick();  // 驱动 GC sweep

private:
    MemoryAllocator m_allocator;
    GC m_gc;
    struct Entry { void* ptr; u32 generation; u32 refCount; TypeId typeId; };
    std::vector<Entry> m_registry;
};
```

### dynamic_ref_cast 实现

```cpp
// GameObject 基类
class GameObject {
protected:
    std::atomic<u32> refCount = 0;
    TypeId typeId;
public:
    virtual ~GameObject() = default;
    TypeId getTypeId() const { return typeId; }
};

// 每个具体类声明静态 TypeId
class VertexBuffer : public GameObject {
public:
    static TypeId classTypeId;
};

// dynamic_ref_cast
template<typename To, typename From>
StrongRef<To> dynamic_ref_cast(const StrongRef<From>& from) {
    if (!from) return {};
    if (from->getTypeId() != To::classTypeId) return {};
    return StrongRef<To>(static_cast<To*>(from.get()));
}
```

### assets.cppm (顶层 umbrella)
```cpp
export import :Handle;
export import :Types;
export import :AssetManager;
export import Assets.Mesh;
export import Assets.RenderObject;
export import Assets.Material;
export import Assets.RenderFeature;
export import Assets.Camera;
export import Assets.Light;
```

---

## Layer 4: new_core/scene

### types.cppm — VisibilityLayerMask
```cpp
using VisibilityLayerMask = u32;
inline constexpr VisibilityLayerMask Layer_All = 0xffffffffu;
inline constexpr VisibilityLayerMask Layer_Default = 1u << 0;
inline constexpr VisibilityLayerMask Layer_EditorOverlay = 1u << 31;
inline constexpr VisibilityLayerMask VisibilityMask_All = Layer_All;
inline constexpr VisibilityLayerMask VisibilityMask_Default = Layer_Default;
```

### scene_node.cppm — SceneNode
```cpp
class SceneNode : public GameObject {
    std::string nodeName;
    std::string name;
    Transform localTransform;
    u32 visibilityMask = Layer_All;

    // 层级
    WeakRef<SceneNode> parent;
    std::vector<WeakRef<SceneNode>> children;

    // 组件引用（scene.yaml 中节点可能挂载的资产）
    std::optional<StrongRef<CameraSnapshot>> camera;
    std::variant<std::monostate, StrongRef<DirectionalLight>, StrongRef<PointLight>, StrongRef<SpotLight>> light;
    std::optional<StrongRef<RenderObject>> renderObject;

    // worldTransform 计算（含 dirty 标记）
    const Mat4f& getWorldTransform() const;
    std::string getPath() const;
    BoundingBox getWorldBounds() const;
};
```

### environment.cppm — Environment
```cpp
struct Environment {
    bool enabled;
    f32 intensity;
    Vec3f ambientColor;
    f32 ambientIntensity;
    bool skyboxEnabled;
};
```

### render_settings.cppm — RenderSettings
```cpp
struct RenderSettings {
    bool shadows;
};
```

### scene_info.cppm — SceneInfo
```cpp
struct SceneInfo {
    std::string name;
    std::string gameplayCameraPath;
    std::string defaultOutputProfile;
    Environment environment;
    RenderSettings rendering;
};
```

### editor_camera.cppm — EditorCamera
```cpp
struct EditorCamera {
    Vec3f position;
    Vec3f rotationEulerDeg;  // (pitch, yaw, roll)
    f32 fovY, nearPlane, farPlane;
};
```

### scene.cppm (umbrella)
```cpp
export import :Types;
export import :SceneNode;
export import :Environment;
export import :RenderSettings;
export import :SceneInfo;
export import :EditorCamera;
```

---

## new_core/core.cppm 更新

```cpp
export module LX_New_Core;

export import LX_New_Common.Platform;
export import LX_New_Common.Math;
export import LX_New_Common.Memory;

export import LX_New_Core.Resource;
export import LX_New_Core.Assets;
export import LX_New_Core.Scene;
```

---

## 迁移来源对照表

| 新文件 | 旧代码来源 | 保留的功能逻辑 | 剥离的内容 |
|---|---|---|---|
| resource/vertex_buffer.cppm | core/rhi/vertex_buffer.hpp | VertexLayout, 预定义 vertex types | IGpuResource 接口, GPU 上传逻辑 |
| resource/index_buffer.cppm | core/rhi/index_buffer.hpp | indices, PrimitiveTopology | IGpuResource 接口 |
| resource/texture_buffer.cppm | core/asset/texture.hpp | TextureDesc fields (w/h/format/dim/mip/array) | CombinedTextureSampler, IGpuResource |
| resource/parameter_buffer.cppm | core/asset/parameter_buffer.hpp | data ptr, size, bindingName | IGpuResource, writeBindingMember |
| assets/mesh/geometry_storage.cppm | core/asset/mesh.hpp | VertexBuffer + IndexBuffer 组合 | GeometryStorageHandle |
| assets/mesh/mesh.cppm | core/asset/mesh.hpp | storage ref + offsets + bounds | MeshHandle |
| assets/render_object/render_object.cppm | infra/scene_io (mesh.uri + material.uri) | mesh + material 组合 | |
| assets/material/parameter_envelope.cppm | core/asset/material_parameter_envelope.hpp | 枚举 + typed value fields | |
| assets/material/parameter_binding.cppm | core/asset/parameter_buffer.hpp + material_instance.hpp | binding name + value | writeBindingMember |
| assets/material/material_instance.cppm | core/asset/material_instance.hpp | bsdfType, envelopes, bindings, enabledPasses | MaterialTemplate, 重验证逻辑 |
| assets/render_feature/* | core/asset/render_effect.hpp | name, level, parameters, shader contract | RenderPathGraph, validation |
| assets/camera/pose.cppm | core/scene/camera.hpp/.cpp | makeCameraPose 正交化逻辑 | CameraData UBO |
| assets/camera/projection.cppm | core/scene/camera.hpp/.cpp | makeCameraProjectionMatrix | CameraData UBO |
| assets/camera/ray_frame.cppm | core/scene/camera.cpp | makeCameraRayFrame | |
| assets/camera/ray.cppm | core/scene/camera.cpp | makeCameraRay | |
| assets/light/directional_light.cppm | core/scene/light.hpp/.cpp | shadow params, updateShadowViewProjection | LightBase 继承, scene 反向引用, 事件系统, pass system |
| assets/light/point_light.cppm | core/scene/light.hpp/.cpp | color/intensity/range, debug bounds | LightBase 继承, pass system |
| assets/light/spot_light.cppm | core/scene/light.hpp/.cpp | direction/cone/angle, debug bounds | LightBase 继承, pass system |
| scene/scene_node.cppm | core/scene/object.hpp/.cpp | parent-children, worldTransform dirty, getPath, getWorldBounds | IRenderable 接口, component system, validated passes |
| scene/environment.cppm | infra/scene_io (environment block) | 数据字段 | |
| scene/render_settings.cppm | core/scene/scene_render_settings.hpp | shadows 字段 | 完整渲染设置 |
| scene/scene_info.cppm | infra/scene_io (scene: block) | name/gameplayCameraPath + environment + rendering | outputProfiles |
| scene/editor_camera.cppm | infra/scene_io (editor: block) | position/rotationEulerDeg/fovY | |

---

## 内存管理流程

```
1. YAML 解析开始
2. AssetManager::create<VertexBuffer>(...) → StrongRef<VertexBuffer>
     └→ MemoryAllocator.allocate() + placement new
     └→ GC.registerObject()
     └→ refCount = 1
3. AssetManager::create<Mesh>(geometryStorageRef, ...) → StrongRef<Mesh>
4. AssetManager::register(asset) → Handle<Mesh> 返回给调用方
5. SceneNode 通过 StrongRef<Mesh> 引用该 Mesh（refCount++）
6. 调用方 release Handle（内部持有计数--）
7. 场景卸载：SceneNode 析构 → StrongRef 析构 → release() → refCount--
8. GC.tick() 扫描：refCount == 0 → 析构 → MemoryAllocator.free()
```

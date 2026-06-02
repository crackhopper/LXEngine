# REQ-067-b: Offline Renderer 迁移到共享资源模型

> 2026-06-02 新增：在 `REQ-067-a` 完成 `SceneResourceTable`、`GeometryStorage`、`MeshBuffer` 和 snapshot 资源模型后，清理 offline renderer 私有的 triangle/material 基础结构，让离线 compute/path tracing 输入直接来自共享场景资源。

## 背景

当前 Vulkan offline renderer 为了快速跑通 compute MVP，在 `src/backend/vulkan/offline/` 里自建了一套 shader 输入结构：

| 当前结构 | 位置 | 问题 |
|---|---|---|
| `GpuTriangle` | `gpu_scene_builder.hpp` | 复制三个世界空间顶点，只保存 face normal，丢失 vertex/index 共享关系 |
| `GpuMaterial` | `gpu_scene_builder.hpp` | 从 `OfflineMaterialIR` 重新打包材质参数，和 `MaterialInstance` / material reflection 分叉 |
| `GpuCameraParams` | `gpu_scene_builder.hpp` | 相机/light 参数和 scene resource snapshot 分叉 |
| `GpuBvhNode` / `ComputeBvhBuilder` | `compute_bvh_builder.*` | BVH 概念应保留，但当前输入依赖 `GpuTriangle`，leaf 直接指向展平后的 triangle buffer |
| `offline_primary_ray.comp` 的 `Triangle` buffer | `assets/shaders/glsl/offline_primary_ray.comp` | shader 只能读取 flat triangle，无法按 barycentric 插值 normal / uv / tangent |

这些结构适合 MVP 验证，但不适合作为后续 offline renderer 的基础。`REQ-067-a` 会把底层资源统一到 `SceneResourceTable`，并引入 `GeometryStorage + MeshBuffer`。本 REQ 专门负责让 offline renderer 消费这套共享结构，删除 backend 私有的重复 mesh/material/scene packing 模型，并把 BVH 输入迁移到新的共享底层结构。

## 目标

1. offline renderer 不再拥有私有 mesh/material 基础结构。
2. offline renderer 直接消费 `SceneResourceTable` 导出的 snapshot。
3. compute shader 输入保留 vertex/index/material/object 的索引关系，而不是展平成 `GpuTriangle`。
4. BVH 构建基于 primitive record，leaf 指向 primitive/index/mesh/object，而不是复制后的 triangle。
5. ray hit 后按 barycentric 从 shared vertex/index buffer 恢复 position、normal、uv、tangent。
6. 把 backend Vulkan offline 目录收敛到 Vulkan 执行、descriptor、buffer upload 和 dispatch，不再承担 scene packing 或 backend-agnostic BVH 构建逻辑。

## 需求

### R1: Offline renderer 输入改为 RenderSceneSnapshot

`VulkanOfflineRenderer::render()` SHALL 接收或内部获取 `SceneResourceTable` 导出的 offline-compatible snapshot，而不是直接依赖 backend 私有 `GpuSceneBuilder`。

snapshot SHALL 至少能提供：

| 数据 | 来源 |
|---|---|
| geometry storage records | `GeometryStorage` |
| mesh records | `MeshBuffer` |
| material records | `MaterialInstance` 导出的 shader record |
| object records | `ObjectResource` / `ObjectInstanceView` |
| camera params | scene resource table camera entry |
| light params | scene resource table light entries |

若 `OfflineSceneIR` 仍作为 CLI / scene compiler 的中间输入存在，它 SHALL 先被转换或注册进 `SceneResourceTable`，再由 snapshot 进入 offline renderer。offline renderer 不 SHALL 从 `OfflineSceneIR` 直接重新拼私有 GPU scene。

### R2: 删除 backend 私有 GpuSceneBuilder 基础模型

以下类型 SHALL 被删除或迁移为共享模型的一部分：

| 当前类型 | 目标 |
|---|---|
| `GpuTriangle` | 删除，替换为 primitive/index/vertex 关系 |
| `GpuMaterial` | 删除，替换为 `MaterialInstance` 导出的 material record |
| `GpuCameraParams` | 迁移为 shared offline scene params record |
| `GpuSceneData` | 删除，替换为 offline snapshot / ray scene buffer view |
| `GpuSceneBuilder` | 删除，替换为 core/offline 的 snapshot-to-ray-input builder |

新的 builder SHALL 不放在 `src/backend/vulkan/offline/`。若需要 CPU 侧打包 shader ABI，它应位于 `src/core/offline/` 或其他 backend-agnostic core 目录，并以 shared resource snapshot 为输入。

### R3: Offline ray input buffer contract

offline shader 输入 SHALL 从 flat triangle buffer 迁移为多 buffer contract。建议结构：

| Buffer | 内容 |
|---|---|
| vertex buffer | position、normal、uv、tangent 等 vertex attribute record |
| index buffer | `u32` triangle index |
| primitive buffer | index offset、mesh index、material index、object index |
| object buffer | objectToWorld、worldToObject、bounds、visibility flags |
| material buffer | baseColor、metallic/roughness、texture indices、flags |
| BVH node buffer | primitive range / child links / bounds |
| scene params buffer | camera、light、image size、sample count、seed |
| output buffer | RGBA float output |

buffer record SHALL 使用明确的 std430 layout contract，并通过 tests 验证 C++/GLSL size、offset 和 descriptor binding 一致。

### R4: Primitive record 保留 mesh/index 关系

primitive record SHALL 表达一组三角形索引关系，而不是复制三份顶点。

最低要求：

```cpp
struct OfflinePrimitiveRecord {
  u32 indexOffset;
  u32 meshIndex;
  u32 materialIndex;
  u32 objectIndex;
};
```

如需支持 mesh 内 vertex/index offset，record 或 mesh buffer record SHALL 能解析：

```text
GeometryStorage + MeshBuffer + PrimitiveRecord -> i0/i1/i2 -> vertex records
```

### R5: BVH builder 迁移为 primitive BVH

`ComputeBvhBuilder` SHALL 不再依赖 `GpuTriangle`。

BVH 构建 SHALL 使用 primitive bounds / centroid：

- 从 primitive record 解析 index buffer。
- 从 vertex buffer 读取三个 position。
- 应用 object transform 后计算 world-space bounds。
- leaf 存 primitive range 或 primitive index。

BVH node layout MAY 保留当前 32-byte `vec4 + vec4` encoding，只要 leaf 指向 primitive，而不是 flat triangle。若保留 layout，命名 SHALL 去掉 Vulkan/backend 私有前缀，例如 `OfflineBvhNode`。

### R6: Shader hit 使用 barycentric 插值

`offline_primary_ray.comp` SHALL 从 indexed vertex 数据恢复 hit attributes。

要求：

- intersection 返回 primitive index 和 barycentric 坐标。
- shading 通过 primitive -> mesh -> geometry -> vertex/index 查回三个 vertex。
- normal 使用 per-vertex normal barycentric 插值，并按 object transform 正确变换。
- uv 使用 barycentric 插值，为后续 texture material 做准备。
- tangent 如果当前 shader 不使用，也 SHALL 在 buffer contract 中预留或明确不支持。

flat face normal MAY 作为 fallback，但不应是唯一输入。

### R7: Material 输入来自 MaterialInstance

offline material record SHALL 来自 `MaterialInstance` 或其 snapshot record。

要求：

- baseColor / metallic / roughness / emissive 等参数从 reflected material parameter buffer 或 material shader record 取得。
- texture index 从 shared texture table 取得。
- 不从 backend 私有 `OfflineMaterialIR -> GpuMaterial` 路径重新打包长期材质数据。
- material record 与 realtime bindless material record 能尽量共享字段定义；确有 offline-only 字段时，用明确后缀或额外 record 扩展。

### R8: Vulkan descriptor layout 改为反射或显式校验

offline compute pipeline SHALL 校验 shader descriptor contract。

可接受方式：

- 使用 `ShaderReflector` 反射 `offline_primary_ray.comp.spv`，再创建 descriptor layout。
- 或保留显式 descriptor layout，但启动时把显式定义与反射结果比对。

校验 SHALL 覆盖：

- descriptor set count。
- binding index。
- descriptor type。
- stage flags。
- buffer record size / shader reflected block size（可反射时）。
- binding 名称或约定名称。

### R9: 删除 backend/offline 私有基础结构

迁移完成后，`src/backend/vulkan/offline/` SHALL 不再定义以下语义：

- mesh / material / camera 的 core-level 数据模型。
- scene-to-GPU packing builder。
- triangle flatten 几何结构。
- backend-agnostic BVH builder。

Vulkan offline 目录 SHALL 只保留：

- shader module / pipeline / descriptor setup。
- VkBuffer upload。
- dispatch 和 barrier。
- readback。
- Vulkan-specific validation / error handling。

BVH 概念 SHALL 保留，并迁移到 backend-agnostic core/offline 层。新的 BVH builder SHALL 直接基于 `GeometryStorage + MeshBuffer + PrimitiveRecord + ObjectInstanceView` 计算 bounds、centroid 和 leaf primitive reference，不再通过 backend 私有 triangle buffer 间接工作。

### R10: 兼容 CLI 与 scene compiler

`lxe_offline_render` CLI 行为 SHALL 保持兼容。

要求：

- 现有 scene YAML 仍可渲染。
- `OfflineSceneCompiler` 可以继续把 scene YAML 编译为中间数据，但最终必须注册到 `SceneResourceTable` 或生成等价 snapshot。
- offline 输出格式、EXR/PNG 写出和 metadata 不因资源模型迁移破坏。

## 测试

### T1: Offline ray input contract

新增测试验证：

- snapshot 能生成 vertex/index/primitive/object/material/params buffers。
- C++ record size 与 shader std430 layout 一致。
- descriptor binding 与 shader reflection 一致。

### T2: Indexed normal 插值

构造一个共享顶点 mesh，三个 vertex normal 不同。

验收：

- offline hit 使用 barycentric 插值 normal。
- 不再只返回 face normal。
- 相同 mesh 被多个 object instance 复用时，vertex/index 数据不重复。

### T3: Primitive BVH

新增测试验证 BVH builder：

- 输入 primitive record，而不是 `GpuTriangle`。
- leaf 指向 primitive range / primitive index。
- world bounds 包含 object transform。
- triangle reorder 不破坏 primitive -> vertex/index/material/object 关系。

### T4: Material record 复用

新增测试验证：

- offline material record 从 `MaterialInstance` 参数生成。
- metallic / roughness / baseColor 与 realtime material 参数一致。
- texture handle / index 来自 shared texture table。

### T5: Vulkan offline renderer regression

更新 `test_vulkan_offline_renderer` / offline CLI 相关测试：

- 现有 MVP scene 仍能渲染。
- 输出图非空。
- validation layer 无 descriptor mismatch。
- 删除 `GpuSceneBuilder` 后测试不再 include backend 私有 scene packing header。

### T6: No duplicate model guard

新增静态或集成测试 / grep guard：

- `src/backend/vulkan/offline/` 不再定义 `GpuTriangle`、`GpuMaterial`、`GpuSceneData`。
- backend offline header 不再被 core/offline BVH builder include。
- offline shader 不再声明 `Triangles { Triangle triangles[]; }` 作为唯一几何输入。

## 修改范围

- `src/core/offline/`
- `src/core/scene/`
- `src/core/asset/mesh.*`
- `src/core/asset/material_instance.*`
- `src/backend/vulkan/offline/`
- `src/tools/lxe_offline_render/`
- `src/infra/offline/`
- `assets/shaders/glsl/offline_primary_ray.comp`
- `src/test/integration/test_offline_gpu_scene.cpp`
- `src/test/integration/test_vulkan_offline_renderer.cpp`
- `src/test/integration/test_offline_render_cli.cpp`
- `openspec/specs/` offline / compute / scene resource model specs
- `notes/source_analysis/src/backend/vulkan/offline/vulkan_offline_renderer.md`

## 边界与约束

- 本 REQ 依赖 `REQ-067-a` 的 resource table、handle-only owner、`GeometryStorage`、`MeshBuffer` 和 snapshot 基础。
- 本 REQ 不要求实现完整 realtime bindless renderer。
- 本 REQ 不要求实现 hardware ray tracing。
- 本 REQ 不要求支持所有 glTF material texture；只要求 resource path 不再阻断后续 texture material。
- 本 REQ 不要求把 frame graph render target 资源放入 `SceneResourceTable`。
- 本 REQ 不允许为了 offline renderer 再新建一套 mesh/material/texture 长期模型。

## 依赖

- `REQ-067-a: SceneResourceTable 与 Bindless-Ready 资源模型`
- `REQ-054-b: Vulkan Compute Offline Renderer MVP`
- `REQ-056-a: Offline PBR 纹理材质支持`
- `REQ-057-a: Offline Path Tracing PBR Reference`
- `REQ-063-a: Compute Pipeline Foundation`
- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/material-system/spec.md`

## 后续工作

- Offline texture sampling：material texture indices 接入 sampled image / bindless texture table。
- Offline path tracing integrator：在 indexed geometry 和 material table 基础上实现多 bounce / MIS / environment sampling。
- Realtime bindless renderer：复用同一 `GeometryStorage`、`MeshBuffer`、material/object/texture table。
- Hardware ray tracing backend：把 primitive/object/material table 复用到 BLAS/TLAS 路径。

## 实施状态

Draft，未实施。

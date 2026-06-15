# REQ-071-c: SceneResourceTable Parser 拆分与 Resource Ownership

> 2026-06-14 归档：SceneResourceTable parser/resource ownership 基础已经落地；剩余 material source contract、package canonical state 和非 mesh resource 扩展分别由 `REQ-073-*`、`REQ-074-*`、`REQ-076-i` 承接。

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第三步。目标是在 `REQ-071-a` 已经前置的最小 `SceneResourceTable` URI 解析、加载、去重和 dependency 注册接口之上，把 mesh、texture、camera、light、SurfaceMaterial、RenderFeature、RenderPathGraph 等具体解析逻辑也接入同一资源抽象，并让 `SceneResourceTable` 真正成为资源 owner。

## 背景

我们已经确定：

- `SceneResourceTable` 应该是场景资源的统一 owner。
- `REQ-071-a` 已经为 `MaterialResourceParser` 前置最小 resource table 能力：URI resolve、load-or-get、typed handle、dependency registration 和 canonical URI 去重。
- `MaterialResourceParser` 负责材质解析，不拥有资源。
- mesh、texture、灯光、相机、RenderFeature、RenderPathGraph 也应有对应 parser，避免 SceneResourceTable 的 cpp 文件继续膨胀。
- 所有资源类型应通过一套抽象注册、查找、引用、去重和打包，而不是每种资源写一套临时逻辑。
- scene/node 上的材质修改是 pure envelope instance override，不修改原始 `.material` 文件，也不修改 `RenderPathGraph`。

本 REQ 处理 CPU scene resource 管理层；GPU resource table、pipeline cache 和异步上传在 `REQ-071-d`。

## 目标

1. 将 `REQ-071-a` 的最小 resource table 能力扩展为全场景通用 resource abstraction。
2. `SceneResourceTable` 管理所有资源类型的生命周期、去重、引用关系、dirty/version 和 handle。
3. 拆分并接入 parser：SurfaceMaterialResourceParser、MeshResourceParser、TextureResourceParser、CameraResourceParser、LightResourceParser、RenderFeatureResourceParser、RenderPathGraphResourceParser、SpectrumResourceParser、BsdfTableResourceParser。
4. parser 只负责具体格式解析和调用 resource table 基础能力，不拥有资源生命周期。
5. scene override 只创建新的 `MaterialInstance` identity，应用 pure envelope patch；不修改原始 SurfaceMaterial、SurfaceMaterialTemplate 或 RenderPathGraph。
6. 为 scene package 序列化准备统一 resource graph。
7. 将几何数据整理为 bindless/indirect 友好的 position stream、attribute stream、index stream 和 material/object indirection。
8. CPU resource table 的导出形态 SHALL 与后续 GPU bindless 分表模型兼容：同类资源在 CPU 侧也能形成稳定的 typed arrays / handles，GPU 上传阶段只做 URI/handle 到 bindless slot 的映射，不重新发明一套资源身份。

## 命名与资源边界

| 名称 | SceneResourceTable 中的资源语义 |
|---|---|
| `SurfaceMaterial` | `.material` v2 pure envelope 资产，包含 BSDF 参数、typed resource dependencies 和 base MaterialInstance |
| `MaterialInstance` | SurfaceMaterial + override hash 形成的运行时 identity，引用 texture/spectrum/bsdfTable/materialRef typed handles |
| `RenderFeature` | `.render-feature.yaml` pure envelope 资产，包含算法参数和 feature-owned resource dependencies |
| `RenderPathGraph` | `.render-path.yaml` / `.render-path-graph.yaml` 资产，包含 RenderPath、RenderPassNode、shader URI、graph resources、feature/material dependencies |
| `RenderClass` | object/material 分类标签，用于 RenderPathGraph pass filter，不替代资源身份 |

parser 只能产出 typed payload 和 dependency edges；资源 lifetime、state、version、dirty propagation、URI 去重和 handle -> typed index 映射都属于 `SceneResourceTable`。

## 需求

### R1: 通用 Resource Identity

所有 scene resource SHALL 使用规范化 URI 作为身份。

规则：

- 相同 canonical URI 的资源只加载一次。
- URI resolver 负责把 scene-relative、asset-root-relative、package-internal URI 解析到 canonical identity。
- `SceneResourceTable` 提供 `findOrLoad(uri, type)` / `registerResource(uri, type, payload)` 等基础能力。
- handle 包含 resource type、index、generation。

资源 identity 不应依赖显示名、node 名或文件 basename。

### R2: Resource Type 抽象

新增通用 resource 元数据：

| 字段 | 说明 |
|---|---|
| `ResourceType` | mesh、geometry、surfaceMaterial、materialInstance、texture、spectrum、bsdfTable、camera、light、renderFeature、renderPathGraph、shader、sceneObject 等 |
| `uri` | canonical identity |
| `state` | unloaded、loading、ready、failed |
| `version` | 内容版本 / dirty generation |
| `dependencies` | 依赖的其他 resource handles |
| `diagnostics` | parser warning/error |

具体资源 payload 存入对应 typed storage buffer；通用层只处理 identity、state、引用关系和生命周期。

状态规则：

- parser 成功写入 typed payload 后，resource state SHALL 从 `loading` 转为 `ready`，并递增或初始化 `version`。
- parser fatal 后，resource state SHALL 变为 `failed`，diagnostics 必须保留 URI、resource type、parser 名和字段路径。
- dependency resource 的 `version` 改变时，`SceneResourceTable` SHALL 标记所有下游 owner dirty，并递增 dirty generation。
- dirty propagation 至少覆盖 `MaterialInstance -> Texture/Spectrum/BsdfTable/SurfaceMaterial`、`RenderPathGraph -> RenderFeature/Shader`、`SceneObject -> MeshBuffer/MaterialInstance`、`Camera -> RenderPathGraph/RenderFeature`。
- upload view SHALL 只从 `ready` 资源生成 typed index；引用 failed/unloaded 资源时 fail-fast 或输出明确 unsupported diagnostic。

### R3: Parser 接口

新增 parser 接口，示例：

```cpp
class ISceneResourceParser {
public:
  virtual ResourceType type() const = 0;
  virtual ResourceHandle parse(SceneResourceTable& table,
                               const ResourceUri& uri,
                               const ParseContext& context) = 0;
};
```

具体 parser：

| Parser | 职责 |
|---|---|
| `SurfaceMaterialResourceParser` | `.material` v2 pure envelope、BSDF 参数、render class / tag、依赖资源注册、base MaterialInstance payload |
| `MeshResourceParser` | OBJ/glTF/转换后 mesh，实际填充 GeometryStorage/MeshBuffer typed storage 和 geometry attribute dependencies |
| `TextureResourceParser` | image/HDR/cubemap metadata 与 texture typed storage，不只 intern metadata |
| `CameraResourceParser` | scene camera 节点、active RenderPath / RenderPathGraph / RenderFeature 引用 |
| `LightResourceParser` | light 节点和 light data |
| `RenderFeatureResourceParser` | `.render-feature.yaml` pure envelope 参数和 feature-owned resource dependencies |
| `RenderPathGraphResourceParser` | `.render-path.yaml` / `.render-path-graph.yaml`，pass DAG、shader URI、graph resources、feature/material dependencies |
| `SpectrumResourceParser` | SPD/eta/k 等 spectrum resource |
| `BsdfTableResourceParser` | `.bsdf` table resource |

parser 可以互相通过 `SceneResourceTable` 加载依赖，但不直接持有另一个 parser 的结果对象。

MeshResourceParser、TextureResourceParser SHALL 实际填充 typed storage / dependencies，不能继续只保存 metadata 或把 loader 私有对象塞进 table 外部结构。

### R4: Material Override 只影响 Instance

scene/node 的 material override SHALL 表达为 `MaterialInstance` override。override 是 pure envelope patch，不是对 base material/template 的 in-place mutation。

规则：

- 原始 `.material` 文件解析得到 immutable-ish SurfaceMaterialTemplate + base MaterialInstance。
- scene/node override 创建新的 `MaterialInstance` 对象实例，复制 base instance 参数后应用 override，绑定到 object/node。
- `MaterialInstance` resource identity SHALL 至少包含 source material URI 和 override identity/hash。无 override 的 base instance 与带 override 的 instance 是不同 resource entry。
- 同一 material URI + 同一 override hash MAY 复用同一个 `MaterialInstance` handle；不同 override hash MUST 是不同 instance。
- `SceneObject` / render object SHALL 引用具体 `MaterialInstance` handle，而不是引用 base material 再在渲染时叠加 override patch。
- override 允许修改 `bsdf.parameters.*` 和运行时 instance 状态。
- override 不允许修改 `SurfaceMaterialTemplate`、`SurfaceMaterial` base payload、`RenderPathGraph`、shader、pass、renderState。
- 保存 scene 时，override 写回 scene 文件对应 node，不修改原始 material YAML。
- override 后的 instance SHALL 重新计算 dependency edges 和 dirty/version，确保 texture/spectrum/bsdfTable/materialRef 的 handle -> typed index 映射稳定可导出。

### R5: 引用关系图

`SceneResourceTable` SHALL 记录资源依赖关系。

示例：

```text
SceneObject -> MeshBuffer
SceneObject -> MaterialInstance
MaterialInstance -> Texture
MaterialInstance -> Spectrum
MaterialInstance -> BsdfTable
MaterialInstance -> SurfaceMaterial
MaterialInstance -> MaterialInstance(materialRef target, header/full load as required)
Camera -> RenderPathGraph
Camera -> RenderFeature
RenderPathGraph -> RenderFeature
RenderPathGraph -> Shader
RenderPathGraph -> GraphResource
RenderFeature -> Texture/Spectrum/Buffer/LUT
```

要求：

- 能诊断 missing dependency。
- 能导出 package 所需资源列表。
- 能在 resource reload 时标记下游 dirty。
- 能避免同一 texture/SPD/BSDF 被重复加载。

### R6: SceneResourceTable 文件职责收敛

`SceneResourceTable` 的 cpp 文件 SHALL 只保留通用 table 逻辑。

不应继续堆叠：

- surface material YAML 解析细节。
- mesh 文件格式解析细节。
- camera/render feature/render path graph YAML 解析细节。
- PBRT converter 默认值逻辑。
- Vulkan/GPU 上传逻辑。

具体逻辑拆到 parser/manager 或 infra loader 中。

### R7: Fatal 优先于隐式兜底

资源解析 SHALL fail-fast。

要求：

- 缺显式 shader、pass target、required material 参数 fatal。
- 缺 converter 默认配置且 PBRT 源也没写 fatal。
- unsupported resource type fatal 或明确 unsupported diagnostic。
- 不做“按文件名猜类型”“按材质名猜透明”“按 pass 名猜 target”。

### R8: Package-ready CPU Resource Graph

`SceneResourceTable` SHALL 能导出 scene package 所需的 CPU resource graph 描述：

- resources 列表。
- dependencies。
- canonical URI 到 package internal URI 的映射。
- resource type / version / content hash。
- parser diagnostics。

实际二进制 package 格式在 `REQ-071-e` 实现；本 REQ 只要求导出足够的信息。

### R9: Geometry Stream 与 Vertex Input 收敛

几何资源 SHALL 为 bindless + indirect draw 做准备。

规则：

- 第一阶段采用混合方案：graphics pipeline 的传统 vertex input 收敛到 position-only，至少包含 `x/y/z`；index buffer 仍作为传统 index input 保留。
- normal、uv、tangent、color、skin weights 等属性 SHALL 作为 geometry attribute streams 记录在 `SceneResourceTable` 中，并能上传为 SSBO。
- attribute stream 是可选资源。mesh 可以没有 normal、uv、tangent、color 或 skin weights；loader 不应为了兼容而静默补默认 stream。
- shader 通过 `gl_VertexIndex` / draw data / mesh descriptor 访问 attribute stream 中的 normal、uv、tangent 等数据。
- 该混合方案用于保留 raster path 的稳定性，便于后续合批、Early-Cull / GPU culling 相关优化，以及兼容 indirect draw。完全 SSBO vertex pulling 不属于本 REQ。
- UV 不是 albedo 本身，而是几何属性中的纹理坐标；材质的 albedo/base color 可以通过 texture 使用该 UV。
- 几何 normal 是 geometry attribute；normal map 是 material texture/resource。两者都需要被 contract 表达，不能混为一个字段。
- 如果 active RenderPathGraph pass 的 shader 需要 normal/uv/tangent/color/skin weights，而 mesh 没有对应 attribute stream，graph validation SHALL fail-fast 或报告 unsupported，不能静默补零。shader 不需要的 attribute 可以缺失。

建议资源关系：

```text
GeometryStorage
  positions
  indices
  attributes:
    normal0
    uv0
    tangent0
    color0

MeshBuffer
  geometryStorage
  positionRange
  indexRange
  attributeSet
```

### R10: Object / Material Indirection

Scene object SHALL 保存 mesh handle、material instance handle 和 object transform。上传视图 SHALL 能导出：

- object array。
- mesh/geometry stream descriptor array。
- material instance indirection array。
- draw/instance data array。

这些数组供 `REQ-071-d` 的 bindless descriptor table 和 indirect draw 使用。

### R11: Bindless-friendly Typed Resource Arrays

`SceneResourceTable` SHALL 在 CPU 侧维护并导出按资源类型组织的稳定数组视图。

最低要求：

- texture、sampler、spectrum、bsdfTable、geometry stream、mesh descriptor、material instance、object、camera、light、effect 等资源都能通过 `ResourceHandle` 映射到对应 typed storage 的 index。
- canonical URI 仍是资源身份；typed array index 是当前 table snapshot 的内部索引，不替代 URI identity。
- 导出的 upload view SHALL 包含 `ResourceHandle -> typed index` 映射，供 `REQ-071-d` 生成 bindless slot、material record 和 object/draw record。
- 资源去重只发生在 SceneResourceTable 的 URI/handle 层；GPU 上传阶段不得通过文件名、路径字符串或材质名再次做 ad hoc 去重。
- material/object/draw record 只引用 handle 或 typed index，不直接持有 parser 私有对象指针。

这个模型用于保证 CPU `SceneResourceTable` 与 GPU `GPUResourceTable` 的 bindless table 一一衔接：CPU 侧负责资源身份、依赖和去重，GPU 侧负责把同一批 typed resource arrays 上传并分配按类型的 bindless slot。

上传视图还 SHALL 稳定输出：

- `ResourceHandle -> typed index` 映射表，覆盖 texture、sampler、spectrum、bsdfTable、geometry stream、mesh descriptor、surfaceMaterial、material instance、object、camera、light、renderFeature、renderPathGraph。
- `MaterialInstanceHandle -> SurfaceMaterialHandle` 与 `MaterialInstanceHandle -> dependency typed indices`。
- `RenderPathGraphHandle -> RenderPassNode indices`、`RenderPathGraphHandle -> RenderFeatureHandle`、`RenderPathGraphHandle -> ShaderHandle`。
- snapshot 内 typed index 稳定；resource reload 后通过 version/dirty 通知上传层重建受影响 record。

## 测试

### T1: URI 去重

两个 material 引用同一个 texture URI：

- parser 只加载一次 texture。
- 两个 material instance 保存同一 texture handle。

### T2: Parser Ownership

验证 parser 返回 handle，资源 owner 是 `SceneResourceTable`；parser 销毁后资源仍有效。

### T3: Material Override

scene node override 修改 `Kd`：

- 原始 material template 文件不变。
- base instance 不变。
- node 绑定 override instance。

### T4: Dependency Graph

加载含 SurfaceMaterial、texture、spectrum、RenderFeature、RenderPathGraph 的 scene，导出依赖图，验证 edge 完整。

### T5: Fatal Missing Contract

删除 required material 参数或 shader URI，验证加载失败且错误信息包含 URI、字段路径和 parser 名。

### T6: Geometry Attribute Streams

构造只有 position 的 mesh 和带 normal/uv/tangent 的 mesh：

- position-only vertex input 可建立。
- attribute stream metadata 正确进入 SceneResourceTable。
- shader 需要 UV 但 mesh 无 UV 时 validation 失败。
- normal map 材质需要 tangent/uv 时 validation 失败或输出明确 unsupported diagnostic。

### T7: Object / Material Indirection Export

多个 object 使用同一个 SurfaceMaterialTemplate 的不同 MaterialInstance：

- resource table 导出 object array。
- material instance array 包含多个 instance。
- object/draw record 能引用正确 material index。

### T7.1: Bindless Upload View

加载包含共享 texture、多个 material instance、多个 mesh/object 的 scene：

- `SceneResourceTable` 导出按类型分组的 upload view。
- 相同 texture URI 只有一个 texture resource handle 和 typed texture index。
- material instance record 通过 handle/typed index 引用 texture、spectrum、bsdfTable。
- object/draw record 通过 handle/typed index 引用 mesh、geometry 和 material instance。
- RenderPathGraph record 通过 handle/typed index 引用 RenderFeature、Shader 和 graph resources。
- 修改 texture 或 RenderFeature 后，下游 MaterialInstance / RenderPathGraph / Camera dirty generation 变化。

### T8: Helmet Rendering Smoke Gate

本 REQ 完成时 SHALL 继续运行 helmet editor/offline smoke：

- helmet scene 通过拆分后的 parser/resource table 加载。
- Material v2、mesh attribute streams、texture resources 和 camera/light resources 都由 resource table 管理。
- editor realtime 输出非全黑。
- offline direct 输出非全黑。
- 如果资源系统拆分期间需要 legacy bridge 保持渲染可用，必须记录 bridge 的资源类型、调用点和计划删除的后续 REQ。
- 在 `REQ-071-d` 之前，允许 transitional per-object draw path 逐 object 提交 draw，以保持 editor smoke 可用；该路径必须读取新的 SceneResourceTable object/material/geometry 数据，不能读取旧 material loader 或旧 PBR 参数。

## 修改范围

- `src/core/scene/` 或 `src/core/resource/`：通用 resource handle、metadata、dependency graph。
- `src/core/offline/`：SceneResourceTable 通用接口调整。
- `src/core/asset/`：GeometryStorage / MeshBuffer attribute stream metadata。
- `src/infra/material_loader/`：SurfaceMaterialResourceParser / MaterialResourceParser pure envelope 边界。
- `src/infra/mesh_loader/`：MeshResourceParser。
- `src/infra/texture_loader/`：TextureResourceParser。
- `src/infra/resource_parsers/`：CameraResourceParser、LightResourceParser、RenderFeatureResourceParser、RenderPathGraphResourceParser、SpectrumResourceParser、BsdfTableResourceParser。
- `src/infra/scene_io/`：camera/light/render feature/render path graph parser 接入。
- `src/test/`：resource table/parser/dependency/override 测试。

## 边界与约束

- 本 REQ 不实现 GPU resource table；GPU 上传在 `REQ-071-d`。
- 本 REQ 不实现 scene package 持久化；只导出 package-ready graph。
- 不为了兼容旧 material pass 格式引入隐式 fallback。
- parser 拆分不应改变现有 scene YAML 的非材质字段语义，除非字段已经被 `REQ-071-a/b` 明确替换。
- 本 REQ 不要求删除所有 legacy interleaved vertex buffer 路径；但新 bindless/indirect 路径的资源模型必须以 position stream + attribute streams 为目标。
- 本 REQ 不允许通过 legacy material root `resources` / `albedoMap` / per-object descriptor path 作为资源真相；上传视图必须从 typed handles 和 typed indices 生成。

## 依赖

- `REQ-067-a`：SceneResourceTable 和 handle/snapshot 基础。
- `REQ-071-a`：SurfaceMaterialResourceParser 的材质参数合同。
- `REQ-071-b`：RenderPathGraph、RenderPassNode、RenderFeature 文件合同。

## 后续工作

- `REQ-071-d`：把 CPU resource graph 上传到 GPUResourceTable。
- `REQ-071-e`：把 CPU resource graph 序列化为 scene package。
- `REQ-071-d` 必须清理本 REQ 为保持 helmet smoke 可用而引入的非 bindless transitional draw/resource bridge，不能拖到 `REQ-071-f`。

## 实施状态

2026-06-14 复核：保留 active，部分完成。

当前已经有 `SceneResourceTable`、Mesh/Texture/RenderFeature/RenderPathGraph parser、source-local material storage、default textures、typed upload view 和大量 `buildUploadView()` 测试；offline renderer 也已从该 upload view 构建 scene storage resources。

仍未完成：

- parser/manager/resource abstraction 还没有完全收敛到一个小而稳定的 resource owner 层；`SceneResourceTable` 仍承担较多 typed storage / upload view 构建职责。
- resource state/version/dirty propagation、package-ready CPU graph 导出、scene object/material indirection 和 component-only renderable ownership 尚未完全完成。
- GPU bindless 默认消费、package serialization 和 legacy hard cut 由 `REQ-073-*` / `REQ-074-*` 承接。

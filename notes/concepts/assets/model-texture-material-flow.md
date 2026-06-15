# 模型、纹理和材质文件如何接入

如果资产根是档案室地址，那么 loader 就像登记员。磁盘上的 `.obj`、`.gltf`、`.png`、`.material` 先要被读取、校验和转换，之后才交给场景系统或材质系统使用。

这页只讲资产系统视角：哪些文件被谁读取，文件字段怎样映射到 loader。材质内部的 pass、参数缓冲、pipeline identity 由 [材质系统](../material/index.md) 展开；可渲染对象如何使用 mesh/material 由 [场景系统：可渲染对象](../../scene-system/renderable-object.md) 展开。

## 模型文件先变成 Mesh

`lxe_editor` 当前主要通过 `SceneBuilder` 加载模型。场景文件给出 `mesh.uri` 后，反序列化结果会保存在 `SceneNodeDocument::meshUri`，后续运行时装配再把它交给 builder：

```yaml
mesh:
  uri: assets/models/damaged_helmet/DamagedHelmet.gltf # -> SceneBuilder 的模型加载路径
```

当前支持的模型入口是：

| 扩展名 | Loader | 当前处理方式 |
|---|---|---|
| `.obj` | `ObjMeshLoader` | 读取位置、法线、UV，组装成统一顶点缓冲 |
| `.gltf` | `GltfMeshLoader` | 读取 glTF mesh 数据；当前 scene 装配里的材质仍由 `material.uri` 决定 |
| `.glb` | `GltfMeshLoader` | 与 glTF 走同一类路径 |

builder 会把 loader 读出的数据转换成 `VertexPosNormalUvBone`。如果模型缺少某些属性，当前路径会补默认法线、UV 或 tangent，让 mesh 至少能进入现有 forward 渲染路径。

## 纹理文件变成 CombinedTextureSampler

纹理入口有两类：

| 来源 | 例子 | 使用位置 |
|---|---|---|
| material envelope 里的 texture URI | `baseColorTexture: { kind: texture, valueType: rgb, uri: ../textures/foo.png }` | `MaterialResourceParser` 注册材质依赖 |
| 内置模型 manifest 的 `albedoTextureUri` | `assets/models/builtin/.../Textures/texture-a.png` | `buildModelAssetNode()` |

当前 `TextureLoader` 使用 `stb_image` 读图，并强制转成 RGBA8。读入后会创建 `Texture`，再包成 `CombinedTextureSampler`，因为材质描述符层需要的是能作为 GPU resource 绑定的 sampler 组合对象。

## 材质文件在资产系统里只是引用边界

`.material` 文件像一张表面参数卡。它说明 `schema: lxe.material.v2`、`bsdf.type`、`bsdf.source`、参数 envelope 和资源 URI；pass、shader 和 render state 来自 RenderPathGraph。场景节点只保存它的路径：

```yaml
material:
  uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material # -> MaterialResourceParser
```

资产系统关心的是这条引用如何落地：

1. `SceneRuntime` 从 `SceneNodeDocument::materialUri` 取得路径。
2. `MaterialResourceParser` 解析 `.material`，再加载 `bsdf.source` 指向的 `.contract.glsl`。
3. parser 按 `MaterialContractReflection` 校验参数名、kind 和 required 参数，并把 texture/spectrum 等 URI 注册为 `SceneResourceTable` 依赖。
4. parser 创建 `MaterialInstance`，写入 BSDF type、source signature、envelope 和 resource dependency。
5. `SceneNode` 上的 `MaterialComponent` 持有这个 `MaterialInstance`；active RenderPathGraph 再决定哪些 pass 消费它。

材质文件里的资源写在参数 envelope 中：

```yaml
bsdf:
  parameters:
    baseColorTexture:
      kind: texture
      valueType: rgb
      uri: ../textures/brick.png
```

这里要注意一个边界：材质 resource dependency 只表达 material-owned surface 资源。像 `SceneLightsUBO`、camera、shadow map、post feature 这种系统或 graph 资源，不应该放进 `.material`；它们由 RenderPathGraph 的 `sources` 和 renderer/resource table 注入。系统内置绑定、pass 和 pipeline identity 的细节见 [材质系统](../material/index.md)。

## 文件读取到这里为止，运行时装配交给场景系统

模型和材质分别读入后，真正让它们参与渲染的是场景节点。这部分不属于资产文件合同，而属于场景系统：

```text
.scene.yaml
  -> SceneDocument / SceneNodeDocument
  -> SceneRuntime
  -> SceneNode + MeshComponent + MaterialComponent
  -> RenderWorkCompiler / draw input
```

当前有几种常见装配路径：

| `mesh.uri` 类型 | 运行时行为 |
|---|---|
| `assets/models/...` | 从磁盘模型文件加载 mesh |
| `assets/models/builtin/...` | 作为内置模型资产加载，并尝试使用 manifest 里的贴图 |
| `builtin://lxe_editor/primitives/cube` 等 | 创建 editor 内置 primitive mesh |
| `builtin://lxe_editor/helmet` / `ground_mesh` | editor 示例场景的特殊内置入口 |

这些 URI 最终都会落到同一个目标：让一个 `SceneNode` 拥有可渲染的 mesh 和 material。具体边界见 [场景系统：可渲染对象](../../scene-system/renderable-object.md)。

## 继续阅读

下一页 [场景也是资产](scene-assets.md) 会把 `.scene.yaml` 作为资产文件展开，解释节点、camera、light、材质覆盖怎样被序列化。

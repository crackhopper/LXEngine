# 资产系统：文件、URI 与序列化边界

资产系统像一间档案室。这里关心的不是一个节点在运行时怎样参与渲染，而是文件放在哪里、文件里写什么、URI 怎样指向别的文件、反序列化后交给哪个系统继续处理。

这一组文档讲当前代码里的资产文件入口，而不是一个还不存在的统一 `AssetManager`。我们把重点放在 `assets/` 目录、`.scene.yaml`、`.material`、`asset.yaml` manifest、模型/贴图文件，以及它们的序列化/反序列化边界。

## 我们先把资产看成三层文件合同

一个资产从磁盘进入 editor，大致会经过三层：

| 层 | 类比 | 当前代码里的对象或文件 | 资产系统回答的问题 |
|---|---|---|---|
| 定位 | 档案室地址 | `assets/`、`LX_RUNTIME_ROOT`、`resolveRuntimePath()` | 文件从哪里找 |
| 文件格式 | 表单 | `.scene.yaml`、`.material`、`asset.yaml`、`.obj`、`.gltf`、`.png` | 文件里允许写什么 |
| 反序列化入口 | 登记员 | `SceneDocument`、`loadGenericMaterial()`、`BuiltinAssetCatalog`、mesh/texture loader | 文件被读成什么中间对象 |

场景运行时对象的含义放在 [场景系统](../../scene-system/index.md) 里讲。资产系统只讲 `.scene.yaml` 作为文件怎样保存节点、组件、相机、光源和可渲染对象引用。

## 当前系统里有哪些资产

当前仓库里的常用资产可以这样理解：

| 资产类型 | 常见位置 | 反序列化入口 | 交给谁继续解释 |
|---|---|---|---|
| 模型 | `assets/models/**/*.obj`、`assets/models/**/*.gltf`、`assets/models/**/*.glb` | OBJ/glTF mesh loader | [场景系统：可渲染对象](../../scene-system/renderable-object.md) |
| 纹理 | `assets/textures/**`、模型包内贴图 | `TextureLoader` | 材质实例或内置模型装配 |
| 材质 | `assets/materials/*.material` | `loadGenericMaterial()` | [材质系统](../material/index.md) |
| Shader | `assets/shaders/glsl/*.vert/.frag` | shader compiler / reflector | 材质系统 |
| 内置模型 manifest | `assets/models/builtin/**/asset.yaml` | `BuiltinAssetCatalog` | editor palette 与场景插入流程 |
| 场景 | `assets/scenes/*.scene.yaml`、project 下的 `scenes/*.scene.yaml` | `SceneDocument` | [场景系统](../../scene-system/index.md) |
| project template | `assets/project_templates/**` | project 初始化流程 | project / scene 打开流程 |

## 阅读顺序

建议按下面顺序读。这个顺序从“文件在哪里”开始，然后进入“文件如何变成运行时对象”，最后解释场景和内置资产这两个更接近 editor 的入口。

1. [资产根与目录](runtime-root-and-layout.md)：我们怎样找到 `assets/`，以及当前目录约定是什么。
2. [模型、纹理和材质如何接入](model-texture-material-flow.md)：模型/纹理/material 文件如何被 loader 读取。
3. [场景也是资产](scene-assets.md)：`.scene.yaml` 如何序列化节点、组件和资源 URI。
4. [内置资产目录](builtin-catalog.md)：`asset.yaml` manifest 如何让 editor 发现可插入模型。

## 和其他系统的边界

资产系统只负责“文件在哪里、文件里写什么、怎样读成文档或资源对象”。它不负责决定 draw 顺序，也不负责解释 `SceneNode`、camera、light 的运行时语义。

| 问题 | 资产系统回答 | 继续阅读 |
|---|---|---|
| 文件从哪里找 | runtime asset root 与相对路径约定 | [资产根与目录](runtime-root-and-layout.md) |
| 一个 `.scene.yaml` 保存了什么 | `SceneDocument` / `SceneNodeDocument` 的 YAML 表面 | [场景也是资产](scene-assets.md) |
| 文档怎样变成运行时节点 | 不在资产系统内展开 | [场景系统](../../scene-system/index.md) |
| 一个 `.material` 怎样被场景引用 | `material.uri` -> `loadGenericMaterial()` -> `MaterialInstance` | [模型、纹理和材质如何接入](model-texture-material-flow.md) |
| 材质为什么有 pass，pipeline identity 如何确认 | 不在资产系统内展开 | [材质系统](../material/index.md) |
| mesh 为什么会影响 pipeline | mesh 提供顶点输入签名，细节属于 pipeline/material 链路 | [材质系统：什么是 Pipeline](../material/what-is-pipeline.md) |

## 继续阅读

从 [资产根与目录](runtime-root-and-layout.md) 开始，我们先把“仓库地址”讲清楚。只要路径模型稳定，后面的模型、材质和场景装配就不会混在一起。

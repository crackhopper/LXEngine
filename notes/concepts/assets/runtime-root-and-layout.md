# 资产根与目录

资产根像仓库的门牌号。模型、材质、shader、场景文件都可以放得很整齐，但如果运行时不知道仓库在哪里，后面的 loader 就只能靠当前工作目录碰运气。LXEngine 现在把这件事收敛到 `src/core/utils/filesystem_tools.*`。

## runtime root 先决定所有相对路径的起点

当前代码用 `initializeRuntimeAssetRoot()` 建立运行时根目录。它按这个顺序查找：

| 顺序 | 来源 | 说明 |
|---|---|---|
| 1 | `LX_RUNTIME_ROOT` | 显式指定运行时根，适合测试、脚本和外部启动器 |
| 2 | 传入的 hint 路径向上查找 | editor 或测试可以从已知位置反推仓库根 |
| 3 | 当前工作目录向上查找 | 直接从仓库或 build 目录启动时使用 |

一个目录要被认为是 runtime root，当前必须能看到：

| 必需路径 | 为什么需要 |
|---|---|
| `assets/materials` | `.material` loader 和 editor 材质 preset 需要它 |
| `assets/shaders/glsl` | shader 源文件编译和反射需要它 |

找到 root 之后，代码用 `resolveRuntimePath("assets/...")` 把逻辑路径转成磁盘路径。我们在文档和 YAML 里优先写 `assets/...` 这种相对 runtime root 的路径。

## 当前 assets 目录承担的职责

`assets/` 不是单一类型文件夹，而是运行时可读资源的集合：

| 目录 | 当前用途 |
|---|---|
| `assets/materials/` | `.material v2` 文件，描述 BSDF type、contract source、参数 envelope 和材质资源 URI |
| `assets/shaders/glsl/` | GLSL 源文件，以及当前仓库里已有的 `.spv` 编译产物 |
| `assets/models/` | 测试模型、示例模型、内置模型包 |
| `assets/models/builtin/` | editor 内置模型目录，子目录里的 `asset.yaml` 会被扫描 |
| `assets/textures/` | 独立纹理资源；有些模型包也会把贴图放在模型目录内部 |
| `assets/env/` | 环境贴图资源；HDR/EXR panorama 已能被当前环境资源路径读取，`khronos/neutral/` 保存 Khronos neutral KTX2 参考环境 |
| `assets/scenes/` | 仓库自带 scene 文档 |
| `assets/project_templates/` | 新建 project 时复制的只读模板 |

这套布局对应当前资产目录约定。测试里也会检查关键示例资产是否存在，避免资源目录被无意破坏。

`assets/env/khronos/neutral/ggx/specular.ktx2` 是 Khronos glTF Sample
Environments 的 GGX 预过滤 specular cubemap。当前只为它接入了受限
`TextureLoader::loadKtx2Cubemap()` 读取路径：uncompressed KTX2、
`VK_FORMAT_R16G16B16A16_SFLOAT`、6 faces、mip chain。它还没有成为
scene environment 的默认输入；`REQ-073-f` 会把 skybox/background pass、
RenderFeature 参数和 scene-level environment resource 一起收束。

## 逻辑路径和真实路径分开

在 scene 或 material 里，我们通常写逻辑路径：

```yaml
mesh:
  uri: assets/models/damaged_helmet/DamagedHelmet.gltf # -> resolveRuntimePath(...)
material:
  uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material # -> MaterialResourceParser
```

这让同一份 scene 文件可以在仓库根、build 目录、远程 editor 进程或测试环境中工作。只要 `LX_RUNTIME_ROOT` 或自动发现结果正确，逻辑路径就不需要知道实际磁盘绝对路径。

当前代码优先使用：

| 推荐 API | 用途 |
|---|---|
| `initializeRuntimeAssetRoot(...)` | 初始化 runtime root |
| `getRuntimeAssetRoot()` | 查询当前 root |
| `resolveRuntimePath(...)` | 把 `assets/...` 变成真实路径 |
| `getRuntimeShaderSourceDir()` | 找到 `assets/shaders/glsl` |

## 继续阅读

路径起点确定后，我们再看 [模型、纹理和材质如何接入](model-texture-material-flow.md)。那一页会讲 loader 如何把这些路径指向的文件转换成 runtime 对象。

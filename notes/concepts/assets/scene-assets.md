# 场景文件也是资产

场景文件像一张表单化的布置图。资产系统关心的是这张布置图怎样写进磁盘、怎样读回 `SceneDocument`，以及它用哪些 URI 引用模型、材质和贴图。至于读回之后节点怎样进入运行时场景，由 [场景系统](../../scene-system/index.md) 继续解释。

所以我们不要把“资产系统”只理解成 mesh 和 texture。对 editor 来说，场景文档本身就是最重要的资产之一。

## scene 文件保存的是文档，不是 runtime 对象

当前 scene 文件读入后先变成 `SceneDocument`。其中每个节点是 `SceneNodeDocument`，它保存可序列化的数据：

| 文档字段 | 运行时对应 |
|---|---|
| `nodeName` | `SceneNode::getNodeName()`，稳定节点标识 |
| `name` | `SceneNode::getName()`，editor 显示名 |
| `transform` | `SceneNode` local transform |
| `visibilityMask` | 节点可见层 |
| `mesh.uri` | mesh 资产引用 |
| `material.uri` | material 资产引用 |
| `materialOverrides` / `nodeMaterialOverrides` | 写入 `MaterialInstance` 的参数覆盖 |
| `camera` | `CameraComponent` 状态 |
| `light` | `DirectionalLight` / `PointLight` / `SpotLight` 状态 |
| `children` | scene node 层级 |

资产系统到这里停在文档层。运行时真正的 `SceneNode` 树由 `SceneRuntime` 创建，这部分见 [场景系统：文档到 Runtime](../../scene-system/document-runtime-flow.md)。

## 当前保存格式是 root 树

`SceneDocument` loader 接受当前模板里仍在使用的 flat `nodes:` 列表，也接受显式 `root:` 树。保存时会写出显式 root 树。概念上我们以 root 树理解 scene：

```yaml
scene: "Example Scene"                  # -> SceneDocument::sceneName()
gameplayCameraPath: "/game_cam"         # -> runtime 要找到的 gameplay camera 节点路径

root:
  nodeName: scene_root                  # -> Scene 根节点；必须是无 payload 的根
  children:
    - nodeName: game_camera             # -> SceneNode::getNodeName()
      name: game_cam                    # -> SceneNode::getName()
      transform:
        translation: [0.0, 2.0, 6.0]
      camera:                           # -> CameraComponent
        eye: [0.0, 2.0, 6.0]
        target: [0.0, 0.0, 0.0]
        up: [0.0, 1.0, 0.0]
        type: Perspective
        fovY: 45.0
        nearPlane: 0.1
        farPlane: 1000.0

    - nodeName: helmet_node
      name: helmet
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      mesh:
        uri: assets/models/damaged_helmet/DamagedHelmet.gltf # -> mesh asset
      material:
        uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material # -> MaterialInstance
      materialOverrides:
        baseColor: { kind: rgb, value: [0.8, 0.7, 0.4] }     # -> material envelope override
        roughness: { kind: float, value: 0.35 }
```

这个 YAML 片段体现了资产系统的核心关系：scene 不直接嵌入模型和材质内容，而是保存 URI 和少量覆盖值。加载时再根据 URI 找到真实资产。

## 反序列化入口负责把 YAML 读成文档

`SceneDocument` 的加载逻辑负责把 YAML 表面读成可验证的文档对象：

| YAML 表面 | 文档字段 | 说明 |
|---|---|---|
| `scene` | `SceneDocument::sceneName()` | 场景名称 |
| `gameplayCameraPath` | `SceneDocument::gameplayCameraPath()` | 运行时要查找的 gameplay camera 路径 |
| `root` | `SceneDocument::rootNode()` | 保存格式使用的显式根 |
| `nodes` | loader 归一化为 root children | 当前模板仍可使用的 flat 表面 |
| `mesh.uri` | `SceneNodeDocument::meshUri` | mesh 文件或 builtin URI |
| `material.uri` | `SceneNodeDocument::materialUri` | `.material` 文件或 builtin material URI |
| `camera` | `CameraNodeState` | 相机参数的序列化形式 |
| `light` | `LightNodeState` | Directional / Point / Spot 的序列化形式 |

这里有一个重要边界：scene 文档是持久化资产，editor camera、debug draw 节点这类 runtime-only 内容不会按普通场景节点保存。

## 材质覆盖属于 scene 对资产实例的局部改写

同一个 `.material` 可以被多个节点引用。scene 里的 `materialOverrides` / `nodeMaterialOverrides` 表示“这个节点使用这份材质时，某些参数换成文档里的值”。当前 v2 材质覆盖目标是 BSDF 参数 envelope，不是旧的 `binding.member`。

```yaml
material:
  uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
materialOverrides:
  baseColor: { kind: rgb, value: [1.0, 0.8, 0.25] }
  roughness: { kind: float, value: 0.25 }
```

覆盖写入时会用材质 contract 校验参数是否存在、kind 是否匹配、资源 URI 是否能解析。也就是说，scene 只声明想改什么参数；参数是否合法由材质系统决定。

## 和场景系统文档的关系

这页从资产角度看 scene：`.scene.yaml` 是一种可保存、可复制、可作为 project 内容管理的资产。

[场景系统](../../scene-system/index.md) 会从运行时对象角度继续解释：`Scene`、`SceneNode`、component、camera、light、visibility layer 如何在内存里协作。

## 继续阅读

接下来可以看 [内置资产目录](builtin-catalog.md)。它解释 editor 为什么能列出一批可插入模型，以及这些模型如何通过 manifest 接入 scene 文件。

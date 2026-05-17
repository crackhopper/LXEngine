# 内置资产目录

内置资产目录像仓库里的索引卡。模型文件本身只说明“盒子里有什么几何数据”，但 editor 还需要知道它在 UI 里叫什么、属于哪个分类、默认用哪个材质、有没有默认贴图、授权是否允许随仓库分发。

这些信息由 `assets/models/builtin/**/asset.yaml` 提供，读取逻辑在 `BuiltinAssetCatalog`。

## catalog 扫描 asset.yaml

`BuiltinAssetCatalog::refresh(root)` 会递归扫描 root 下所有名为 `asset.yaml` 的文件。当前 `lxe_editor` 使用的 root 是：

```text
assets/models/builtin
```

扫描后会得到一组 `BuiltinModelAsset`，并按 `category` 和 `displayName` 排序。editor 可以用这组数据生成内置模型列表，场景装配时也可以根据 `meshUri` 找回对应的默认贴图。

## manifest 描述一个可插入模型

一个典型的内置模型 manifest 长这样：

```yaml
assetId: "characters_blocky_a"                                           # -> 稳定资产 id
displayName: "Blocky Character A"                                        # -> editor 显示名
category: "characters"                                                   # -> UI 分类和排序
meshUri: "assets/models/builtin/characters/characters_blocky_a/model.obj" # -> mesh asset
defaultMaterialUri: "assets/materials/blinnphong_textured.material"       # -> 默认材质
albedoTextureUri: "assets/models/builtin/characters/characters_blocky_a/Textures/texture-a.png"
materialFiles:
  - "character-a.mtl"                                                     # -> 原始包内文件记录
textureFiles:
  - "Textures/texture-a.png"
sourcePack: "Kenney Blocky Characters"
sourceUrl: "https://kenney.nl/assets/blocky-characters"
license: "CC0-1.0"
commercialUse: true
triangleCount: 72
modelBytes: 7011
resourceBytes: 20271
assetBytes: 34610
```

这里最关键的是三条运行时引用：

| 字段 | 运行时用途 |
|---|---|
| `meshUri` | 插入 scene 时写入节点的 `mesh.uri` |
| `defaultMaterialUri` | 没有手动指定材质时写入节点的 `material.uri` |
| `albedoTextureUri` | `SceneBuilder` 构建内置模型节点时尝试绑定的默认 albedo 贴图 |

授权、来源、大小和三角形数更多是资产治理信息。它们让我们知道这个模型为什么可以放进仓库，以及它是否符合内置资产预算。

## root manifest 约束整个目录

`assets/models/builtin/README.asset.yaml` 描述整个内置资产目录的规则：

```yaml
description: "LXEngine built-in model asset manifest root"
licensePolicy: "Only commercial-use friendly CC0 assets are imported here."
maxTrianglesPerModel: 1000
maxModelBytes: 262144
```

这不是运行时加载单个模型所必需的字段，但它给目录维护者一个清楚边界：内置资产应该小、可分发、授权友好。

## builtin URI 和 manifest URI 是两类入口

当前 editor 同时存在两类内置入口：

| URI 形式 | 例子 | 来源 |
|---|---|---|
| 特殊 builtin URI | `builtin://lxe_editor/primitives/cube` | 代码创建 primitive mesh |
| manifest mesh URI | `assets/models/builtin/.../model.obj` | `asset.yaml` 指向真实模型文件 |

特殊 builtin URI 不需要 manifest；它们是 editor 代码内置的 primitive 或示例入口。manifest mesh URI 则更接近普通资产：它最终仍然是 `assets/...` 路径，只是多了一张索引卡帮助 editor 展示和默认装配。

## 继续阅读

内置资产最终仍会进入 scene。回到 [场景也是资产](scene-assets.md) 可以看到这些 URI 如何保存进 `.scene.yaml`；继续看 [材质系统](../material/index.md) 可以理解默认材质如何影响 pass 和 pipeline。

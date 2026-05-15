# 03 加载与保存场景：先有项目，再有场景

场景文件像工作台上的一张布置图，但一张布置图通常不会单独漂在外面。我们会把它放进一个项目文件夹里，旁边还可以放贴图、模型、材质和 editor 的辅助状态。LXEngine 现在按这个顺序组织：`project_template` 提供只读起点，`project` 是我们的工作文件夹，`scene` 是 project 里的一个可打开文档。

## project_template 是只读样板

`project_template` 像新建工程时选择的项目类型。它放在 `assets/project_templates/`，由仓库提供，启动练习时只读取，不直接修改。

| 概念 | 类比 | 当前位置 |
|---|---|---|
| `project_template` | 只读样板间 | `assets/project_templates/<type>/project_template.yaml` |
| `project` | 我们复制出来的工作室 | `data/projects/<name>/project.yaml` |
| `scene` | 工作室里的布置图 | `data/projects/<name>/scenes/*.scene.yaml` |
| project assets | 跟项目一起走的材料柜 | `data/projects/<name>/assets/` |

先列出可用模板：

```text
project templates
```

当前内置模板是 `empty`。它包含一个最小场景：一个 gameplay camera 和一个 directional light。

## 从模板创建第一个项目

接下来我们从模板创建一个自己的项目：

```text
project init empty my_first_project
```

这条命令会把 `empty` 模板复制到 `data/projects/my_first_project/`，生成 `project.yaml`，并把模板里声明的默认 scene 排进加载队列。runtime scene 会在下一次 editor update tick 绑定完成，所以我们用 `project status` 查看项目层状态，用 `state summary` 或画面确认 runtime 已经切换。

```text
project status
```

`project status` 返回的结构里最重要的是：

| 字段 | 意义 |
|---|---|
| `id` | 当前 project 的稳定 id |
| `path` | project 文件夹路径 |
| `activeScene` | project metadata 记录的当前 scene |
| `dirty` | project metadata 或当前 scene 是否有待保存变化 |

## 一个项目可以有多个 scene

项目像文件夹，scene 像文件夹里的多个布置图。我们先列出当前 project 的 scenes：

```text
scene list
```

然后创建一个新的练习 scene：

```text
scene new lighting_test
```

`scene new` 会在当前 project 下写入 `scenes/lighting_test.scene.yaml`，把它登记进 `project.yaml`，并把它设为 active scene。我们可以显式打开它：

```text
scene open lighting_test
```

这里的 `scene open` 只在当前 project 内解析 scene id 或 scene 路径，不会去全局 `assets/scenes/` 和 `data/scenes/` 搜索。

## scene save 和 project save 的边界

保存时先分清两件事：scene 保存当前布置图，project 保存项目目录和 active scene 这层索引。

```text
scene save
project save
```

| 命令 | 保存什么 | 常见时机 |
|---|---|---|
| `scene save` | 当前 runtime scene 对应的 `.scene.yaml`，以及同名 editor sidecar state | 修改了节点、相机、灯光、材质后 |
| `project save` | `project.yaml`，包括 scene 列表、active scene、asset roots 等 metadata | 新建、复制、删除 scene 后 |

在 editor 中，`project save` 也会保存 active scene，避免项目 metadata 和当前 scene 内容脱节。我们仍然把两个命令分开理解，因为它们对应不同层的文件。

## scene YAML 如何记录一个节点

一个 scene 节点大致像这样：

```yaml
nodeName: primitive_cube_1        # -> SceneNode::getNodeName()
name: Cube                        # -> SceneNode::getName(), editor path 显示
transform:                        # -> LX_core::Transform
  translation: [0.0, 0.5, 0.0]
meshUri: builtin://lxe_editor/primitives/cube
materialUri: assets/materials/rtr_experiment_template.material
```

我们不需要一开始记住所有字段。先记住：scene file 负责保存场景内容，project file 负责把这些 scene 组织成一个工作单元。

## 我们已经学会了什么

我们已经把“加载和保存”拆成了三层：只读 `project_template` 提供起点，`project` 拥有可写文件夹，`scene` 保存当前布置图。之后所有编辑动作都发生在当前 project 的 active scene 上。

## 下一步

进入 [04 基础场景编辑](04-basic-authoring.md)。

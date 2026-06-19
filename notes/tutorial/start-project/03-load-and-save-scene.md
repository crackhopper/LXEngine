# 03 加载与保存场景：先分清 scene asset 和 project scene

场景文件像工作台上的布置图。当前 editor 支持两种入口：没有 project 时可以直接 `scene open <path>` 打开一个 scene asset；打开 project 后，`scene open` / `scene save` 才落到 project 的 scene catalog 和 `project.yaml`。

## 当前主线先直接打开 scene asset

从 Console 运行：

```text
scene open assets/scenes/generated/helmet_standard_pbr.scene.yaml
```

这条命令会把 scene 读入 pending runtime，并在下一次 update tick 切换到新 runtime。它不要求当前已经打开 project，也不会把 asset scene 写入 `data/projects/`。

确认运行时已加载：

```text
state summary
state scene
```

我们关心两件事：`sceneName` 是否切到目标 scene，以及 cameras/lights/renderables 是否已经进入 runtime。

## project 是保存边界

`scene save` 只保存当前 project 的 active scene。如果当前没有 project，保存会失败并提示需要先打开已有 project，或先把外部 scene 导入一个可写 project。仓库不再把内置 project template 作为教程主线。

| 命令 | 当前含义 |
|---|---|
| `project list` | 列出 `data/projects/` 中已有 project |
| `project open <id-or-path>` | 打开已有 project，并排队打开它的 active scene |
| `scene open <path>` | 没有 project 时直接打开 scene asset；有 project 时优先按 project scene 解析，失败后回退到路径打开 |
| `scene import <source-path> [scene-id]` | 有 project 且提供 scene id 时，把外部 scene 复制进 project |
| `scene save` | 保存当前 project active scene；没有 project 时不可用 |

## 把现有 asset scene 导入 project

如果我们已经有一个 project，可以把仓库里的 scene asset 导入为 project scene：

```text
project open <project-id-or-path>
scene import assets/scenes/generated/helmet_standard_pbr.scene.yaml helmet_standard_pbr
scene save
project save
```

`scene import` 会把源文件复制到当前 project 的 `scenes/<scene-id>.scene.yaml`，登记到 `project.yaml`，并把它设为 active scene。之后的 `scene save` 写回 project 内的副本，不会覆盖仓库 `assets/scenes/` 原文件。

## scene YAML 如何记录一个节点

一个 scene 节点大致像这样：

```yaml
nodeName: damaged_helmet
name: damaged_helmet
transform:
  translation: [0.0, 0.0, 0.0]
mesh:
  uri: assets/models/damaged_helmet/DamagedHelmet.gltf
material:
  uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
```

我们不需要一开始记住所有字段。先记住：scene file 保存场景内容，project file 负责把这些 scene 组织成一个可写工作单元。

## 我们已经学会了什么

我们已经把“加载和保存”拆成了两层：scene asset 可以直接打开验证，project scene 才是可保存的工作副本。当前教程不再依赖内置 project template。

## 下一步

进入 [04 基础场景编辑](04-basic-authoring.md)。

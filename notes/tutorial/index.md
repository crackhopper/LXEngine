# Tutorial：从跑起来到扩展 editor

这组教程解决一个连续问题：我们先把 LXEngine 跑起来，再学会改变场景里的表面、灯光和编辑器行为，最后理解怎样为未来扩展新增 editor 能力和场景节点类型。阅读顺序不是按目录排，而是按依赖排：没有 editor，材质和场景验证就没有落点；不理解 command 和 scene node，扩展教程就只会变成零散改文件。

## 五条学习线之间的依赖

| 顺序 | 系列 | 学习目标 |
|---|---|---|
| 1 | [启动项目](start-project/index.md) | 把工程构建起来，打开 editor，加载和保存场景 |
| 2 | [自定义材质](custom-material/index.md) | 理解材质系统，并写一个 Gooch shader |
| 3 | [自定义灯光](custom-light/index.md) | 理解当前 light 底座和未来 custom light 工作流 |
| 4 | [扩展编辑器](extend-editor/index.md) | 理解 toolbar 与 command bus 如何配合 |
| 5 | [扩展场景节点](extend-scene-node/index.md) | 理解一种新节点如何兼容 editor 全流程 |

前两条学习线偏当前实践：构建、启动、保存场景、写材质、在 editor 验证。后三条学习线会同时讲“当前手工路径”和“未来应该沉淀成扩展点的路径”。

## 当前可实践的路径与未来扩展路径

| 未来路径 | Requirement |
|---|---|
| 光源资产与自定义光源注册 | [REQ-042-a](../requirements/042-a-tutorial-light-asset-and-custom-light-registry.md) |
| toolbar / command 扩展注册 | [REQ-042-b](../requirements/042-b-tutorial-editor-extension-registry.md) |
| 自定义场景节点注册 | [REQ-042-c](../requirements/042-c-tutorial-custom-scene-node-registry.md) |

凡是教程描述当前还没有的顺滑工作流，都会明确链接到 active requirement。这样我们能讲清楚设计方向，同时不把未来能力伪装成当前已经存在的 API。

## 按目标选择阅读顺序

| 目标 | 建议路径 |
|---|---|
| 只想先跑起来 | 启动项目 01 到 03 |
| 想做一个能看见的材质实验 | 启动项目全部 + 自定义材质全部 |
| 想理解 light 和 shader 的连接 | 自定义材质 01/02 + 自定义灯光 01/02/05 |
| 想改 editor 操作入口 | 启动项目 04 + 扩展编辑器 |
| 想新增一种场景对象 | 启动项目 03/04 + 扩展场景节点 |

我们不要求一开始理解所有底层 Vulkan 细节。先把一条链路跑通，再回头读 [概念](../concepts/scene/index.md) 和 [子系统设计](../subsystems/index.md)，会更稳。

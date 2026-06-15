# 新增节点 kind 的当前触点：让一种新道具进剧场

新增一种节点 kind 像把新道具引入剧场：道具要写进清单，搬上舞台，能被选中，能被复制，能画辅助线，还要能在演出结束后重新摆回同样位置。当前仓库具备这些基础能力，但新增语义仍要手工接入多处 editor 触点。

## 一个教学例子：ProbeVolume

我们用 `ProbeVolume` 做例子。它表示一个调试用空间盒子，可能用于标记采样区域或可视化区域。这个例子强调扩展触点，不要求当前引擎已经实现 ProbeVolume。

| 字段 | 含义 | 类比 |
|---|---|---|
| `size` | 盒子尺寸 | 道具占地范围 |
| `color` | debug draw 颜色 | 舞台图上的标记颜色 |
| `enabled` | 是否参与调试显示 | 是否把道具摆出来 |

这个例子强调的是节点扩展链路，不要求当前引擎已经有 ProbeVolume 功能。

## 当前手工触点

| 触点 | 需要回答的问题 |
|---|---|
| scene document | `ProbeVolume` 的 payload 怎样保存 |
| runtime factory | 加载时怎样创建 runtime node |
| Inspector | `size`、`color`、`enabled` 如何编辑 |
| CommandBus | 如何创建、修改、复制该节点 |
| scene tree | 节点怎样显示、重命名和删除 |
| DebugDraw | 盒子 helper 怎样画 |
| picking bounds | 点击哪里算选中 |
| duplicate policy | 复制时 payload 是否深拷贝 |
| API summary | 远程诊断怎样看见它 |

这张表就是新增节点语义的真实成本。当前教程不把它伪装成已经有 registry 可以自动接线。

## 当前能保持的原则

| 原则 | 做法 |
|---|---|
| 通用操作不分叉 | select、rename、move 继续走 `SceneNode` 和 command |
| 行为集中 | 新节点改状态也通过 command |
| 文档是权威保存形状 | runtime 可重建，不依赖临时 UI 状态 |
| debug helper 可诊断 | 新节点至少能显示 bounds |

## 我们已经学会了什么

我们知道新增节点 kind 的难点不是创建一个 C++ 类型，而是让它加入 editor 的整套操作合同。

## 下一步

进入 [04 Debug draw 与 picking](04-debug-draw-and-picking.md)，专门看“能看见”和“能点中”这两件事。

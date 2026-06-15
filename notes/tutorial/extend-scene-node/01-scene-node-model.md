# SceneNode 心智模型：道具、标签和零件

`SceneNode` 像舞台道具的底座：底座有名字、位置、父子层级和可见性；底座上可以装不同零件，比如 mesh、material、skeleton、camera 或 light。我们扩展节点时，先要分清“底座能力”和“零件能力”。

## 当前核心对象

| 对象 | 作用 | 类比 |
|---|---|---|
| `Scene` | 持有场景级资源和节点命名空间 | 舞台管理表 |
| `SceneNode` | 节点底座，管理 transform、path、component | 道具底座 |
| `IComponent` | 节点可挂载能力 | 可拆装零件 |
| `MeshComponent` | 提供几何 | 道具外形 |
| `MaterialComponent` | 提供表面材质 | 道具涂装 |
| `PerDrawData` | 每次 draw 的模型矩阵等数据 | 演出时递给渲染器的小纸条 |

这个模型让我们知道：新增“节点种类”不一定要改 `SceneNode` 本体。很多时候，我们是在定义一种特定的 component 组合和文档负载。

## Path 与 nodeName 的区别

| 名称 | 用途 | 说明 |
|---|---|---|
| `name` / `getPath()` | editor 选择、命令、层级操作 | 面向作者和工具 |
| `nodeName` | 渲染调试身份 | scene 内要求唯一 |
| `path` | `/world/player` 这类层级定位 | command 和 API 常用 |

扩展节点时不要把这些概念混在一起。`path` 解决“我们点的是哪个道具”，`nodeName` 更偏渲染调试身份。

## 当前通用操作

| 操作 | 节点需要支持什么 |
|---|---|
| select / deselect | 能通过 path 找到 runtime node |
| move / rotate / scale | 有 transform 并能更新 world transform |
| rename | path 和 scene tree 能刷新 |
| copy / paste / duplicate | 文档负载能被复制 |
| remove | runtime 与文档都能删除 |
| save / load | scene document 能 round-trip |
| debug draw | 能提供 bounds 或 helper |

新增一种节点语义时，当前要手工确认这些操作仍然成立。

## 我们已经学会了什么

我们把节点理解为“统一底座 + 可选零件”。扩展时要保护底座通用操作，再增加新语义。

## 下一步

进入 [02 当前节点如何保存和重建](02-document-and-runtime.md)，看 scene document 怎样变成 runtime node。

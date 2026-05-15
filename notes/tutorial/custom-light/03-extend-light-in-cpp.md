# C++ 扩展光源能力：新增一种灯具要改哪些齿轮

新增 light kind 像给舞台系统接入一种新灯具。灯本身只是一个零件，真正麻烦的是灯控台、布线图、调试标记和演出程序都要认识它。当前仓库可以解释这条链路，但还没有把它收束成稳定插件接口。

## 当前手工路径

| 步骤 | 需要触达的区域 | 原因 |
|---|---|---|
| 定义数据 | `src/core/scene/light.hpp` | C++ 侧要有新 light 的参数结构 |
| 扩展文档 | `scene_document.hpp` / scene runtime loader | scene 文件要能保存和读取 kind 与字段 |
| 收集运行时数据 | `scene_runtime.cpp` | editor scene 要转换成 renderer 使用的数据 |
| 扩展命令 | `builtin_commands.cpp` | command bus 要能创建和修改新字段 |
| 扩展 Inspector | editor UI 相关代码 | 作者要能看到和编辑字段 |
| 扩展 debug draw | editor helper 相关代码 | 视口里要能看懂 light 范围 |
| 扩展 shader 合同 | `scene_lights_ubo.glsl` 与相关 shader | 渲染公式要能使用新数据 |

这张表就是“新增 light kind”的真实成本。教程可以带我们理解它，但不能把这条手工路径说成轻量扩展点。

## 一个假想的 TubeLight

我们用 `TubeLight` 做教学例子：它像一根发光灯管，需要两个端点、颜色、强度和影响半径。

| 参数 | 含义 | 对应现有概念 |
|---|---|---|
| `start` | 灯管起点 | 类似 point light 的位置 |
| `end` | 灯管终点 | 新增形状信息 |
| `color` | 颜色 | 与现有 light 一致 |
| `intensity` | 强度 | 与现有 light 一致 |
| `range` | 影响距离 | 与 point / spot 类似 |

如果今天手工实现它，第一步会是在 C++ light 数据里表达这些字段。随后每个使用 light 数据的层都要回答同一个问题：`TubeLight` 应该怎样保存、怎样编辑、怎样画 debug helper、怎样传给 shader。

## 为什么需要未来注册表

当前三类 light 是内置形状，代码可以直接写死分支。自定义 light 则需要一个集中登记处，像剧院的设备清单：登记一次，灯控台、舞台图和调试工具都能查到它。

[REQ-042-a](../../requirements/042-a-tutorial-light-asset-and-custom-light-registry.md) 计划把这些分散知识收束成 light kind registry。未来教程会从“注册一种 light”开始，而不是让新人同时追七个模块。

## 我们已经学会了什么

我们看清了当前新增 light kind 的真实链路：数据、文档、运行时、命令、Inspector、debug draw、shader 都要对齐。

## 下一步

进入 [04 光源资产与注册表](04-light-assets-and-registry.md)，用未来路径把这条链路整理成更容易学习的模型。

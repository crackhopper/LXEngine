# 在 editor 中验证光源：同时看灯、看影子、看记录单

验证 light 像检查舞台布光：只看舞台亮不亮还不够，我们还要看灯具是否在正确位置、记录单是否保存了参数、shader 是否拿到了同一份数据。

## 验证顺序

| 顺序 | 检查点 | 目的 |
|---|---|---|
| 1 | 场景树里存在 light 节点 | 确认创建成功 |
| 2 | Inspector 字段与预期一致 | 确认 scene document 字段可编辑 |
| 3 | 视口里有视觉变化 | 确认 runtime 和 shader 接上 |
| 4 | 保存 scene 后检查 YAML | 确认参数持久化 |
| 5 | 重新加载 scene | 确认 round-trip 没丢字段 |

## 现象对应到哪条链路

| 现象 | 优先检查 | 说明 |
|---|---|---|
| 创建命令没有 light | `builtin_commands.cpp` 的 completion | command surface 可能没有暴露该 kind |
| Inspector 没有字段 | `LightNodeState` 与 UI 映射 | 文档字段存在不代表 UI 已经展示 |
| 视口无变化 | `scene_runtime.cpp` 收集逻辑 | scene 节点可能没有进入 `SceneLightsData` |
| shader 结果异常 | `scene_lights_ubo.glsl` | C++ / GLSL 布局或字段含义可能不一致 |
| 保存后丢参数 | scene 序列化逻辑 | 字段需要参与保存和读取 |

## Debug draw 的角色

Debug draw 像舞台平面图：它不替代真实照明，但能让我们看见光源的方向、范围和 cone。对 light 教程来说，debug draw 很重要，因为许多 light bug 不是“灯坏了”，而是“灯朝向不对”或“范围太小”。

未来 custom light registry 会要求每个 light kind 声明 `debugShape`，这部分同样由 [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) 跟踪。

## 我们已经学会了什么

我们形成了 light 验证的固定顺序：先确认节点，再确认字段，再看 runtime 视觉变化，最后保存和重新加载。

## 下一步

继续进入 [扩展编辑器](../extend-editor/index.md)，学习 toolbar 与 command 如何成为作者入口。

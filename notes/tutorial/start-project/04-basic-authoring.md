# 04 基础场景编辑：完成最小作者循环

这一章我们开始真正摆放物体。把 editor 想成一个小舞台：primitive 是演员，相机是观众的眼睛，light 是舞台灯。

## 场景里先要认识的四类对象

| 对象 | 类比 | 当前支持 |
|---|---|---|
| Primitive | 舞台道具 | cube / sphere / plane / cylinder / cone |
| Camera | 观众眼睛 | perspective camera |
| Light | 舞台灯 | directional / point / spot |
| Transform | 道具的位置姿态 | translation / rotation / scale |

## 创建、选择、修改都走同一套命令

| 部分 | 文件 |
|---|---|
| toolbar 创建入口 | `src/demos/lxe_editor/ui_overlay.*` |
| 创建命令 | `src/core/editor/commands/builtin_commands.*` |
| scene document payload | `src/demos/lxe_editor/scene_document.*` |
| runtime 构建 | `src/demos/lxe_editor/scene_runtime.*` |

这些文件对应一个共同原则：toolbar 和 Inspector 是入口，scene document 负责保存，runtime 负责把文档变成可渲染对象。

## 先做一遍完整编辑闭环

1. 在 toolbar 里创建一个 `Cube`。
2. 点击视口或 Scene Tree 选中它。
3. 在 Inspector 里修改 transform。
4. 用 duplicate / copy-paste 复制一个节点。
5. 创建一个 Point Light，调高 intensity 或 range。
6. 按 Preview 进入 gameplay camera 预览，再退出。
7. 执行 `scene save`。

同一批操作也可以通过 Console 命令表达。典型命令包括：

```text
select /Cube
move /Cube 0 1 0
set /Cube.visibilityMask 0xffffffff
undo
redo
```

命令名和参数以后会继续演进，但核心原则不变：UI 只是更容易点的入口，真正的修改路径是 command。

## 编辑时常见的三个分岔

| 现象 | 解释 |
|---|---|
| 物体选不中 | 可能没有可用 bounds，或当前在 Preview 模式 |
| 调了 light 但画面不明显 | 当前 shader 是否消费 `SceneLightsUBO` 或 light 参数 |
| 保存后路径变化 | asset 场景在 user 模式下会重定向到 local |

## 我们已经学会了什么

我们已经完成最小作者循环：创建、选择、修改、预览、保存。这是后续材质、光源和节点扩展教程的基础。

## 下一步

进入 [05 启动排错](05-troubleshooting.md)，或者开始 [自定义材质](../custom-material/index.md)。

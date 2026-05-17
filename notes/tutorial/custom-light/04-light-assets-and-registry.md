# 光源资产与注册表：把灯具型号写进设备清单

未来的 light asset 可以像“灯具型号卡”：它记录一个 light kind 的默认颜色、强度、范围和 editor 展示方式。注册表则像剧院设备清单，editor 和 runtime 通过它认识有哪些灯具可以创建。

> 这一章描述的是未来教程目标，由 [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) 跟踪。当前仓库还没有完整 light asset / registry 工作流。

## 未来心智模型

| 对象 | 作用 | 类比 |
|---|---|---|
| Light kind registry | 登记可创建 light 类型 | 剧院设备清单 |
| Light preset asset | 保存一种 light 的默认参数 | 灯具型号卡 |
| Scene node light state | 场景里某一盏灯的实例参数 | 舞台上的实际灯 |
| Inspector schema | 描述哪些字段可编辑 | 灯控台面板 |
| Debug shape | 视口里的辅助形状 | 舞台布光图 |

注册表的价值，是让这些对象围绕同一个 `kind` 对齐。这样新增 `TubeLight` 时，教程能让我们先登记类型，再逐步补齐渲染逻辑。

## 未来 light preset 形状

```yaml
kind: Spot                              # -> registry.lookup("Spot")
displayName: Warm Stage Spot           # -> editor 创建菜单显示名
defaults:                              # -> 创建新 light 时的 LightNodeState 默认值
  color: [1.0, 0.92, 0.72]
  intensity: 4.0
  range: 8.0
  innerConeDegrees: 18.0
  outerConeDegrees: 34.0
inspectorFields:                       # -> Inspector 自动生成编辑控件
  - color
  - intensity
  - range
  - innerConeDegrees
  - outerConeDegrees
debugShape: cone                       # -> 视口 debug helper
```

这个 YAML 不是当前可直接加载的格式。它是 requirement 里的教学目标：让资产文件、editor 和 runtime 有一张共同表。

## 未来教程会怎样操作

| 步骤 | 教程动作 | 预期结果 |
|---|---|---|
| 1 | 新建 `assets/lights/warm_spot.light.yaml` | light preset 可以被 registry 发现 |
| 2 | 在 editor 创建该 preset | 场景出现带默认参数的 light 节点 |
| 3 | Inspector 修改参数 | 字段按 schema 显示并保存 |
| 4 | 保存并重新加载 scene | `kind` 和参数 round-trip |
| 5 | 打开 debug helper | 视口显示对应 debug shape |

## 当前能提前练习什么

在 registry 完成前，我们仍然可以练习三件事：

| 当前练习 | 对未来能力的帮助 |
|---|---|
| 使用内置三类 light | 理解 light state 的字段 |
| 阅读 `scene_runtime.cpp` | 理解 scene 到 runtime 的转换 |
| 对照 shader UBO | 理解 C++ 与 GLSL 的布局合同 |

## 我们已经学会了什么

我们把未来 custom light 工作流拆成了 registry、preset asset、scene instance、Inspector schema 和 debug shape 五个角色。

## 下一步

进入 [05 在 editor 中验证](05-verify-and-debug-lights.md)，学习当前可用的验证和排错方法。

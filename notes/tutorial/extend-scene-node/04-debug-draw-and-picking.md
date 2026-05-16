# Debug draw 与 picking：让新节点看得见、点得到

Debug draw 像舞台平面图上的虚线框，picking 像我们用手指点图纸上的道具。自定义节点如果没有 mesh，也仍然需要某种可视边界，否则 editor 很难选择、移动和诊断它。

## 两种可视化

| 可视化 | 目的 | 例子 |
|---|---|---|
| 渲染外观 | 游戏或场景里真正看到的对象 | cube、helmet、地面 |
| debug helper | editor 为理解对象画的辅助形状 | AABB、light cone、点击命中点 |

新节点不一定有渲染外观，但应该尽量有 debug helper。尤其是 volume、trigger、light、camera 这类编辑对象，helper 是作者理解它们的主要方式。

## Picking 需要什么

| 来源 | 适用场景 | 说明 |
|---|---|---|
| mesh bounds | 可渲染节点 | 从几何范围计算点击命中 |
| explicit bounds | 非渲染节点 | kind 声明自己的选择范围 |
| debug shape bounds | helper 节点 | 用 helper 形状辅助选择 |

当前 editor 已经在选择命中后显示 world-space AABB 和交点辅助标记。未来自定义节点 kind 需要声明 `boundsPolicy`，让 picking 不必猜。

## ProbeVolume 的 debug draw 示例

```yaml
kind: ProbeVolume                       # -> future node kind registry
payload:
  size: [2.0, 1.5, 2.0]                 # -> explicit bounds
  color: [0.2, 0.8, 1.0, 1.0]           # -> debug draw style
debugDraw:
  shape: box                            # -> DebugDraw helper
  boundsPolicy: payload.size            # -> picking bounds source
```

这仍然是未来目标格式，由 [REQ-042-c](../../requirements/042-c-tutorial-custom-scene-node-registry.md) 跟踪。当前教程用它帮助我们理解“payload、debug draw、picking bounds”之间的关系。

## 验证清单

| 验证 | 预期 |
|---|---|
| 选中节点 | 视口显示 helper 或 AABB |
| 移动节点 | helper 跟随 transform |
| 保存后加载 | helper 参数不丢失 |
| 复制节点 | bounds 与 payload 独立 |
| API 查询 | 能看到节点 kind 和调试状态 |

## 我们已经学会了什么

我们知道自定义节点要兼容 editor，必须同时考虑可视 helper 和 picking bounds。看得见、点得到，后续操作才有基础。

## 下一步

进入 [05 未来节点注册表](05-future-node-kind-registry.md)，把这些规则整理成 kind metadata。

# 未来节点注册表：给每种道具一张说明卡

未来的 node kind registry 像道具说明卡：它说明这种节点叫什么、保存哪些 payload、运行时怎样创建、Inspector 显示哪些字段、怎样画 debug helper、怎样被选择和复制。这样新增节点时，我们不必在 editor 多处手工接线。

> 这一章描述的是未来教程目标，由 [REQ-042-c](../../requirements/042-c-tutorial-custom-scene-node-registry.md) 跟踪。当前仓库还没有完整 custom scene node kind 注册入口。

## 未来 metadata 形状

```yaml
nodeKinds:
  - kind: ProbeVolume                   # -> scene document stable kind
    displayName: Probe Volume           # -> editor UI label
    components: []                      # -> runtime component factory inputs
    documentPayload:                    # -> kind-specific scene payload schema
      size: vec3
      color: rgba
      enabled: bool
    inspectorFields:                    # -> Inspector generated fields
      - size
      - color
      - enabled
    debugDraw:
      shape: box                        # -> DebugDraw helper
      boundsPolicy: payload.size        # -> picking / selection bounds
    duplicatePolicy: deep-copy-payload  # -> copy / paste behavior
```

这份 metadata 的作用不是替代 C++ runtime factory，而是把 editor 需要知道的规则放在同一处。

## 未来 scene YAML

```yaml
nodes:
  - name: RoomProbe
    kind: ProbeVolume                   # -> registry.lookup("ProbeVolume")
    transform:
      translation: [0.0, 1.0, 0.0]
    payload:                            # -> ProbeVolume documentPayload
      size: [4.0, 2.5, 4.0]
      color: [0.2, 0.8, 1.0, 1.0]
      enabled: true
```

未来加载时，runtime 先查 `kind`，再把 `payload` 交给对应 factory。这样 `SceneNodeDocument` 不需要为每种新节点硬加一组专属字段。

## 兼容现有操作

| 操作 | registry 提供什么 |
|---|---|
| create | `displayName`、默认 payload、factory |
| select | `boundsPolicy` |
| move / rotate / scale | 复用 `SceneNode` transform |
| rename | 复用 path/name 规则 |
| duplicate | `duplicatePolicy` |
| save / load | `documentPayload` schema |
| debug draw | `debugDraw` |
| API summary | `kind` 与 payload 摘要 |

## 当前能提前练习什么

| 当前练习 | 对未来能力的帮助 |
|---|---|
| 创建 primitive 节点并保存 | 理解 document 到 runtime 的 round-trip |
| 复制和重命名节点 | 理解 path、payload、duplicate 的边界 |
| 观察选中 AABB | 理解 debug helper 与 picking |
| 阅读 `scene_runtime.cpp` | 理解 runtime factory 的位置 |

## 我们已经学会了什么

我们把未来 custom node kind 拆成 metadata、payload、factory、debug draw、bounds 和 duplicate policy。每个角色都服务于现有 editor 操作的兼容性。

## 下一步

回到 [Tutorial 总览](../index.md)，按需要复习任一系列，或进入 [Scene 子系统](../../subsystems/scene.md) 阅读当前实现细节。

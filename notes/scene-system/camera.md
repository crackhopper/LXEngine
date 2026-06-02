# 相机：挂在节点上的观察组件

相机像舞台前的摄影机。它不是独立漂在 scene 外面的对象，而是挂在某个 `SceneNode` 上的 `CameraComponent`：节点的 `transform` 提供位置和朝向，组件只提供投影参数、render target 和 culling mask。

## 相机分工

| 部分 | 当前职责 |
|---|---|
| owner `SceneNode` | transform、runtime path、可见层语义里的节点身份 |
| `CameraComponent` | Perspective/Orthographic 参数、`CameraData`、render target、culling mask |
| `Scene` | 持有 camera node 列表 |
| render queue | 按 target 和 culling mask 选择 camera scene-level resource |

场景文件里的 `camera` 字段只负责序列化投影和可见性参数。相机的位置、朝向和缩放全部来自同一层 node `transform`，这样 mesh、light、camera 的位姿来源保持一致。

## YAML 到组件

```yaml
transform:                         # -> SceneNode local transform
  translation: [0.0, 2.0, 6.0]     # -> CameraComponent::getEyePosition()
  rotation: [0.986394, -0.164399, 0.0, 0.0]
  scale: [1.0, 1.0, 1.0]
camera:                            # -> CameraComponent projection state
  type: perspective
  fovY: 45.0
  aspect: 1.777778
  nearPlane: 0.1
  farPlane: 1000.0
  focusDistance: 6.324555          # -> CameraComponent look target distance
  cullingMask: 2147483647          # -> CameraComponent culling mask
```

透视相机不在 YAML 中保存 `left`、`right`、`bottom`、`top`。这四个 frustum 边界可以由 `fovY`、`aspect` 和 `nearPlane` 推导出来。正交相机保存 `orthographicHeight`，运行时再由 `orthographicHeight` 和 `aspect` 推导左右上下边界。

旧场景里如果仍有 `camera.eye`、`camera.target`、`camera.up`，scene loader 会把它们迁移到 node transform 和 `focusDistance`。保存时会写出新的 canonical 格式。

## 继续阅读

- [文档到 Runtime](document-runtime-flow.md)
- [Node 节点](node.md)
- [一次点击怎样穿过像素、NDC 和 Vulkan](../concepts/camera/pixel-ndc-vulkan.md)

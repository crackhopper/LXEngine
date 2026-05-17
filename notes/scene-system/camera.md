# 相机：挂在节点上的观察组件

相机像舞台前的摄影机。它不是独立漂在 scene 外面的对象，而是挂在某个 `SceneNode` 上的 `CameraComponent`：节点提供位置和朝向，组件提供投影参数、target 和 culling mask。

## 相机分工

| 部分 | 当前职责 |
|---|---|
| owner `SceneNode` | transform、runtime path、可见层语义里的节点身份 |
| `CameraComponent` | Perspective/Orthographic 参数、`CameraData`、target、culling mask |
| `Scene` | 持有 camera node 列表 |
| render queue | 按 target 和 culling mask 选择 camera scene-level resource |

场景文件里的 `camera` 字段只负责序列化参数。运行时含义由 `CameraComponent` 承担。

## YAML 到组件

```yaml
camera:                       # -> CameraComponent
  eye: [0.0, 2.0, 6.0]        # -> lookAt 输入，也会同步节点 transform
  target: [0.0, 0.0, 0.0]
  up: [0.0, 1.0, 0.0]
  type: perspective           # -> projection type
  fovY: 45.0
  aspect: 1.777778
  nearPlane: 0.1
  farPlane: 1000.0
  cullingMask: 2147483647     # -> CameraComponent culling mask
```

## 继续阅读

- [文档到 Runtime](document-runtime-flow.md)
- [Node 节点](node.md)
- [一次点击怎样穿过像素、NDC 和 Vulkan](../concepts/camera/pixel-ndc-vulkan.md)

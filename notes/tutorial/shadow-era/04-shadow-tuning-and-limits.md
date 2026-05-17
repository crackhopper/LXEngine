# 调节阴影时先看哪些边界

调阴影像调舞台灯：我们能先改灯光强度、照射距离和分段数量，但不能把整套灯具系统临时变成插件市场。当前教程只使用 v0.1.1 已经落地的入口，把更大的扩展 API 留在 pending requirement。

## 当前可调入口

Directional light 的 shadow 参数有三条作者表面：scene YAML、CommandBus、Inspector。它们最终都调用同一组 `DirectionalLight` setter。

| 参数 | YAML 字段 | Command field | 当前作用 |
|---|---|---|---|
| shadow strength | `light.shadowStrength` | `light.shadowStrength` | 控制 forward shader 中阴影混合强度 |
| shadow distance | `light.shadowDistance` | `light.shadowDistance` | 控制 CSM 覆盖的最远相机深度 |
| cascade count | `light.shadowCascadeCount` | `light.shadowCascadeCount` | 控制 active cascade 数量，范围 1–4 |

Command 例子：

```text
set /dir_light.shadowStrength 0.8
set /dir_light.shadowDistance 120
set /dir_light.shadowCascadeCount 4
```

Inspector 中选择 directional light 节点后，可以在 Light 区域调整 `Shadow Strength`、`Shadow Distance` 和 `Shadow Cascades`。保存 scene 后，这些字段会回写到 `.scene.yaml`。

## 当前还不是扩展 API

| 能力 | 状态 | Requirement |
|---|---|---|
| light kind registry | pending | [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) |
| command / toolbar extension registry | pending | [REQ-042-b](../../requirements/pending/042-b-tutorial-editor-extension-registry.md) |
| custom scene node registry | pending | [REQ-042-c](../../requirements/pending/042-c-tutorial-custom-scene-node-registry.md) |
| Web Editor | pending | [REQ-044-a](../../requirements/pending/044-a-web-editor-ipc-and-shell.md) |
| Engine CLI / MCP | pending | [REQ-044-b](../../requirements/pending/044-b-engine-cli-mcp-agent-entry.md) |
| AssetRegistry / hot reload | pending | [REQ-044-c](../../requirements/pending/044-c-editor-asset-registry-and-hot-reload-bridge.md) |

## 我们已经学会了什么

我们知道 shadow tuning 当前是 directional light 的一组明确字段，不是独立插件系统。教程场景可以通过 YAML、command 和 Inspector 调整这些字段；更大的注册表、Web shell、CLI/MCP 和 hot reload 仍然保持 pending。

## 下一步

继续阅读 [架构总览](../../concepts-design/architecture.md)，把 shadow-era 教程放回 core / infra / backend 的整体分工里。

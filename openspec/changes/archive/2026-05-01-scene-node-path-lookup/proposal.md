## Why

`SceneNode` 现在有 parent/child 层级，但 `Scene` 还没有稳定的“路径 → 节点”查询能力。后续命令总线和 ImGui scene tree 都需要一个人类可读、跨帧稳定、可脚本化的节点寻址方式，因此要先把 path lookup 基础设施立起来。

## What Changes

- 为 `SceneNode` 增加节点名、路径字符串生成和基本命名约束。
- 为 `Scene` 增加 `findByPath(...)`，支持 `/world/player/arm` 这类从 scene root 开始的严格路径查询。
- 为 `Scene` 增加 `dumpTree()` 文本树导出，给后续 `list nodes` / scene tree 调试使用。
- 定义重名节点的确定性规则：允许重名、按插入顺序首匹配、发现重名时发 `WARN`。
- 保持 parent/child 存储结构不变，不引入 wildcard、GUID、额外索引表或 path cache。

## Capabilities

### New Capabilities

- `scene-node-path-lookup`: `SceneNode` 命名、路径生成、`Scene::findByPath(...)`、`Scene::dumpTree()` 与重名查询规则

### Modified Capabilities

- None.

## Impact

- Affected code:
  - `src/core/scene/object.{hpp,cpp}`
  - `src/core/scene/scene.{hpp,cpp}`
  - `src/test/integration/test_scene_path_lookup.cpp`（新）
  - `notes/source_analysis/src/core/scene/scene.md` 或等效说明文档
- API impact:
  - `SceneNode` 增加 `setName/getName/getPath`
  - `Scene` 增加 `findByPath` 和 `dumpTree`
- No new external dependencies.

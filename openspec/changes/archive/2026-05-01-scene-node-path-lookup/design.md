## Context

场景层已经有可工作的 `SceneNode` parent/child hierarchy，但缺少把“用户输入的节点标识”解析到具体节点的统一入口。REQ-040 命令总线和 REQ-041 编辑器 scene tree 都需要这层能力，而且更偏向人类/agent 可读性，而不是渲染路径的高性能索引。因此实现上应优先选择简单、确定性强、易调试的路径系统。

## Goals / Non-Goals

**Goals:**
- 给每个 `SceneNode` 提供稳定的名字和从 root 到自身的路径字符串。
- 提供 `Scene::findByPath(...)`，支持绝对路径和“相对 root”的简写路径。
- 提供 `Scene::dumpTree()` 文本导出，为命令行和调试 UI 服务。
- 在不改变现有 hierarchy 结构的前提下，对同名节点定义确定性查找行为和告警策略。

**Non-Goals:**
- 不引入 wildcard、regex、`.` / `..`、`[index]` 等扩展路径语法。
- 不引入 GUID、name interning、path cache 或全局索引加速结构。
- 不让 name 参与 pipeline identity、render graph、资源系统契约。

## Decisions

### 1. 路径系统采用文件系统风格的绝对字符串

`/world/player/arm` 对用户、脚本、agent 都最直观，也便于和 `dumpTree()` 输出互相映射。相较于句柄 ID 或 GUID，这种格式更适合当前 Phase 1.5 的编辑器和命令交互。

备选方案：
- 仅用节点名查找。放弃，原因是 hierarchy 下重名不可避免。
- 直接用 GUID。放弃，原因是超出当前阶段目标。

### 2. 不做 path cache，每次按 parent 链现算

`getPath()` 频率相对低，`setParent()` 后又天然会改变路径。现算实现简单、语义稳定，不需要维护失效协议。编辑器节点规模下，这个成本可接受。

备选方案：
- 缓存 path 并在 hierarchy 变化时级联失效。放弃，原因是复杂度高、收益小。

### 3. `findByPath()` 对重名节点首匹配 + WARN

REQ-036 明确不强制唯一性，因此路径解析必须有确定性。按 child 插入顺序选第一个，能给出稳定结果；同时在同 parent 下检测到重名时发 `WARN`，把歧义暴露给调用方和调试日志。

备选方案：
- 发现重名直接拒绝插入。放弃，原因是和 REQ 约束冲突。
- 重名时返回多个节点。放弃，原因是会把复杂度推给所有调用方。

### 4. 非法名字不报 fatal，按 REQ 走 `assert` + `_` 替换

名字主要是命令/UI 表层数据，不值得用 fatal 中断全局流程。保留 `assert` 帮开发期尽早发现，同时把 `/` 等非法字符替换成 `_`，让路径系统保持可用。

## Risks / Trade-offs

- [同名节点导致路径歧义] → 固定首匹配规则并输出 `WARN`
- [大场景下路径查找是 O(depth × siblings)] → 当前编辑器场景规模可接受；若未来需要再引入索引结构
- [空名祖先让路径可读性变差] → `getPath()` 用 `<unnamed-node-0xADDR>` 占位，仅用于调试；后续编辑器可强制赋名
- [`dumpTree()` 格式被后续命令依赖] → 让其与 `findByPath()` 互逆，减少后续格式漂移

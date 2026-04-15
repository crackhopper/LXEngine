# REQ-023: 通用 Forward Shader 的 Variant 合约

## 背景

在 REQ-021 中，我们已经确定：

- push constant 只保留真正的 per-draw 数据
- `enableLighting` / `enableSkinning` 这类静态开关应迁移为 shader variants
- `SceneNode` 必须基于 shader 反射结果校验 mesh/material/skeleton 是否匹配

但随着讨论推进，`blinnphong_0` 的目标已经不再只是“把两个布尔开关从 push constant 挪走”，而是要被重定义为一个**更通用的 forward shader**。这会引入一组新的、彼此有关联的 variant 规则：

- base color 来源可以是常量、vertex color、UV 纹理采样，或它们的乘积
- lighting / normal map / skinning 之间存在逻辑依赖
- 不同 variant 组合会要求不同的 vertex inputs / descriptors

把这些规则继续塞在 REQ-021 里，会让那个文档同时承担：

1. push constant 收敛
2. 高层 `IRenderable` / `SceneNode` 重构
3. renderable 合法性校验框架
4. 通用 forward shader 设计

这四件事耦合太高。为保持边界清晰，本需求将“通用 forward shader 的 variant 合约”独立出来。

## 目标

1. 把 `blinnphong_0` 定义为一个通用 forward shader，而不是专用硬编码 shader
2. 明确短期支持的 variants、它们的逻辑约束和输入契约
3. 明确哪些约束在 loader/template 层校验，哪些在 `SceneNode` 层校验
4. 为后续 material loader、shader 代码和 `SceneNode` 合法性校验提供单独的需求依据

## 依赖

- **REQ-021**（必需）：提供 push constant 收敛、`SceneNode` 抽象、variant 进入 pipeline identity、SceneNode 级合法性校验框架
- **REQ-022**（相关）：`MaterialTemplate` / `MaterialInstance` 的 pass 体系会决定这些 variants 在不同 pass 下如何装配，但 REQ-023 不直接定义 pass enable/disable 机制

## 设计判断

### D1: `blinnphong_0` 重定义为通用 forward shader

本期不再把 `blinnphong_0` 看作“一个固定写死输入的 Blinn-Phong shader”，而是把它定义为一个通用 forward shader。

短期支持的 variants：

- `USE_VERTEX_COLOR`
- `USE_UV`
- `USE_LIGHTING`
- `USE_NORMAL_MAP`
- `USE_SKINNING`

### D2: base color 来源规则

基础颜色来源规则确定为：

- `USE_VERTEX_COLOR=1` 时，基础颜色包含 `vertexColor`
- `USE_UV=1` 时，基础颜色包含纹理采样结果
- 两者都开时，基础颜色为 `baseColor * vertexColor * texture`
- 两者都关时，基础颜色仅来自材质常量 `baseColor`

这里的 `baseColor` 常量始终保留，作为默认乘子和 fallback。

### D3: 短期直接写死 variant 逻辑约束

短期不做通用 variant 规则引擎，而是直接把当前 forward shader 支持的约束写死。

逻辑约束如下：

- `USE_NORMAL_MAP => USE_LIGHTING`
- `USE_NORMAL_MAP => USE_UV`
- `USE_SKINNING => USE_LIGHTING`
- `USE_VERTEX_COLOR` 与 `USE_UV` 可以同时开启
- `USE_LIGHTING` 不强制要求 `USE_VERTEX_COLOR` 或 `USE_UV`

### D4: 约束分两层校验

校验分两层：

1. loader / `MaterialTemplate` 层  
检查 variant 组合本身是否自洽。

2. `SceneNode` 层  
检查该 variant 组合对实际 mesh/skeleton/material resource 的要求是否满足。

职责边界：

- loader/template：发现逻辑约束错误时直接失败
- `SceneNode`：发现资源不匹配时直接失败

## 需求

### R1: loader 必须声明并编译固定 variant 集合

`loadBlinnPhongMaterial(...)` 或其后继 loader 必须能够声明并编译以下 variants：

- `USE_VERTEX_COLOR`
- `USE_UV`
- `USE_LIGHTING`
- `USE_NORMAL_MAP`
- `USE_SKINNING`

这些 variants 必须同时进入：

- shader 编译输入
- `RenderPassEntry::shaderSet.variants`

### R2: loader 层必须校验 variant 逻辑约束

若构造出的 variant 组合违反以下任一规则，统一采用 `FATAL + terminate`：

- `USE_NORMAL_MAP => USE_LIGHTING`
- `USE_NORMAL_MAP => USE_UV`
- `USE_SKINNING => USE_LIGHTING`

### R3: `SceneNode` 层必须校验资源约束

`SceneNode` 在创建完成或结构性成员变化后，必须根据当前 shader variant 组合校验资源是否匹配。

至少包括：

- `USE_VERTEX_COLOR => mesh` 必须提供 `inColor`
- `USE_UV => mesh` 必须提供 `inUV`
- `USE_LIGHTING => mesh` 必须提供 `inNormal`
- `USE_NORMAL_MAP => mesh` 必须提供 `inTangent + inUV`
- `USE_SKINNING => mesh` 必须提供 `inBoneIDs + inBoneWeights` 且节点必须提供 `Skeleton/Bones`

### R4: shader 输入契约必须按 variant 严格收缩

`blinnphong_0` 至少满足以下行为：

- `USE_VERTEX_COLOR` 关闭时：
  - shader 不要求 `inColor`
- `USE_VERTEX_COLOR` 打开时：
  - shader 要求 `inColor`
- `USE_UV` 关闭时：
  - shader 不要求 `inUV`
- `USE_UV` 打开时：
  - shader 要求 `inUV`
- `USE_LIGHTING` 关闭时：
  - shader 不要求 `inNormal`
  - fragment shader 不执行 Blinn-Phong 光照计算
  - 输出为 `baseColor` 乘以当前启用的数据源（常量 / vertexColor / texture）
  - 不保留 ambient 项
  - 不访问 light/camera 相关光照计算路径
- `USE_LIGHTING` 打开时：
  - shader 要求 `inNormal`
- `USE_NORMAL_MAP` 关闭时：
  - shader 不要求 `inTangent`
- `USE_NORMAL_MAP` 打开时：
  - shader 要求 `inTangent + inUV`
- `USE_SKINNING` 关闭时：
  - shader 不声明骨骼输入 attribute
  - 不声明 `Bones` UBO
  - 不执行 skinning 计算
- `USE_SKINNING` 打开时：
  - shader 声明骨骼输入 attribute
  - 声明 `Bones` UBO
  - 执行 skinning 计算

### R5: 测试覆盖

至少补齐以下覆盖：

- 非法 variant 组合（如 `USE_NORMAL_MAP=1` 且 `USE_LIGHTING=0`）→ fatal
- `USE_VERTEX_COLOR` 打开但 mesh 缺 `inColor` → fatal
- `USE_UV` 打开但 mesh 缺 `inUV` → fatal
- `USE_LIGHTING` 打开但 mesh 缺 `inNormal` → fatal
- `USE_NORMAL_MAP` 打开但 mesh 缺 `inTangent + inUV` → fatal
- `USE_SKINNING` 打开但 mesh 缺 `inBoneIDs + inBoneWeights` → fatal
- `USE_SKINNING` 打开但未提供 `Skeleton/Bones` → fatal

## 非目标

- 本期不做通用 variant 规则引擎
- 本期不做多个通用 forward shader family 的抽象
- 本期不设计完整材质编辑器 UI

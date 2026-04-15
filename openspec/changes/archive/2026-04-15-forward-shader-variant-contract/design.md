## Context

REQ-021 已把静态 shader 开关从 push constant 中剥离，并把结构性校验责任前移到 `SceneNode`。REQ-023 进一步明确：`blinnphong_0` 不应继续被当成一个固定输入的专用 shader，而应作为一个由固定 variant 集驱动的通用 forward shader family。

当前风险不在“是否支持 variants”，而在“variants 缺少统一契约”：

- loader 可以生成逻辑上冲突的组合，但没有单一位置负责拦截；
- shader 代码可能在 feature 关闭时仍保留输入或资源声明，导致反射契约与真实期望不一致；
- `SceneNode` 虽然已经承担结构性校验职责，但还缺少针对 forward variants 的细化资源约束表；
- 如果把这些规则放回 REQ-021，会再次把 push constant、对象建模、合法性校验和 forward shader 设计耦合在一起。

因此本变更单独定义 forward shader variant contract，并把职责切分为三层：shader 决定声明什么，loader 决定哪些组合合法，`SceneNode` 决定当前资源是否满足该组合。

## Goals / Non-Goals

**Goals:**

- 把 `blinnphong_0` 定义为一个稳定的通用 forward shader family，而不是临时硬编码 shader。
- 固化短期支持的五个 variants 及其逻辑依赖关系。
- 明确 `baseColor`、vertex color、纹理采样在不同 variants 下的组合规则。
- 要求 vertex inputs、`Bones` UBO 和光照路径按 variant 严格收缩。
- 把逻辑非法组合和资源不匹配统一收敛到 `FATAL + terminate`，并优先通过非 GPU 测试验证。

**Non-Goals:**

- 本期不引入通用 variant 规则引擎或数据驱动约束语言。
- 本期不抽象多个 forward shader family，也不重命名 `blinnphong_0` 文件。
- 本期不设计材质编辑器 UI 或运行时动态切换 variants 的用户工作流。
- 本期不把资源校验下沉到 Vulkan backend；backend 继续只消费已验证结果。

## Decisions

### Decision: 保留固定 variant 集，而不是引入通用规则系统

本期直接把 `USE_VERTEX_COLOR`、`USE_UV`、`USE_LIGHTING`、`USE_NORMAL_MAP`、`USE_SKINNING` 作为 `blinnphong_0` 支持的固定 variant 集。逻辑约束也直接写死在 loader 中：

- `USE_NORMAL_MAP => USE_LIGHTING`
- `USE_NORMAL_MAP => USE_UV`
- `USE_SKINNING => USE_LIGHTING`

这样做的原因：

- 当前需求只覆盖一个 forward shader family，规则数量少且稳定；
- 直接写死可以让 fatal 诊断和测试覆盖更直观；
- 过早抽象通用规则引擎会扩大实现面，并引入额外配置格式。

备选方案：

- 引入通用 variant dependency graph 或规则 DSL。
  该方案对当前单一 shader family 明显过度设计，拒绝采用。

### Decision: loader 只校验“组合是否合法”，不校验场景资源是否满足

`loadBlinnPhongMaterial(...)` 及其后继 loader 负责声明 variant 集、传入 shader 编译输入，并持久化到 `RenderPassEntry::shaderSet.variants`。同一层也负责校验组合本身是否自洽，例如 normal map 依赖 lighting 和 UV。

这样做的原因：

- 组合是否合法只依赖 material/template 自身配置，不依赖 mesh 或 skeleton；
- loader 是唯一同时看到“声明的 variants”和“编译输入”的位置，最适合保证两者一致；
- 把资源校验放在 loader 会错误耦合场景装配时机。

备选方案：

- 让 `SceneNode` 同时负责组合合法性和资源合法性。
  这会让同一个非法组合在更晚阶段才失败，也削弱 loader 对编译输入一致性的责任，不采用。

### Decision: `SceneNode` 负责 variant-to-resource 约束表

`SceneNode` 在已有结构性校验链上扩展 forward shader 资源约束：

- `USE_VERTEX_COLOR` 需要 mesh 提供 `inColor`
- `USE_UV` 需要 mesh 提供 `inUV`
- `USE_LIGHTING` 需要 mesh 提供 `inNormal`
- `USE_NORMAL_MAP` 需要 mesh 提供 `inTangent + inUV`
- `USE_SKINNING` 需要 mesh 提供 `inBoneIDs + inBoneWeights`，且节点提供 `Skeleton/Bones`

这样做的原因：

- 这些约束依赖具体 mesh vertex layout 和节点是否挂接 skeleton，只能在场景装配层判断；
- `SceneNode` 已经是结构性 fatal 校验中心，继续扩展可保持单一失败入口；
- queue/backend 不应再承担首次发现资源不匹配的职责。

备选方案：

- 把这些约束拆散到 mesh loader、skeleton loader、backend pipeline 创建时分别发现。
  该方案会让错误暴露时机分散且更晚，不采用。

### Decision: shader 通过条件声明来收缩输入契约

`blinnphong_0.vert/.frag` 按 variant 直接裁剪声明和执行路径，而不是只在函数体内绕开使用：

- feature 关闭时，不声明对应 vertex input；
- `USE_SKINNING` 关闭时，不声明骨骼 attributes 和 `Bones` UBO；
- `USE_LIGHTING` 关闭时，不执行 Blinn-Phong 光照，也不走 light/camera 相关路径；
- `USE_NORMAL_MAP` 关闭时，不声明 tangent 相关输入。

这样做的原因：

- 只有从 GLSL 声明层裁剪，反射结果才会精确对应当前 compiled variant；
- 这能让 `ShaderReflector` 成为 SceneNode 校验的可靠输入，而不是额外维护一份手写契约表；
- 严格收缩还能减少误用未绑定资源的风险。

备选方案：

- 保留完整 shader interface，只在运行时代码路径里分支跳过。
  该方案会让反射契约虚高，破坏“缺少资源时应 fatal”的前提，不采用。

### Decision: `baseColor` 常量始终保留为统一乘子和 fallback

无论是否启用 vertex color 或 UV 纹理，材质常量 `baseColor` 都始终存在。最终基础颜色按如下规则组成：

- 无 `USE_VERTEX_COLOR`、无 `USE_UV`：`baseColor`
- 仅 `USE_VERTEX_COLOR`：`baseColor * vertexColor`
- 仅 `USE_UV`：`baseColor * texture`
- 两者都开：`baseColor * vertexColor * texture`

当 `USE_LIGHTING` 关闭时，fragment 输出就是该基础颜色结果，不再附加 ambient 或进入任何 Blinn-Phong 光照路径。

这样做的原因：

- 保留统一 `baseColor` 常量可以稳定 UBO 结构和默认材质行为；
- 乘法组合让 loader 和材质默认值更简单，也符合当前 forward shader 的短期需求。

备选方案：

- 在启用 vertex color 或 texture 时去掉 `baseColor` 常量。
  这会扩大 UBO/材质分叉，并削弱默认值和 fallback 语义，不采用。

## Risks / Trade-offs

- [Risk] shader 条件声明如果写错，反射结果和运行时预期会再次脱节。 → Mitigation: 增加不依赖 GPU 的 shader 编译/反射测试，覆盖 UV、normal map、skinning 等组合。
- [Risk] `FATAL + terminate` 让非法组合测试需要死亡测试或子进程方式。 → Mitigation: 把绝大多数覆盖放在 loader 和 `SceneNode` 的 core/infra 测试，避免 Vulkan 路径。
- [Risk] `baseColor` 规则和旧版 `blinnphong_0` 行为不一致时，现有示例资源可能出现视觉变化。 → Mitigation: 在 proposal/spec 中把组合规则写死，并在 loader 默认值测试中固定期望。
- [Risk] 未来如果新增更多 variants，手写约束表会继续增长。 → Mitigation: 本期先把规则集中到单处实现，后续若出现第二个 shader family 再评估抽象。

## Migration Plan

1. 先在 OpenSpec 中固定 shader family、variant 逻辑约束和职责边界。
2. 调整 loader，使声明的 variants 同时进入编译输入和 `RenderPassEntry::shaderSet.variants`，并对非法组合执行 fatal。
3. 调整 `blinnphong_0` GLSL，使 vertex inputs、`Bones` UBO 和 fragment 路径按 variant 收缩。
4. 扩展 `SceneNode` 资源校验和相关 diagnostics，覆盖 mesh/skeleton 不匹配场景。
5. 用 shader 编译/反射测试、loader 测试和 `SceneNode` 死亡测试验证，不把主覆盖放到 Vulkan backend。

回滚策略：

- 若 shader 收缩实现中途不稳定，可先保留现有 runtime 分支，但不得跳过 loader 逻辑约束校验和 `SceneNode` 资源 fatal 校验。

## Open Questions

- 当前 `MaterialUBO` 中与 lighting 相关但在 `USE_LIGHTING=0` 时不再使用的成员，是否保持布局不变以减少材质迁移成本。
- `SceneNode` fatal 诊断里是否需要输出规范化的“缺失 attribute/resource 列表”，以便测试稳定匹配错误文本。

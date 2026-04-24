# 06 · LX Engine 演进路径

> 总结性一章：LX Engine 当前的 bindless 相关状态、是否要做、若做则大致怎么分步走、风险与触发条件。

## 6.1 当前状态（2026-04）

LX Engine **目前不采用 bindless**。渲染后端仍是传统的 descriptor set 绑定模型：

- 每个材质一套 descriptor set（通过现有 material system 管理）
- Pipeline key 包含 material signature，material 不同 → pipeline 不同
- 贴图通过 `sampler2D` 按 binding slot 绑定，shader 里是编译期符号

这个状态和 Godot 的 Forward+ / KlayGE 类似，**适合当前阶段**（核心在把基础架构打扎实），不是要立刻改动的事情。

### 但我们已经做对了一些事

仓库里有几个 spec 为将来的 bindless 改造留了余地：

- `openspec/specs/pipeline-key/spec.md` —— `PipelineKey::build(objSig, matSig)` 已经把 pipeline 身份结构化
- `openspec/specs/pipeline-build-desc/spec.md` —— pipeline 构造输入聚合成独立对象
- `openspec/specs/pipeline-signature/spec.md` —— signature 机制让资源可以声明参与哪些 pass
- `openspec/specs/pipeline-cache/spec.md` —— `find` / `getOrCreate` / `preload` 已是缓存模型

这些都是演进到 bindless 时有用的基础设施。

## 6.2 我们要不要做？—— 结论：暂不做，但预留接口

### 不做的理由

1. **当前阶段主任务不在此**：roadmap 上 Phase 1 的渲染深度（PBR/shadow/IBL/HDR）还没完成，bindless 改造应该让位给功能性缺口
2. **需求没到**：当前场景物体数量有限，CPU 绑定不是瓶颈
3. **Shader 系统未成熟**：material DSL / shader graph 还没立，强推 bindless 会让用户（或 AI agent）直接写 `nonuniformEXT` 的 GLSL —— 这是反 AI-Native 设计的
4. **移动端目标未定**：如果将来要上 WASM / WebGPU，bindless 不可用；上移动端，Android 只有 1% 支持率。过早 commit 会自绑手脚

### 预留接口的理由

1. 现在偷懒写"每材质一 set"的代码很容易，以后全量重写就很贵
2. Godot 的经验告诉我们：**shader 层抽象若早做，后端切 bindless 几乎无感**；晚做，用户 shader 全部要重写

## 6.3 如果要实施，分步路径（概括性）

按"每一步都可独立落地、都有短期收益"的原则排序。

### 阶段 0 · 预备（与 Phase 1/2 并行，无重构）

- **material DSL 化**：用户不直接写 `sampler2D`，而是通过 material/shader graph 声明"我需要 diffuse 贴图"
- **shader 编译器层抽象**：`.lxshader` → 后端 GLSL，编译时决定绑定形式
- 仍生成传统 sampler2D shader。**一行 bindless 代码都没写**，但为未来改造打好接口

预期产出：shader 的"用户语义"和"后端绑定"解耦。

### 阶段 1 · Bindless 纹理（最小侵入）

- 启用 `VK_EXT_descriptor_indexing` 并做硬件检测，缺失则回退
- 建立全局 bindless descriptor set（长度 4096 起步，不用追求 65536）
- `TextureHandle` 带 slot id；加载/删除走"帧起始 flush + frames-in-flight 延迟回收"策略
- Shader 编译器识别 "这个 sampler 是材质资源" → 生成 bindless 路径；保留传统路径作为 fallback

预期产出：descriptor set layout 全局收敛 1 个，pipeline layout 全局收敛。`vkCmdBindDescriptorSets` 频率从每材质一次降到每帧一次。

### 阶段 2 · Bindless buffer（可选）

- Vertex / index / instance data 也放进 SSBO，走 vertex pulling
- 配合 `vkCmdDrawIndirect` 做 GPU-driven 剔除（对接 Phase 1 的 shadow / culling）

预期产出：Pipeline 里不再有 VertexInputState 差异，PSO 进一步收敛。

### 阶段 3 · Ubershader（性能需求驱动，不一定做）

- 当 PSO 数量真正成问题（比如到 5k+）时，把材质特性从 permutation 改成 uniform-branch ubershader
- 少数核心变体用 specialization constants

预期产出：PSO 从万级降到百级。**这一步只有当 PSO stutter 实际出现时才值得做**，不要为做而做。

## 6.4 需要同步改的周边

| 子系统 | 变动 |
|-------|------|
| `ResourceManager` / `TextureLoader` | 加 slot 分配 + 延迟回收队列 |
| `PipelineKey` | 移除 descriptor-set-layout 维度；加 pass / blend / vertex format 维度 |
| `PipelineBuildDesc` | 统一 pipeline layout 为全局引用 |
| `MaterialInstance` | 材质参数走 SSBO，shader 接口改成 index + offset |
| Shader 编译器 | 自动注入 `#extension` + `nonuniformEXT()` |
| Frame 调度 | 帧起始 hook：reclaim + flush updates |

## 6.5 风险与兼容性

| 风险 | 影响 | 应对 |
|-----|------|-----|
| **移动端没 descriptor indexing** | Android 渲染路径断裂 | 保留传统路径作为 fallback；接受"移动端仅低端特性" |
| **WebGPU 暂无 bindless** | 浏览器路径受限 | Phase 1/2 暂不触 WebGPU；真需要时走另一条 shader 编译路径 |
| **Shader 编译复杂度** | 编译器要管 uniform 性推断，容易漏 `nonuniformEXT` | 默认保守加，仅对明确的 uniform 源（push constant）省略 |
| **Driver 行为差异** | uniform branching 的性能表现跨厂商不一致 | 持续 profile，把关键分支降级为 specialization constant |
| **Validation 层检查不全** | 应用层的 slot 误更新可能在 debug 时漏检 | 建立 slot-lifecycle 的单元测试 |

## 6.6 触发条件（什么时候应该重启这个话题）

出现以下信号时，再把这个 roadmap 拿出来：

1. **场景 instance 数 > 10k**，CPU 绑定成了 profile 热点
2. **PSO 数 > 2k**，首帧/切换场景出现明显 stutter
3. **项目要上 GPU-driven culling / Nanite 式几何**，bindless 成为前置
4. **AI agent 需要生成材质**，用户侧写 shader 的成本成瓶颈（material DSL 是这时候不得不做的事）
5. **SIGGRAPH / GDC 有新引擎开源**，bindless 实现模式出现更优范式

## 6.7 一句话结论

> LX Engine 当前不做 bindless，但从现在开始所有的 shader / material 接口都应该按"**将来能切到 bindless**"的方式设计。最重要的是 **material DSL 和 shader 编译器层抽象**（阶段 0），它让未来的迁移几乎无感；剩下的阶段 1-3 按需激活。

补充阅读：如果要把未来的 shader 编译层、反射层、材质层职责拆清楚，先看 [04A · Shader 反射与 Layout 约束](04a-Shader反射与Layout约束.md)。

# REQ-059-a: Rendering Research Integrator Sandbox

> 2026-06-01 新增：本 REQ 将 Offline Rendering Lab 扩展为论文复现和实时化实验场。当前仍在讨论中，未开始。

## 背景

用户希望离线渲染器不只是产出 ground truth，也能成为实验场：当考虑把某个效果实时化，或复现论文时，先在离线 renderer 中搞清楚算法、参数、AOV 和误差，再决定如何移植到实时管线。

这对应用户优先级中的 C 阶段，排在 ground truth 和 editor 集成之后。Bake asset
generator 已拆入 `notes/requirements/planned/`，本轮不作为 active 前置依赖。

## 目标

1. 让 integrator/pass 可插拔。
2. 支持实验 shader、参数和 AOV 注册。
3. 支持同一场景下多算法对比。
4. 支持把实验结果沉淀为实时 renderer 需求。
5. 作为 Offline Rendering Lab 最后阶段 active REQ，首版只做 registry/profile/AOV/metrics 基础。

## 需求

### R1: Integrator registry

定义 integrator 注册模型。

字段：

| 字段 | 含义 |
|---|---|
| `name` | profile 中引用的稳定名 |
| `backend` | `vulkan-compute` / future `vulkan-rt` |
| `shader` | compute/ray tracing shader 入口 |
| `parameters` | 可配置参数 schema |
| `outputs` | beauty/AOV/bake outputs |
| `requirements` | 需要的 scene/material/texture 能力 |

### R2: Experiment profile

profile 可以选择 integrator 并传参：

```yaml
offlineRender:
  profiles:
    paper_test:
      backend: vulkan-compute
      integrator: my-paper-integrator
      width: 512
      height: 512
      samples: 128
      parameters:
        enableMis: true
        clampFireflies: 20.0
```

### R3: AOV 与 metrics

实验应能输出：

- beauty
- normal/albedo/depth 等常规 AOV
- integrator 自定义 AOV
- 与 reference 的差异指标，例如 MSE/PSNR/SSIM 的预留接口

首版 metrics 可只做 MSE/PSNR。

### R4: 与实时化需求连接

实验完成后应能记录：

- 实验场景
- 参数
- reference 输出
- candidate 输出
- 差异指标
- 是否值得进入 realtime renderer 需求

这可以先是文档/metadata，不要求完整 UI。

### R5: 测试覆盖

覆盖：

- integrator registry 能注册两个不同 integrator。
- profile 能选择 integrator 并传参。
- unsupported parameter 给出诊断。
- 自定义 AOV 可以写出。
- metrics 能比较两张同尺寸 float image。

## 修改范围

- offline integrator registry
- render profile parser
- shader pipeline organization
- output/AOV writer
- tests
- notes/concepts-design/offline-rendering-lab/

## 边界与约束

- 本 REQ 不要求脚本语言。
- 本 REQ 不要求动态编译任意用户 shader。
- 本 REQ 不要求 Web dashboard。
- 本 REQ 不要求 production render farm。
- 本 REQ 不改变前序 MVP 的稳定 CLI 行为。

## 依赖

- `REQ-057-a`
- `REQ-058-a` 可选

## 后续工作

- Vulkan hardware ray tracing backend。
- 实时 renderer pass migration workflow。
- Web/Editor experiment dashboard。

## 实施状态

讨论中，未开始。

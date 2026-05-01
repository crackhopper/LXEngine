# 02 · Specialization Constants

> 阅读前提：[01](01-VRS基础.md) 讲完了 VRS。本文转向 *跟 VRS 相关但独立* 的 Vulkan 工程能力 — specialization constants。

## 2.1 是什么

**编译期常量，但 pipeline 创建时确定值**：

```glsl
// shader 里
layout(constant_id = 0) const uint SUBGROUP_SIZE = 32;
layout(local_size_x = SUBGROUP_SIZE) in;

// CPU 端创建 pipeline 时改值
VkSpecializationInfo info = {...};
info.pMapEntries = ...;
info.pData = &actual_value;   // 比如 64 (AMD) / 32 (NVIDIA)
```

SPIR-V 编一次，多个 pipeline 用不同 spec 值。编译器知道是常量，会做常量传播 / unroll / 死分支消除等优化。

## 2.2 跟其它"常量"机制对比

| 机制 | 何时确定 | 改动需要 | 用途 |
|------|---------|---------|------|
| **预处理 #define** | SPIR-V 编译时 | 重新编译 SPIR-V | 静态变体 |
| **Specialization constant** | 创建 pipeline 时 | 重新创建 pipeline | 静态变体 / 硬件适配 |
| **Push constant** | 每个 draw call | 无 | 高频小数据 |
| **Uniform buffer** | 每帧 | 无 | 大量参数 |
| **Storage buffer** | 每帧 | 无 | 大数据 + 可写 |

定位：**编译期常量但延迟绑定**。比 #define 灵活、比 push constant 高效。

## 2.3 三个典型用例

### 用例 1：硬件适配

```glsl
layout(constant_id = 0) const uint SUBGROUP_SIZE = 32;
layout(local_size_x = SUBGROUP_SIZE) in;
```

NVIDIA `SUBGROUP_SIZE = 32`、AMD `= 64`、Intel `= 16`。同一份 shader 编一次，pipeline 创建时按硬件填值。

跟 async-compute / gpu-driven-rendering 调研里讲的 *warp / wave 大小* 直接对应。

### 用例 2：Shader 变体

```glsl
layout(constant_id = 1) const bool USE_SHADOW = false;

void main() {
    if (USE_SHADOW) { /* 加 shadow 代码 */ }
}
```

CPU 端创建两个 pipeline：USE_SHADOW=false 和 =true。**编译器知道是常量，会消除分支**。比 runtime branch 高效。

跟 LX [REQ-118 PBR 完整管线](../../main-roadmap/phase-1-rendering-depth.md#req-118--pbr-完整管线) 讨论的 shader 变体管理强相关。

### 用例 3：算法参数

```glsl
layout(constant_id = 2) const float EDGE_THRESHOLD = 0.1;
```

Sobel 阈值 0.1 在 shader 里是常量，但可以按 *场景* 调整（亮场景调高、暗场景调低）。

## 2.4 SPIR-V 反射扩展

LX shader system 已经有完整 reflection 路径（参见 source_analysis 里的 shader.md）。增加 specialization constant 是 *扩展* 不是重写：

```cpp
// 新增 SPIR-V 解析路径
case SpvOpDecoration:
    if (decoration == SpvDecorationSpecId) {
        id.binding = data[word_index + 3];   // 拿 constant_id
    }
    break;

case SpvOpSpecConstantTrue:
case SpvOpSpecConstantFalse:
case SpvOpSpecConstant:
case SpvOpSpecConstantOp:
case SpvOpSpecConstantComposite:
    // 保存 spec constant 描述：constant_id / name / type / default_value
    save_spec_constant(...);
    break;
```

存到 shader metadata，pipeline 创建时按名字查找填值。

## 2.5 Pipeline 创建时填值

```cpp
VkSpecializationMapEntry entries[N];
u32 data[N];

// 按 shader 反射出的 spec constants 列表逐个填
for (each spec constant in shader_state) {
    entries[i].constantID = spec.binding;
    entries[i].size       = sizeof(u32);
    entries[i].offset     = i * sizeof(u32);
    
    if (strcmp(name, "SUBGROUP_SIZE") == 0) {
        data[i] = device->subgroup_size;
    }
    // ... 其它名字
}

VkSpecializationInfo specialization_info = {
    .mapEntryCount = N,
    .pMapEntries   = entries,
    .dataSize      = N * sizeof(u32),
    .pData         = data,
};

shader_stage_info.pSpecializationInfo = &specialization_info;
```

## 2.6 Pipeline Cache 一致性

**重要**：specialization values 不同的 pipeline 是 *不同 pipeline*，cache 应该按值区分。

LX 的 `PipelineKey` 当前由 `(objectSig, materialSig)` 组成（REQ-042 后变成三级 compose）。引入 specialization constants 后，可能需要：

- 把 spec values 进 PipelineKey 的某一级 compose
- 或者把 spec values 单独作为 cache 维度（pipeline = (PipelineKey, specValues)）

具体设计待 LX 立项时决定。

## 2.7 跟 LX 现有设计的耦合

| LX 已有 | 加什么 |
|---------|--------|
| `IShader` reflection | + specialization constants 列表 |
| `MaterialTemplate` 编译 | + 接受 specialization values |
| `PipelineKey` 组件 | 可选：specialization values 进 cache key |
| Pipeline cache | 不同 spec values = 不同 cache 项 |

## 2.8 Specialization vs 多 SPIR-V 编译

LX 当前如果用 `#define` 实现变体，相当于 *为每个变体编一份 SPIR-V*。Specialization 让你 *编一份 SPIR-V，多份 pipeline*。

| 维度 | 多 SPIR-V (#define) | Specialization |
|------|---------------------|----------------|
| Build 阶段编译次数 | 多 | 一次 |
| Build 输出 .spv 文件数 | 多 | 一份 |
| Pipeline 创建开销 | 一样 | 一样 |
| Runtime 改值 | 不行（要重 build） | 不行（要重 createPipeline） |
| Shader 编译期优化 | 充分 | 充分（值已知） |

主要差别是 *构建期* 复杂度。变体多时 specialization 显著简化 build pipeline。

## 2.9 接下来读什么

- [03 LX 演进与触发条件](03-LX演进与触发条件.md) — 候选 REQ + 何时立项

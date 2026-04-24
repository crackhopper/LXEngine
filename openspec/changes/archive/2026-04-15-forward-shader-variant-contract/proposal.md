## Why

`blinnphong_0` 已经不再只是一个固定输入的 Blinn-Phong shader。它正在承担通用 forward shader 的职责，但当前 variants、输入契约、资源约束和校验责任仍然分散在 loader、shader 代码和 `SceneNode` 语义之外，容易让非法组合在较晚阶段才暴露。

## What Changes

- 将 `blinnphong_0` 重定义为通用 forward shader family，并明确短期支持的 variants：`USE_VERTEX_COLOR`、`USE_UV`、`USE_LIGHTING`、`USE_NORMAL_MAP`、`USE_SKINNING`。
- 明确 `baseColor` 的来源规则：常量 `baseColor` 始终保留，vertex color 与 texture 采样按 enabled variants 参与乘积组合。
- 要求 shader 输入契约按 variant 严格收缩，关闭的 feature 不再声明对应 vertex inputs 或依赖的 shader 资源。
- 要求 loader 在 material/template 层校验 variant 逻辑约束，非法组合统一 `FATAL + terminate`。
- 要求 `SceneNode` 在资源装配层校验 mesh / skeleton / material 是否满足当前 shader variant 合约，资源不匹配统一 `FATAL + terminate`。
- 补齐以 core/infra 为主的测试覆盖，尽量避免把验证依赖压到 Vulkan/GPU 测试路径。

## Capabilities

### New Capabilities
- `forward-shader-variant-contract`: 定义 `blinnphong_0` 作为通用 forward shader 的 variant 集、`baseColor` 组合规则、逻辑依赖和按 variant 收缩的 shader 输入/资源契约。

### Modified Capabilities
- `material-system`: 增加 forward material loader 对固定 variant 集的声明、编译输入持久化以及非法组合的 fatal 校验要求。
- `scene-node-validation`: 增加 `SceneNode` 基于当前 forward shader variants 对 mesh vertex layout、tangent/UV、以及 skeleton/Bones 资源的细化 fatal 校验要求。
- `shader-reflection`: 明确反射得到的 vertex input contract 必须对应当前 variant 真实启用的 shader 输入集合，禁用的 inputs 不得残留在契约中。

## Impact

- 影响 `assets/shaders/glsl/blinnphong_0.vert`、`assets/shaders/glsl/blinnphong_0.frag`、`src/infra/material_loader/blinn_phong_material_loader.cpp`、`src/infra/shader_compiler/shader_reflector.cpp` 以及 `SceneNode` 结构性校验路径。
- 影响 forward material 的编译和装配方式、mesh vertex layout 校验行为，以及 fatal 失败的诊断覆盖。
- 需要新增或调整以 shader 编译/反射、loader、`SceneNode` 校验为主的非 GPU 测试。

# REQ-023: 通用 Forward Shader 的 Variant 合约

## 状态

经核对，本文原先列出的需求已经在当前代码库中落地，包括：

- `blinnphong_0` 作为通用 forward shader family 的 variant 集合
- loader 对 variant 逻辑约束的校验
- shader 输入契约按 variant 收缩
- `SceneNode` 对 mesh/skeleton 资源约束的校验
- 相关 core/infra 层测试覆盖

因此，**当前没有剩余的 REQ-023 待实现项**。

## 说明

已落地实现可参考：

- [blinnphong_0.vert](/home/lx/proj/renderer-demo/shaders/glsl/blinnphong_0.vert)
- [blinnphong_0.frag](/home/lx/proj/renderer-demo/shaders/glsl/blinnphong_0.frag)
- [blinn_phong_material_loader.cpp](/home/lx/proj/renderer-demo/src/infra/material_loader/blinn_phong_material_loader.cpp)
- [object.cpp](/home/lx/proj/renderer-demo/src/core/scene/object.cpp)
- [test_shader_compiler.cpp](/home/lx/proj/renderer-demo/src/test/integration/test_shader_compiler.cpp)
- [test_blinn_phong_material_loader.cpp](/home/lx/proj/renderer-demo/src/test/integration/test_blinn_phong_material_loader.cpp)
- [test_scene_node_validation.cpp](/home/lx/proj/renderer-demo/src/test/integration/test_scene_node_validation.cpp)

若后续需要扩展新的 forward shader family、引入更通用的 variant 规则引擎、或放宽/重定义当前 variant 合约，应另起新需求。

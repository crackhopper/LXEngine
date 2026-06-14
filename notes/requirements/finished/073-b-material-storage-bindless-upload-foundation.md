# REQ-073-b: Material Storage And Bindless Upload Foundation

> 2026-06-13 拆分并收紧边界：原 `REQ-073-b` 同时包含 material storage、bindless tables、shader variant、indirect batching、RenderPath 术语迁移和 realtime hard cut，范围过大。本文件只保留第一段实现，但这段必须形成 foundation 闭环：`REQ-073-a` 的 source-reflected material contract 要真实进入 `SceneResourceTableUploadView`，并能被 backend/GPU resource table 消费为 bindless-ready texture/sampler/material/object/draw/mesh 数据。后续 shader variant、URI 迁移、indirect batching 和 realtime hard cut 分别由 `REQ-073-c` 到 `REQ-073-f` 承接。

## 背景

`REQ-073-a` 已完成合同层：材质必须声明 `bsdf.source`，source 可以反射出参数 schema、source signature 和 Material Accessor ABI，PBR shader 通过 `LX_MATERIAL_CONTRACT_SOURCE` 引入访问器。

但合同层完成后，realtime 渲染仍需要一个干净的数据基础：

- 每个 material source 需要自己的 source storage；同一个 storage 内的材质实例使用 source-local material index。
- 缺失贴图不能继续靠 invalid texture index + shader 分支兜底。
- texture、sampler、material、object、draw、mesh/geometry 数据必须能以全局 table 形式进入 backend/GPU resource table 的 bindless-ready staging。
- upload view 必须能说明每个 material record 的 factor、texture slot、channel selector、所属 source storage 和 source-local material index。

本 REQ 不只是 CPU-only view。它必须证明 backend 能消费这些 table 并建立稳定 slot/staging 数据；但它不负责 shader variant、不负责 shader URI 迁移、不负责 RenderWorkQueue indirect batching、不负责删除旧 realtime 默认路径。

## 目标

1. 让 `SceneResourceTableUploadView` 导出 source-reflected material records。
2. 为 `white`、`black`、`flatNormal` 默认纹理建立稳定 resource identity 和 table slot。
3. 让 `Kd`、`metallic`、`roughness`、`ao`、`emissive`、`normalmap` 以 factor × texture/channel 形式进入 material record。
4. 建立 bindless-ready 的 texture、sampler、material、object、draw、mesh/geometry table 数据。
5. 让 backend/GPU resource table 可以消费 upload view，建立或更新对应 table/slot/staging 数据。
6. 输出可审计 diagnostics，证明 material record 来自 `bsdf.source` 反射，而不是旧 `MaterialUBO` bytes。

## 非目标

- 不实现 RenderPath material source shader variant 展开；由 `REQ-073-c` 处理。
- 不迁移 `assets/shaders/glsl/techniques/` 到 `render_paths/`；由 `REQ-073-d` 处理。
- 不要求 RenderWorkQueue / geometry pass 默认消费新 table 生成 indirect batch；由 `REQ-073-e` 处理。
- 不删除 realtime 旧 descriptor / per-item fallback / 旧 `SceneGpuMaterialRecord` 默认路径；由 `REQ-073-f` 处理。
- 不处理 OfflineRT 配置入口；由 `REQ-073-g` / `REQ-073-h` 处理。
- 不实现 package、pipeline cache blob 或 BC7 压缩。

## 需求

### R1: Source-local Material Storage View

`SceneResourceTableUploadView` SHALL 按 material source signature 导出 source storage，并在每个 storage 内分配 source-local material index。

要求：

- 每个 storage 记录自己的 source signature、source URI、reflection hash 和 storage ABI hash。
- 同一 source signature 下的 material 使用连续的 source-local material index。
- 不同 source signature 不能共享 material record layout 或 source-local index 空间。
- upload view SHALL 暴露稳定的 `sourceStorageIndex`，它是 source storage table 的行号；`sourceSignature` 只存储在 storage header / diagnostics 中，不在每个 material reference 上重复存储。
- object/draw record 中引用 material 时，必须能追踪到 `{sourceStorageIndex, sourceLocalMaterialIndex}`；本 REQ 的新增 table、diagnostics 和测试不得把旧全局 material index 当作 Material v3 引用真相。旧字段如果仍因旧 renderer 默认路径存在，只能作为旧路径字段保留，不参与本 REQ 的正向验证。
- 如果同一 source signature 反射出不一致 layout，必须报告 engine invariant violation；不得 fallback、拆第二套 layout 或静默选择其中一个。

术语约定：

| 术语 | 含义 |
|---|---|
| source signature | 某个 material source / reflection hash / storage ABI 的结构身份 |
| source storage | 使用同一 source signature 的 material record 表 |
| sourceStorageIndex | source storage table 的行号，用于 object/draw 引用某个 storage |
| sourceLocalMaterialIndex | 某个 source storage 内的 material record 行号 |

示例：

```text
sourceStorageIndex = 0
  sourceSignature = matte-v1
  records:
    sourceLocalMaterialIndex 0 -> red_paint.material
    sourceLocalMaterialIndex 1 -> blue_paint.material

sourceStorageIndex = 1
  sourceSignature = metal-v1
  records:
    sourceLocalMaterialIndex 0 -> chrome.material
```

`sourceSignature` 选择 layout；`sourceLocalMaterialIndex` 选择该 layout 表里的具体材质实例。两者不能互相替代。

### R2: Material Record Contents

material record SHALL 只保存 shader 需要的结构化数据。

最低字段：

| 字段 | 说明 |
|---|---|
| source storage identity | 不写入 material payload；由 storage header 的 `sourceStorageIndex -> sourceSignature` 以及 material ref 的 `{sourceStorageIndex, sourceLocalMaterialIndex}` 表达 |
| factor values | `Kd`、`metallic`、`roughness`、`ao`、`emissive`、`normalScale` 等 |
| texture slots | baseColor、metallic/roughness、AO、emissive、normal 或默认纹理 slot |
| channel selector | packed metallic/roughness/AO 的通道选择 |
| flags | alpha、normal map、double-sided 等后续可扩展位 |
| source-local material index | 当前 material 在本 source storage 内的连续 index |

规则：

- material payload 不保存 material URI 字符串、backend object pointer、source signature 或旧 `MaterialUBO` bytes。
- texture 是否存在、texture id、material URI、material handle 和参数值不能改变 source signature。
- 显式 texture 参数的 slot 解析顺序必须保持干净：优先使用材质实例在 `SceneResourceTable` 注册后持有的 `TextureHandle`，其次使用 parser 记录的 canonical dependency URI 查找已加载 texture；如果二者都不能解析，必须 fail-fast。只有参数缺省时，才使用 source storage field 声明的默认纹理语义。
- source 不支持的参数必须在 parser/contract 阶段失败，upload 阶段不能补隐式语义。
- `SourceLocalMaterialRecord` SHALL 使用 source-reflected bytes payload。C++ 类型只承担 envelope/metadata 职责，例如 local index 和 byte range；不得定义一套固定 PBR record 作为 Material v3 layout 真相，也不得从旧 `SceneGpuMaterialRecord` 反向推导。

### R3: Default Texture Resources

Resource table / upload path SHALL 注册稳定的默认纹理资源。

最低集合：

| 默认纹理 | 用途 | 值 |
|---|---|---|
| `white` | baseColor、metallic、roughness、AO 等乘法贴图 fallback | `(1, 1, 1, 1)` |
| `black` | emissive fallback | `(0, 0, 0, 1)` |
| `flatNormal` | normal map fallback | `(0.5, 0.5, 1.0, 1.0)` |

要求：

- 默认纹理有稳定 resource identity。
- 默认纹理只注册/上传一次。
- `SceneResourceTable` 构造后默认拥有这三张 builtin texture；`buildUploadView()` 即使在没有材质贴图时也会导出它们作为全局默认 table slot。
- 缺失贴图指向默认 texture table slot，不创建材质本地 placeholder。
- backend/GPU resource table 能为默认纹理建立稳定 bindless/table slot。
- shader 后续仍走同一采样路径，不需要 `hasSceneTexture` 分支判断缺失贴图。

### R4: Bindless-ready Tables

upload view SHALL 提供 backend 可直接消费的 table 数据。

最低表：

| 表 | 内容 |
|---|---|
| texture table | imported textures + default textures |
| sampler table | sampler state |
| material storage table | per-source material records |
| material ref table | `sourceStorageIndex` + `sourceLocalMaterialIndex` |
| object table | transform、mesh、visibility；Material v3 引用通过 draw/material ref table 追踪 |
| draw table | object、material ref、mesh/draw offsets；旧 `materialIndex` 只为 legacy path 保留 |
| mesh/geometry table | global position/index/attribute stream ranges |

要求：

- 所有 table entry 只保存 handle、slot、offset、count 和结构签名，不保存 backend object pointer。
- 同一 canonical texture URI 在 texture table 中去重。
- mesh/geometry table 首版可以保留现有 buffer 分组，但必须输出 table export 的 unsupported/skip 原因；batch compatibility split diagnostics 由 `REQ-073-e` 处理。

### R5: Backend/GPU Resource Table Foundation

backend/GPU resource table SHALL 能消费 `SceneResourceTableUploadView` 的 bindless-ready tables。

最低要求：

- imported texture 与 default texture 建立稳定 bindless/table slot。
- sampler state 进入 sampler table 或等价 descriptor staging。
- per-source material records 进入 material storage table 或等价 upload buffer staging。
- object、draw、mesh/geometry table 进入 backend 可追踪的 staging/buffer 描述。
- backend diagnostics 能把每个 slot / staging record 追踪回 resource identity、sourceStorageIndex、source signature 和 source-local material index。

约束：

- 本 REQ 只要求 backend 能建立 table/slot/staging 数据；不要求 realtime renderer 默认 path 已经绑定并消费这些 table。
- 如果 backend 暂时不能上传某类 table，必须输出 unsupported diagnostic 并使对应验证失败，不能静默忽略。
- 不允许用 per-material descriptor 或旧 `MaterialUBO` upload 证明 bindless foundation 成功。

### R6: Diagnostics

upload view / validation profile SHALL 输出可审计 diagnostics：

- material source count。
- 每个 source signature 的 material count 和 storage ABI hash。
- sourceStorageIndex 到 source signature / storage ABI hash 的映射。
- 默认纹理 slot。
- texture table 去重统计。
- object/draw/material index 映射。
- backend table/slot/staging 统计。
- 无法进入 bindless-ready table 的资源和原因。

缺少默认纹理、缺少 source signature、material record layout 不一致、texture slot 无效、source-local material index 无效、backend table upload 不支持时必须 fail-fast。

## 测试

### T1: Source-local Material Records

构造两个同 source material 和一个不同 source material，断言：

- 同 source material 进入同一 storage。
- 同 source material 获得不同 source-local material index。
- 不同 source material 进入不同 storage。
- object/draw 引用 `{sourceStorageIndex, sourceLocalMaterialIndex}`，并能经 storage header 查回 source signature。
- material URI / 参数值不改变 source signature。

### T2: Factor And Texture Record

构造常量材质、贴图材质、packed metallic-roughness 材质，断言：

- factor 值进入 material record。
- 已注册材质 texture handle 和 parser canonical texture URI 都能转为 texture table slot；显式贴图无法解析时 fail-fast，不静默落到默认纹理。
- packed channel selector 被记录。
- 常量-only 和 texture-backed material 使用同一 source storage layout。

### T3: Default Texture Dedup

断言：

- `white`、`black`、`flatNormal` 只注册一次。
- 缺失 baseColor/metallic/roughness/AO 指向 `white`。
- 缺失 emissive 指向 `black`。
- 缺失 normal 指向 `flatNormal`。

### T4: Bindless Table Export

构造含 mesh、material、texture、object 的小场景，断言 upload view 导出 texture/material/object/draw/mesh table，并且 handle/slot/index 可互相追踪。

### T5: Backend Table Consumption

构造含 imported texture、默认纹理、source-local material storage、object、draw、mesh/geometry 的小场景，断言 backend/GPU resource table 能消费 upload view 并建立：

- imported/default texture slots。
- sampler table 或 descriptor staging。
- material storage staging。
- object/draw/mesh/geometry table staging。
- slot/staging 到 resource identity、sourceStorageIndex、source signature 和 source-local material index 的 diagnostics。

### T6: Negative Diagnostics

覆盖：

- 缺默认纹理 slot。
- material 缺 source signature。
- source signature layout 冲突。
- texture dependency 无法解析。
- object 指向无效 source-local material index。
- backend 不支持 required table upload。

## 修改范围

- `src/core/asset/material_contract*`
- `src/core/asset/material_instance.*`
- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_gpu_records.*`
- backend/GPU resource table table/slot/staging foundation
- material loader / glTF loader / PBRT converter 的 material record 输入路径
- scene resource upload view tests
- backend table consumption tests
- material source contract tests

## 边界与约束

- 不写 shader runtime source/type branch。
- 不用旧 `MaterialUBO` bytes 作为 material record 通过条件。
- 不为缺失贴图创建材质本地 placeholder。
- 不在本 REQ 删除旧 draw fallback；这里只让新数据和 backend table/staging 可用且可诊断。

## 依赖

- `REQ-073-a`: Material v3 source contract、source signature 和 accessor ABI。
- `REQ-072`: RenderPathGraph / SceneResourceTable closure audit 基础。

## 后续工作

- `REQ-073-c`: Material source shader variant boundary。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-073-f`: Realtime material path hard cut and smoke。
- `REQ-073-g` / `REQ-073-h`: OfflineRT RenderPathGraph compute path and config hard cut。

## 实施状态

完成。

已完成：

- source contract storage field 反射和 layout 校验。
- supported built-in contract source 的 storage field 声明。
- `SourceLocalMaterialRecord` source-reflected bytes 打包。
- `white`、`black`、`flatNormal` builtin 默认纹理 resource identity 与 upload slot。
- source-contract material 在 upload view 中硬切到 `materialRefs + sourceMaterialRecords`；正向测试不再把 `SceneGpuMaterialRecord` 当作 Material v3 真相。
- source texture slot 解析支持 table-owned parameter `TextureHandle` 和 parser canonical dependency URI；显式贴图缺资源时保持 fail-fast。
- source signature mismatch、同 source signature storage layout 冲突、显式 texture slot 无法解析时会在 upload 阶段抛出可审计 diagnostics。
- backend/GPU resource table shell 可以消费 upload view，建立 texture bindless slots、per-source material storage buffers、object/draw/mesh staging buffers，以及 position/index/primitive/attribute geometry staging buffers；material storage report 保留 sourceStorageIndex、source signature 和 storage ABI hash 追踪信息。
- build/runtime shader source 同步已纳入测试 target 依赖，避免 CTest 从 `build/` runtime root 读取过期 `.contract.glsl`。
- Offline loader 的 Material v2 测试已改为验证 source-contract `materialRefs + sourceMaterialRecords`，不再把旧 `materialIndexByHandle` 当作 Material v3 注册证据。

验证：

- `cmake --build build --target test_material_source_contract test_scene_resource_upload_view_v2 test_bindless_indirect_contract test_scene_resource_table`
- `./build/src/test/test_material_source_contract`
- `./build/src/test/test_scene_resource_upload_view_v2`
- `./build/src/test/test_bindless_indirect_contract`
- `./build/src/test/test_scene_resource_table`
- `cmake --build build --target test_material_instance test_material_v2_parser test_material_v2_resource_dependencies test_gltf_scene_asset_loader test_offline_gpu_scene`
- `./build/src/test/test_offline_gpu_scene`
- `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device`
- `rg -n "SceneGpuMaterialRecord|MaterialUBO|toGpuMaterialRecord|\\.materials" src/test/integration/test_material_source_contract.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp src/test/integration/test_bindless_indirect_contract.cpp src/test/integration/test_scene_resource_table.cpp`

旧 material truth 审计结果：073-b 正向 source-contract 测试使用 `materialRefs + sourceMaterialRecords`；`SceneGpuMaterialRecord` / `toGpuMaterialRecord` 只保留在 legacy/default 路径覆盖或明确的 legacy 断言中。

### 原始 073-b 中未在本 REQ 完成的条目

073-b 的完成边界是“upload view + backend staging foundation”。下列条目需要 renderer、variant 或 validation profile 上下文，已拆给后续节点：

| 原始条目 | 当前状态 | 未在 073-b 完成的原因 | 承接节点 |
|---|---|---|---|
| RenderPath shader source variant、final shader reflection、compile/reflection key 和 `PipelineKey` 完整接入 | `REQ-073-a` 已有 compiler 宏注入能力；073-b 未让 RenderPath resolver 生成最终 variant shader | 073-b 只建立数据表和 backend staging；shader final identity 属于 RenderPath / pipeline 层 | `REQ-073-c` |
| `assets/shaders/glsl/techniques/...` 到 `render_paths/...` 的默认 URI 迁移 | 073-b 只修复 build/runtime shader source 同步，未迁移默认 URI | URI 迁移是 default asset / resolver / positive test 的硬切，必须在 source variant boundary 后单独验证 | `REQ-073-d` |
| `BuildTest` 中裸 `CompileShaders` 对需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader 失败 | 已定位：裸编译 `pbr.frag` / `pbr_gbuffer.frag` / `offline_pbr_direct_ray.comp` 会触发 source variant 缺失错误；073-b 用 targeted tests 和 headless auto suite 验证 | 正确修复不是放宽 shader `#error`，而是让 shader build target 走 material source variant 或拆出不编译 variant-only base shader 的目标 | `REQ-073-c` |
| RenderWorkQueue / geometry pass 默认消费 bindless table 并生成 indirect batch | backend shell 已能 stage table，但 realtime geometry pass 还没有默认消费这些 table | 需要 final shader variant / pipeline identity 后才能判断 batch compatibility 和 split reason | `REQ-073-e` |
| material source / batch / pipeline / draw 的集中 validation diagnostics profile | upload view 和 backend report 已提供结构化数据与 fail-fast 诊断；没有 renderer/batch 级统计输出 | 统计项依赖 RenderWorkQueue batch、pipeline 和 fallback/unsupported draw 归因 | `REQ-073-e` |
| 删除旧 realtime `SceneGpuMaterialRecord` / per-material descriptor / non-bindless fallback | 旧字段仍为 legacy/default path 保留；073-b 正向测试不再把它当 Material v3 真相 | 删除旧路径要等 renderer 默认消费新 table 后硬切，否则会中断当前可运行路径且无法归因 | `REQ-073-f` |
| Helmet/BMW realtime 低分辨率非全黑 smoke | 073-b 验证了 Helmet/BMW source record 与 backend staging；未做 clean realtime smoke | 视觉 smoke 必须证明没有旧 fallback、使用 `render_paths/...` 和 bindless/indirect stats | `REQ-073-f` |
| OfflineRT RenderPathGraph compute path 和 config hard cut | offline Material v2 测试已改为 source records；未改 OfflineRT 默认配置入口 | OfflineRT 有独立 provider/framegraph bridge，需要先建 graph compute path 再删除旧入口 | `REQ-073-g` / `REQ-073-h` |

## 归档记录

2026-06-14 复核通过。073-b 的完成范围是 source-reflected material storage、builtin default texture identity、SceneResourceTable upload view 和 backend bindless/staging foundation；shader variant、URI migration、indirect batching、realtime hard cut 和 OfflineRT 配置入口已明确拆给后续需求。

本次归档前验证：

- `ninja -C build test_material_source_contract test_material_v2_parser test_material_v2_resource_dependencies test_scene_resource_upload_view_v2 test_bindless_indirect_contract test_scene_resource_table test_material_source_variant_pipeline test_pipeline_identity test_pipeline_build_info test_shader_compiler test_073d_render_path_hard_cut`
- `ctest --test-dir build --output-on-failure -R 'test_(material_source_contract|material_v2_parser|material_v2_resource_dependencies|scene_resource_upload_view_v2|bindless_indirect_contract|scene_resource_table|material_source_variant_pipeline|pipeline_identity|pipeline_build_info|shader_compiler|073d_render_path_hard_cut)$'`

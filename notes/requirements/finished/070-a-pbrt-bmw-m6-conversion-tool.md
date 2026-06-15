# REQ-070-a: PBRT BMW M6 场景转换工具

> 2026-06-06 新增：本 REQ 将 PBRT v3 `bmw-m6` 数据转换成 LXEngine 可直接加载的 scene、mesh 和 material 资产。目标不是让引擎直接长期消费 PBRT 文件，而是先新增一个独立转换工具，把外部 reference scene 落到我们现有资产合同上，最终让实时和离线渲染器都能渲染 BMW M6。

## 背景

我们已经从 <https://www.pbrt.org/scenes-v3> 下载 PBRT v3 场景包到：

- `data/pbrt-v3-scenes/pbrt-v3-scenes.tar.gz`
- `data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6/`

`bmw-m6` 目录当前事实：

| 输入 | 当前内容 |
|---|---|
| 主场景 | `bmw-m6.pbrt` |
| Mesh | 114 个 `Shape "plymesh"`，文件位于 `geometry/mesh_*.ply` |
| PLY 格式 | `binary_little_endian 1.0`，顶点包含 `x/y/z/nx/ny/nz`，face 为索引列表 |
| 材质 | 28 个 `MakeNamedMaterial`，实际绑定到 114 个 shape |
| 环境光 | `LightSource "infinite"` 引用 `textures/sky.exr` |
| 光谱资源 | `spds/Al.eta.spd`、`spds/Al.k.spd` |
| BSDF 资源 | `bsdfs/leather.bsdf` |
| 相机 | `LookAt -11 .8 5   -2 -.5 0   0 1 0`，`Camera "perspective" "float fov" 30` |
| PBRT 渲染参数 | `Film 1400x1000`、`Sampler sobol pixelsamples 4096`、`Integrator path maxdepth 10` |

当前 LXEngine 事实：

| 能力 | 当前状态 |
|---|---|
| Scene | `.scene.yaml` 已支持 camera、environment、outputProfiles、offlineRender、mesh URI 和直接 material URI |
| Mesh loader | 规范承诺 OBJ 和 glTF；没有通用三角 PLY runtime loader |
| glTF loader | 当前以单 mesh / 单 primitive 路径为主，不适合作为首版 BMW M6 多 shape 合并输出 |
| Texture loader | 已支持 LDR 图片和 HDR float texture 读取 |
| Material | `.material` + `MaterialInstance` 是统一材质入口，实时和离线通过 profile 与直接 material URI 选择 |

因此本 REQ 选择新增一个 **独立转换工具**，把 PBRT `bmw-m6` 转成 LXEngine 当前最稳妥的资产结构：一个 scene YAML、114 个 OBJ mesh、一组当前 runtime 可加载的 PBR 近似 material 文件、一组无损保留 PBRT 原始语义的 source material 文件、复制后的 EXR/SPD/BSDF 资源，以及转换报告。

## 目标

1. 新增 `lxe_pbrt_scene_convert` 工具，首个支持目标为 PBRT v3 `bmw-m6`。
2. 将 `bmw-m6.pbrt` 解析成 LXEngine `.scene.yaml`。
3. 将 114 个 binary PLY 三角网格转换成 LXEngine 当前可识别的 OBJ 网格文件。
4. 将 PBRT named materials 转成两套 LXEngine 资产：当前可渲染的 PBR 近似 `.material`，以及无损保留 PBRT 原始参数的 source material YAML。
5. 生成实时和离线都能消费的 BMW M6 scene。
6. 对无法精确表达的 PBRT 材质模型输出明确诊断，而不是静默丢失。
7. 为最终高质量离线 path tracing 和实时 PBR 近似渲染建立同一份资产输入。

## 需求

### R1: 独立转换工具入口

新增工具 `lxe_pbrt_scene_convert`，不把 PBRT 解析逻辑放进 editor、offline scene loader 或 realtime renderer。

工具至少支持：

```bash
lxe_pbrt_scene_convert \
  --input data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6/bmw-m6.pbrt \
  --out assets/converted/pbrt/bmw-m6 \
  --scene assets/scenes/pbrt_bmw_m6.scene.yaml
```

要求：

- 输入路径以 PBRT 主文件为准，资源相对路径从该文件所在目录解析。
- 输出目录必须包含 mesh、runtime material、PBRT source material、texture、SPD/BSDF resource 和 manifest/report。
- 工具重复运行时输出稳定；同一输入产生相同文件名和相同 scene node 顺序。
- 工具不启动 renderer，不依赖窗口系统。

### R2: PBRT v3 子集解析

首版 parser 只覆盖 `bmw-m6.pbrt` 实际使用的语法子集。

必须解析：

| PBRT 指令 | 转换用途 |
|---|---|
| `Film` | output profile 分辨率、默认输出名 |
| `LookAt` | camera transform |
| `Camera "perspective"` | camera FOV |
| `Sampler` | 离线 profile metadata |
| `Integrator "path"` | 离线 profile `maxBounce` / metadata |
| `WorldBegin` | world 内容开始 |
| `LightSource "infinite"` | scene environment HDR |
| `MakeNamedMaterial` | material 文件生成 |
| `NamedMaterial` | shape 到直接 material URI / PBRT source material URI 的绑定 |
| `Shape "plymesh"` | mesh 转换输入 |
| `AttributeBegin` / `AttributeEnd` | shape 分组边界 |

如果遇到未支持但会影响结果的 PBRT 指令，工具必须失败并报告行号、指令名和原因。注释掉的 `AreaLightSource` 与 `Texture` 不参与转换，但应在报告中记录为 ignored commented directives。

### R3: PLY 到 OBJ mesh 转换

工具必须读取 `binary_little_endian 1.0` PLY，输出当前 LXEngine 已识别的 OBJ mesh。

要求：

- 保留 position 和 normal。
- face 必须输出为三角面；如果遇到非三角面，工具必须选择可诊断策略：失败，或明确 triangulate 并写入报告。
- 每个 PBRT `Shape "plymesh"` 生成一个稳定命名的 OBJ，例如 `meshes/mesh_00001.obj`。
- 输出 mesh 的 local bounds 写入 manifest，便于测试和后续 scene validation。
- 不把 PLY 直接作为运行时 mesh URI，除非后续另一个 REQ 明确新增通用 PLY triangle loader。

### R4: Scene YAML 生成

工具必须生成 `assets/scenes/pbrt_bmw_m6.scene.yaml` 或用户指定 scene 路径。

scene 要求：

- `scene.name` 使用 `PBRT BMW M6`。
- `gameplayCameraPath` 指向转换出的 camera node。
- camera 来自 PBRT `LookAt` + perspective FOV。
- `scene.environment.enabled` 为 true，`hdrUri` 指向复制后的 `textures/sky.exr`。
- `outputProfiles.pbrt-reference` 使用 PBRT film 分辨率 `1400x1000`。
- `offlineRender.samples` 使用 PBRT `pixelsamples`，`maxBounce` 使用 PBRT `maxdepth`。
- 每个 PBRT shape 生成一个 scene node，mesh URI 指向对应 OBJ。
- 每个 shape 绑定一个直接 material URI：
  - 首版指向 PBR 近似 `.material`，供 realtime profile 与 `offline-pbrt-reference` profile 共用。
  - scene/material manifest 必须同时记录对应的 PBRT source material URI，便于后续离线 renderer 改为消费真实 PBRT BSDF。

### R5: Material 文件生成分为可渲染近似与无损 source material

工具必须为每个 PBRT named material 生成两类资产：

| 输出 | 目标 | 示例路径 |
|---|---|---|
| runtime PBR 近似 `.material` | 当前 realtime/offline loader 可加载，可先渲染完整车体 | `materials/runtime-pbr-approx/CarPaint.material` |
| PBRT source material YAML | 无损保留 PBRT 原始材质语义，供未来真实 BSDF/spectral renderer 使用 | `materials/pbrt-source/CarPaint.pbrt-material.yaml` |

PBRT source material YAML 属于本 REQ 新增的资产格式。它不是当前 generic `.material` loader 的输入，不能为了让 loader 暂时通过而删字段、改语义或丢参数。它必须完整保留 PBRT named material 的原始信息。

PBRT source material YAML 至少包含：

| 字段 | 含义 |
|---|---|
| `schema` | 固定为 `lxe.pbrtMaterialSource.v1` |
| `name` | PBRT named material 名称 |
| `pbrtType` | PBRT `string type` 原值，如 `matte` / `uber` / `substrate` / `metal` / `glass` / `fourier` / `mix` |
| `parameters` | 原始 PBRT 参数表，按类型保留 `float`、`rgb`、`spectrum`、`string` 等 |
| `parameterOrder` | PBRT 文件中的参数顺序，便于 round-trip 和审计 |
| `resourceRefs` | SPD、BSDF、texture 等外部资源的输入路径与复制后路径 |
| `namedMaterialRefs` | `mix` 等材质引用关系 |
| `sourceLocation` | PBRT 文件路径和行号范围 |
| `runtimeApproximation` | 对应 runtime `.material` URI 与近似策略说明 |
| `unsupportedByCurrentRenderer` | 当前 renderer 尚不能精确消费的字段，不代表丢弃 |

要求：

- source material 必须保留 `glass`、`fourier`、`metal` SPD、`mix`、`substrate` 的全部原始参数，即使当前 renderer 不能使用。
- `spectrum eta/k` 必须保留为资源引用，不能只折算成 RGB 后丢掉 SPD。
- `fourier` 的 `bsdffile` 必须保留为资源引用，不能只折算成 leather fallback 后丢掉 BSDF。
- `mix` 必须保留 `namedmaterial1`、`namedmaterial2` 和 `amount`，不能只输出混合后的颜色。
- runtime PBR 近似 `.material` 可以简化，但必须在 source material 和 manifest 中记录它是 approximation。
- 后续真实 PBRT 材质 renderer 应优先消费 source material YAML，而不是重新解析原始 `.pbrt`。

runtime PBR 近似首版转换策略：

| PBRT type | LXEngine 首版映射 |
|---|---|
| `matte` | PBR dielectric，`baseColorFactor = Kd`，高 roughness |
| `uber` | PBR dielectric/transparent 近似，使用 `Kd`、`Ks`、`roughness`，`Kt` 和 `opacity` 写入报告 |
| `substrate` | PBR dielectric/clearcoat-like 近似，使用 `Kd`、`Ks`、`uroughness/vroughness` 平均 roughness |
| `metal` | PBR metallic，`metallicFactor = 1`，从 SPD 名称记录 `eta/k` 来源；首版可近似为铝/银色 baseColor |
| `glass` | 生成 transparent/glass-intent material metadata；实时首版可退化为高 specular 透明近似或报告 unsupported visual fidelity |
| `fourier` | 记录 `bsdffile`，首版 PBR 近似为 leather fallback |
| `mix` | 记录两端 material 与 amount，首版生成 blended baseColor fallback |

要求：

- 每个生成 material 文件必须使用现有 `.material` loader 能识别的 shader 和 parameter/resource 表达。
- runtime `.material` 不得把 unsupported PBRT 字段塞进当前 loader 不能识别的位置；无损信息放在 PBRT source material YAML 和 manifest 中。
- `offline-pbrt-reference` profile 不得声称已精确实现 Fourier BSDF、spectral metal 或 PBRT glass，除非后续 renderer 模块实际支持并消费 source material YAML。
- 转换报告必须列出所有 precision loss，并逐项指向未丢失的 source material 字段位置。

### R6: 资源复制与路径约定

工具必须把 `bmw-m6` 依赖资源复制或引用到输出目录，避免 scene 运行时依赖 `data/` 临时下载目录。

要求：

- `textures/sky.exr` 复制到 `assets/converted/pbrt/bmw-m6/textures/sky.exr`。
- `spds/Al.eta.spd`、`spds/Al.k.spd` 复制到输出目录，并在 manifest 中保留引用。
- `bsdfs/leather.bsdf` 复制到输出目录，并在 manifest 中保留引用。
- `tyrant_monkey_bmw249.blend` 不进入运行时资产输出，但 manifest 可记录原始文件存在。
- 许可证 `BLENDSWAP_LICENSE.txt` 必须复制到输出目录。

### R7: 转换 manifest 与诊断报告

工具必须输出机器可读 manifest，例如 `pbrt_bmw_m6.converted.json`。

manifest 至少包含：

| 字段 | 含义 |
|---|---|
| inputScene | PBRT 主文件路径 |
| sourceSceneBounds | PBRT 注释中的 scene bounds |
| outputScene | 生成的 `.scene.yaml` |
| meshCount | mesh 数量，BMW M6 预期为 114 |
| materialCount | PBRT named material 数量，BMW M6 预期为 28 |
| runtimeMaterialCount | runtime PBR 近似 `.material` 数量，BMW M6 预期为 28 |
| sourceMaterialCount | PBRT source material YAML 数量，BMW M6 预期为 28 |
| shapeMaterials | 每个 mesh 对应的 PBRT material、runtime material URI 和 source material URI |
| camera | eye / target / up / fov |
| environment | EXR 输入与输出路径 |
| sourceResources | SPD / BSDF / license 等原始资源的复制映射 |
| unsupportedFeatures | 精度损失和 unsupported PBRT feature 列表 |

### R8: Renderer 缺口记录

本工具完成后，实时和离线 renderer 仍可能需要下游模块才能高质量渲染 BMW M6。需求必须在报告和文档中明确区分：

| 缺口 | 对最终 BMW M6 的影响 |
|---|---|
| Environment importance sampling | 离线 path tracing 渲染 `sky.exr` 时噪声控制不足 |
| PBRT glass / transparency | 车窗、灯罩无法准确折射/透射 |
| Spectral metal eta/k | 轮毂、Logo、chrome 只能 PBR 近似 |
| Fourier BSDF | leather 内饰无法准确复现 |
| Substrate / clearcoat car paint | 车漆高光层只能近似 |
| 多 mesh scene 性能 | 114 个 node/material 的实时 draw 和离线 BVH 构建需要验证 |

这些下游渲染模块不在本 REQ 实现，但本 REQ 必须把转换结果组织成后续模块可以替换材质和 integrator 的形式。

### R9: 测试覆盖

至少新增以下测试：

- parser test：读取 `bmw-m6.pbrt`，得到 28 个 named materials、114 个 shapes、1 个 infinite light、1 个 perspective camera。
- PLY conversion test：读取一个 BMW M6 binary PLY，输出 OBJ 后 position / normal / face count 与 PLY header 一致。
- scene generation test：生成的 `.scene.yaml` 能被 `SceneDocument` 读取并通过验证。
- runtime material generation test：所有 PBRT named materials 都生成可被 generic material loader 读取的 PBR 近似 `.material` 文件。
- source material preservation test：所有 PBRT named materials 都生成 source material YAML，且 `glass`、`fourier`、`metal`、`mix`、`substrate` 的原始参数和资源引用完整存在。
- manifest test：manifest 中 mesh/runtime material/source material/camera/environment 数量与输入一致。
- offline loader smoke：转换后的 scene 能被 offline scene loader 消费，objectCount 为 114。
- realtime loader smoke：转换后的 scene 能被 editor/runtime scene loader 消费，不要求首版图像质量完全匹配 PBRT。

## 修改范围

- `src/tools/lxe_pbrt_scene_convert/`
- `src/tools/CMakeLists.txt`
- `src/infra/` 中可复用的 PLY/OBJ 写出、PBRT parser helper 或 asset path helper（如果工具需要共享）
- `assets/scenes/`（生成或 fixture scene）
- `assets/converted/pbrt/bmw-m6/`（生成资产输出位置）
- `tests` / `src/test/integration/`
- `notes/requirements/`

## 边界与约束

- 本 REQ 不要求运行时直接加载 PBRT 文件。
- 本 REQ 不要求运行时直接加载 PBRT PLY。
- 本 REQ 不实现完整 PBRT parser，只支持 `bmw-m6` 使用的子集。
- 本 REQ 不实现 spectral renderer。
- 本 REQ 不实现 Fourier BSDF。
- 本 REQ 不实现真实玻璃折射、caustics 或 bidirectional path tracing。
- 本 REQ 不把 114 个 mesh 合并成单 glTF，除非先证明当前 glTF loader 可以稳定消费多 primitive / 多 material。
- 本 REQ 不要求输出图像与 PBRT reference 像素级一致；首版验收是“转换资产可被实时和离线路径加载并渲染出完整车体”。

## 依赖

- `REQ-052-a: Offline Rendering Lab 总览`
- `REQ-056-a: 共享 PBR 纹理材质加载与离线/实时等价验证`
- `REQ-057-a: Offline Path Tracing PBR Reference`
- `openspec/specs/mesh-loading/spec.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/material-asset-loader/spec.md`
- `openspec/specs/texture-loading/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`

## 下游工作

- BMW M6 高质量离线 path tracing 验收图。
- PBRT glass / metal / substrate / Fourier 材质模型的 renderer 支持。
- Environment importance sampling 与 HDR sky 光照降噪。
- 如果后续要支持更多 PBRT v3 scenes，再把本工具从 `bmw-m6` 子集扩展为通用 PBRT scene converter。

## 实施状态

2026-06-14 复核关闭：PBRT BMW M6 转换工具已实现。当前入口为 `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`，配套 README、默认值配置、manifest/source material 输出和 `src/test/integration/test_lxe_pbrt_scene_convert.py`。

当前输出已经进入 Material v2/source-contract 路线；完整 PBRT `glass`、`fourier`、`mix`、conductor `eta/k` 的物理支持由后续材质需求承接。

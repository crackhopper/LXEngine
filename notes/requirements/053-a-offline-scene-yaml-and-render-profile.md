# REQ-053-a: Offline Scene YAML 与 Render Profile

> 2026-06-01 新增：本 REQ 扩展 `.scene.yaml`，让同一份场景能声明离线渲染 profile。当前仍在讨论中，未开始。

## 背景

当前 editor/runtime 已经使用 `.scene.yaml` 描述场景。`assets/scenes/ibl_metal_sphere.scene.yaml` 已经包含 `scene.environment`，用于加载 HDR 环境并触发 IBL bake。

Offline Rendering Lab 不应另起一套 scene 格式。第一版应复用 `.scene.yaml`，并在需要时扩展 `scene.offlineRender`，让同一份场景同时服务实时渲染、离线 ground truth、后续 bake 和 editor preview。

## 目标

1. 定义 `.scene.yaml` 的离线渲染 profile 扩展。
2. CLI 能读取 profile，也能覆盖 profile 参数。
3. 保持现有 scene 文件可继续加载。
4. 让离线 renderer 的输入仍然是引擎场景，而不是旁路格式。
5. 把通用 scene YAML IO 从 editor demo 中下沉，避免 tools 反向依赖 `src/demos/lxe_editor/`。

## 需求

### R1: `scene.offlineRender` profile schema

新增可选字段：

```yaml
scene:
  offlineRender:
    defaultProfile: preview
    profiles:
      preview:
        backend: vulkan-compute
        integrator: primary-ray
        width: 512
        height: 512
        samples: 1
        maxDepth: 1
        seed: 1
        outputFormat: exr-png
      mvp:
        backend: vulkan-compute
        integrator: primary-ray
        width: 1024
        height: 576
        samples: 4
        maxDepth: 1
        seed: 1
        outputFormat: exr-png
      reference:
        backend: vulkan-compute
        integrator: path-tracing
        width: 1920
        height: 1080
        samples: 64
        maxDepth: 4
        seed: 1
        outputFormat: exr-png
```

首版字段至少包含：

| 字段 | 含义 |
|---|---|
| `backend` | `vulkan-compute` |
| `integrator` | `primary-ray`，后续扩展 `path-tracing` / `probe-bake` |
| `width` / `height` | 输出分辨率 |
| `samples` | 每像素 sample 数 |
| `maxDepth` | 最大路径深度，MVP 可固定为 1 |
| `seed` | 可复现随机种子 |
| `outputFormat` | `exr-png` |

profile 数据结构归 `src/core/offline/offline_render_profile.*` 管理。`scene_io`
负责 YAML round-trip，offline scene compiler 负责把 profile 与 scene document 输入组合成离线渲染 job。

offline compiler 首版主路径：

```text
.scene.yaml -> scene_io scene document -> OfflineSceneIR
```

首版不要求先构建 editor runtime `Scene`。后续可以增加 runtime `Scene -> OfflineSceneIR`
辅助路径，但不能作为首版工具链前置。

profile 分层约定：

| Profile | 用途 |
|---|---|
| `preview` | 快速检查，建议 512x512 / samples=1 / maxDepth=1 |
| `mvp` | `REQ-054-b` 验收，建议 1024x576 / samples=4 / maxDepth=1 |
| `reference` | `REQ-057-a` 高质量 reference，建议 samples>=64 / maxDepth>=4 |

### R2: CLI 覆盖规则

`lxe_offline_render` 支持从 profile 读取默认值，并允许命令行覆盖：

```bash
lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile reference \
  --samples 64 \
  --out artifacts/offline/ibl_metal_sphere
```

覆盖规则：

- CLI 参数优先于 profile。
- 未指定 `--profile` 时使用 `defaultProfile`。
- 没有 `offlineRender` 时使用内置 default profile。
- 输出路径可以是目录或 basename，具体文件后缀由 `REQ-055-a` 定义。
- `--seed` 可覆盖 profile seed；未指定时默认固定为 `1`，保证重复渲染可复现。

### R3: Scene document round-trip

`scene.offlineRender` 必须能保存和重载，不破坏现有 scene 字段。

要求：

- `scene.offlineRender` 必须结构化解析和保存。
- 未知 profile 字段要么保留，要么产生明确诊断；不能静默破坏 scene。
- 对象级 `offline:` 子树首版至少用 raw YAML node 或 extension map 保留，不要求 C++ 强类型。
- 保存 scene 时不能删除已存在的对象级 `offline:` 子树。
- 对其它未知字段保持当前 scene_io 行为，不要求全 scene lossless round-trip。
- 默认值不应在保存时制造大量无意义字段。
- 现有 scene 没有 `offlineRender` 时仍能正常加载。

### R4: 与 environment 配合

offline profile 不复制 environment 路径。HDR 环境仍来自 `scene.environment`。

要求：

- 离线 renderer 通过 scene 的 environment 读取 HDR。
- `offlineRender` 只描述渲染任务，不描述场景内容。
- 后续 bake profile 可以引用 camera/probe/lightmap target，但不复制资产 URI。

### R4.1: Cache asset URI

从 assets-downloader 导入的外部资产在 scene 中统一使用 `cache://` URI，而不是直接写
`.asset_cache/` 物理路径。

格式：

```text
cache://<source>/<asset-id>/<variant>/<relative-converted-path>
```

示例：

```yaml
scene:
  environment:
    type: hdr
    source: cache://polyhaven/studio_small_03/2k-hdr/converted/environment.exr

materials:
  brushedMetal:
    baseColorTexture: cache://ambientcg/metal_001/1k/converted/textures/basecolor.png
```

要求：

- `scene_io` 负责保留 URI 字符串，不把它提前展开成绝对路径。
- runtime/editor/offline renderer 通过统一 asset resolver 解析 `cache://`。
- resolver 默认从 repo root 下 `.asset_cache/` 查找 cache 资产。
- resolver 支持 `LXENGINE_ASSET_CACHE` 环境变量覆盖 cache root。
- scene 不直接引用 `.asset_cache/.../raw/...`。
- 内置小资产仍可沿用当前 `assets/...` 路径；外部 cache 资产统一走 `cache://`。

### R4.2: 复用现有 light 节点 schema

当前 scene YAML 已支持光源节点，通用格式为节点上的 `light:` 字段，示例：

```yaml
children:
  - nodeName: dir_light_node
    name: dir_light
    light:
      kind: Directional
      direction: [-0.35, -1.0, -0.25]
      color: [1.0, 0.96, 0.88]
      intensity: 0.8
      shadowStrength: 0.45
      shadowDistance: 80.0
      shadowCascadeCount: 4
```

离线渲染不新增旁路 light schema。要求：

- offline scene compiler 复用现有 `light:` 节点格式。
- MVP 至少消费 `kind: Directional`、`direction`、`color`、`intensity`。
- `shadowStrength` 作为 realtime shadow 调参保留；offline MVP 的 hard shadow 是物理遮挡，遮挡时 direct light 为 0，不用 `shadowStrength` 混合阴影。
- offline MVP 的 directional light 默认参与 shadow ray。
- offline compiler 忽略 realtime-only shadow 参数，例如 `shadowStrength`、`shadowDistance`、`shadowCascadeCount`，除非未来某个离线需求明确复用它们。
- 如果离线渲染需要额外 light 字段，可以在 scene 中增加离线扩展字段；实时 renderer 不要求支持这些字段。
- 现有 legacy `directionalLight:` 可由 `scene_io` 兼容读取，但新增/保存不扩展 legacy 格式。
- point / spot / area light 不属于 offline MVP；遇到时按 `REQ-054-b` support matrix 输出 warning/error。

### R4.3: 离线专用对象扩展字段

如果未来 light/camera/material/mesh 需要离线专用配置，统一放在对象的 `offline:`
子字段中，不与 realtime 字段平铺混用。

示例：

```yaml
light:
  kind: Directional
  direction: [-0.35, -1.0, -0.25]
  color: [1.0, 0.96, 0.88]
  intensity: 0.8
  offline:
    sampleWeight: 1.0

material:
  uri: cache://ambientcg/wood_floor/1k/converted/material.yaml
  offline:
    doubleSided: false
```

要求：

- `offline:` 子字段只由 offline compiler 消费。
- realtime renderer 可以忽略 `offline:` 子字段。
- `scene_io` round-trip 必须保留未知 `offline:` 字段。
- 首版 MVP 不要求新增具体 offline-only 字段，只定义扩展位置。

### R5: 测试覆盖

覆盖：

- 无 `offlineRender` 的旧 scene 可加载。
- 带 `offlineRender.profiles` 的 scene 可 round-trip。
- CLI 覆盖 profile 的 width/height/samples/out。
- 缺失 profile 名称时给出可诊断错误。

### R6: Scene IO 下沉

当前 `.scene.yaml` 的主要 parse/save 能力在 `src/demos/lxe_editor/scene_document.*`。
为了让 `lxe_offline_render` 复用 scene 文件而不依赖 editor demo，需要把通用
scene YAML IO 下沉到更干净的模块。

建议目标：

| 模块 | 职责 |
|---|---|
| `src/core/scene/` | scene document 中与运行时语义无关的数据结构，可选 |
| `src/infra/scene_io/` | `.scene.yaml` parse/save、offline profile round-trip、资产 URI 字符串保留与基础校验 |
| `src/demos/lxe_editor/` | project/session/command/API/editor-specific 状态 |

要求：

- `src/tools/lxe_offline_render/` 不得依赖 `src/demos/lxe_editor/`。
- editor 与 offline CLI 共同依赖 `scene_io`。
- editor-specific 字段或行为不得污染 offline scene parser。
- `OfflineSceneCompiler` 首版从 `scene_io` scene document 编译 `OfflineSceneIR`，不依赖 editor runtime `SceneRuntime`。
- 迁移后现有 editor scene save/load 行为保持不变。
- 不允许为了快速 MVP 复制一份 scene YAML parser 到 tools。

## 修改范围

- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/infra/scene_io/`
- 新增 offline CLI 参数解析模块
- `assets/scenes/ibl_metal_sphere.scene.yaml`
- `assets/project_templates/pbr_ibl/scenes/ibl_metal_sphere.scene.yaml`
- 相关 tests

## 边界与约束

- 本 REQ 不实现 compute renderer。
- 本 REQ 不实现 EXR 写入。
- 本 REQ 不引入独立 offline scene 格式。
- 本 REQ 不要求 editor UI 编辑 profile。

## 依赖

- `REQ-052-a`
- 当前 `.scene.yaml` scene document 体系

## 后续工作

- `REQ-054-a` 使用 profile 启动 Vulkan compute 离线渲染。
- `REQ-058-a` 在 editor 中选择/运行 profile。

## 实施状态

讨论中，未开始。

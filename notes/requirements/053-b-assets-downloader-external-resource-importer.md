# REQ-053-b: Assets Downloader 外部资源下载与导入工具

> 2026-06-01 更新：`assets-downloader` 已落地为独立 TypeScript + React 项目，当前支持本地 Web UI、Fastify API、catalog、preview plan、手动 import job、cache metadata、HDR/GLB/PBR 材质整理，以及 PLY 点云/3DGS 源文件 cache 导入。复杂归档解压、editor 完整 Asset Browser 和 EXR/PNG 输出仍由后续需求推进。

## 背景

Offline Rendering Lab 需要高质量场景、HDR skybox、PBR 材质和 glTF 模型来产生可信 reference 图。把这些大型资产直接提交到 git 会膨胀仓库；完全手工下载又会导致路径、许可、格式、版本不可复现。

因此需要一个独立项目 `assets-downloader`。它提供 Web UI，展示每个数据源站点的导航入口、分类筛选和推荐可导入 URL。用户可以打开原始站点查看资产信息，再把 URL 传给导入工具。工具负责下载、解压缩、确认并移动 license、转换/整理为引擎可用格式，最终存放到 cache 中。

后续工作流由 `lxe-editor` 接管：`lxe-editor` 识别 cache 中的资产，用户在 editor 里把资产添加到场景、编辑灯光/相机/材质并导出 `.scene.yaml`。离线渲染器读取该场景文件进行 ray tracing；实时渲染效果由 `lxe-editor` 显示。

## 目标

1. 提供 React Web UI 浏览数据源站点和推荐导入 URL。
2. 支持用户传入 URL，下载并整理外部资产到 cache。
3. 确认、保存并移动 license 信息，保证每个资产来源可追踪。
4. 把外部资产转换为 `lxe-editor` 可识别的格式和 cache metadata。
5. 避免大型二进制资产进入 git。
6. 让 `lxe-editor` 能从 cache 中导入资产并写入场景。

## 需求

### R1: `assets-downloader` TypeScript + React app

新增独立 TypeScript + React 项目，位置固定为：

```text
src/tools/assets-downloader/
  package.json
  tsconfig.json
  vite.config.ts
  src/
    backend/
    frontend/
    shared/
  catalog/
```

项目名固定为 `assets-downloader`。它不是 C++ engine 的一部分，但与仓库同源开发。

项目整体定义：

- 下载外部资源。
- 从用户输入的 URL 导入资源到本地 cache。
- 转换为 `lxe-editor` 可识别的格式与路径。
- 生成 cache metadata，供 `lxe-editor` 发现和导入。
- 不直接替代 editor 的场景编辑与保存流程。

首版启动命令：

```bash
pnpm --dir src/tools/assets-downloader dev
```

技术栈固定为：

| 层 | 技术 |
|---|---|
| frontend | Vite + React + TypeScript |
| backend | Node.js + TypeScript + Fastify |
| package manager | pnpm |
| scope | 本地开发工具，默认只监听 `localhost` |

要求：

- 一个 dev 命令同时启动 frontend 与 backend。
- UI 与 backend 在同一个 `assets-downloader` 项目中维护，不拆成独立 monorepo。
- backend API 首版只服务本地 Web UI，不做公网部署。
- 不提供 CLI。
- 未来如需外部自动化，再扩展 REST API；本 REQ 只要求本地 API 结构清晰。

本地 API 首版候选：

| API | 用途 |
|---|---|
| `GET /api/sources` | 列出数据源、分类和推荐 URL |
| `POST /api/import/preview` | 生成 import preview plan，不写文件 |
| `POST /api/import/start` | 启动 import job |
| `GET /api/jobs/:id` | 查询 job 状态和日志 |
| `GET /api/cache` | 列出 cache 中已导入资产 |

联网行为：

- 用户点击 Import 后，assets-downloader 必须能真实联网下载 URL 指向的资产。
- build、自动测试、`lxe-editor` 启动和 offline renderer 启动不得自动联网。
- 测试使用 mock downloader 或 local fixture，不访问真实 URL。
- import preview 可以做本地 recipe/URL 解析；如未来需要联网探测 metadata，必须在 UI 上明确提示。

### R2: 前端 UI 主流程

首版 UI 流程：

1. 打开 Web 页面。
2. 首页显示每个数据源站点的导航按钮。
3. 点击进入某个数据源站点页面。
4. 站点页面提供分类筛选按钮，例如 HDRI / model / material / scene / course asset。
5. 每个类别显示推荐的可导入 URL。
6. 用户可以打开原始站点查看数据、license、预览图、分辨率和下载信息。
7. 页面下方提供导入工具，用户粘贴 URL。
8. 点击 Import 后，工具下载、解压、确认 license、转换并整理到 cache。
9. `lxe-editor` 识别 cache 资产，用户在 editor 中添加到场景。
10. 用户在 `lxe-editor` 中编辑场景并导出 `.scene.yaml`。
11. `lxe_offline_render` 读取 `.scene.yaml`，按 profile 输出 EXR + PNG。

UI 至少包含：

| 区域 | 功能 |
|---|---|
| Data source navigation | Poly Haven、ambientCG、Khronos glTF Sample Assets、pbrt scenes 等站点入口 |
| Source detail | 来源说明、license 策略、原始站点入口 |
| Filters | 按 HDRI / model / material / scene / course asset 等分类筛选 |
| Recommended URLs | 每个类别显示推荐可导入 URL |
| Import tool | 输入 URL，执行 import |
| Preview plan | import 前显示 URL、来源站点、license 状态、cache 目标路径、将写入文件 |
| Job log | 展示下载、解压、license 确认、转换、cache 写入进度与错误 |
| Cache browser | 显示 cache 中已经整理好的可用资产 |

要求：

- UI 不允许隐藏 license 信息。
- UI 执行 import 前必须展示 preview plan。
- UI 可以打开原始站点，让用户查看数据详情。
- UI 首版服务本地开发，不要求公网部署。
- UI 中每个 action 应映射到 backend TypeScript service API。

### R3: Backend service

后台使用 TypeScript 编写，负责本地文件系统、下载、解压、转换、license 处理和 cache metadata。

职责：

| 服务 | 职责 |
|---|---|
| source registry | 读取默认数据源与推荐 URL |
| import planner | 根据 URL 判断 source、variant、目标 cache 路径和需要写入的文件 |
| downloader | 手动触发下载，不在 build/test/editor 启动时自动下载 |
| archive extractor | 解压 zip/tar 等归档 |
| license collector | 查找、保存、移动 license / credit 文件 |
| converter | 转换或整理为引擎可识别格式 |
| cache indexer | 写入 cache metadata，供 `lxe-editor` 发现 |

### R3.1: 首版 converted output 范围

首版 converter 只做可控的整理和轻量转换，不做复杂场景自动重建。

| kind | 输入来源 | converted 输出 |
|---|---|---|
| `environment` | HDRI / EXR / HDR environment | `converted/environment.exr` 或 `converted/environment.hdr` |
| `model` | glTF / GLB model | `converted/model.glb` |
| `material` | PBR texture set | `converted/material.yaml` + `converted/textures/*` |
| `point-cloud` | PLY 点云 / 3DGS splat source | `converted/point_cloud.ply` + `converted/point_cloud.asset.yaml` |

要求：

- converted 输出必须能通过 `cache://` URI 被 `lxe-editor` / offline renderer resolver 找到。
- `material.yaml` 至少记录 baseColor、normal、metallicRoughness、ao、emissive 纹理路径和 color space。
- 首版不做 mesh 拓扑重写。
- 首版不做 texture compression。
- 首版点云导入只复制 PLY 并生成 manifest，不解析或重排 Gaussian splat。
- 首版不自动把 pbrt/Mitsuba/Tungsten/GAMES 复杂场景转换成 LXEngine `.scene.yaml`。
- 对 pbrt/Mitsuba/Tungsten/GAMES 资产，首版只要求下载、license、cache metadata 和 manual import notes。

`material.yaml` 最小合同：

```yaml
kind: material
model: pbr-metallic-roughness
baseColor:
  texture: cache://ambientcg/wood_floor_001/1k/converted/textures/basecolor.png
  colorSpace: srgb
normal:
  texture: cache://ambientcg/wood_floor_001/1k/converted/textures/normal.png
  colorSpace: linear
metallicRoughness:
  texture: cache://ambientcg/wood_floor_001/1k/converted/textures/metallic_roughness.png
  colorSpace: linear
  metallicChannel: b
  roughnessChannel: g
ao:
  texture: cache://ambientcg/wood_floor_001/1k/converted/textures/ao.png
  colorSpace: linear
emissive:
  texture: null
  colorSpace: srgb
defaults:
  baseColor: [1, 1, 1, 1]
  metallic: 0
  roughness: 0.5
```

要求：

- `material.yaml` 只描述 PBR metallic-roughness，不描述材质图。
- 所有纹理路径使用 `cache://`。
- 必须明确 color space。
- metallic/roughness packed texture 必须明确通道约定。
- 缺失纹理使用 `null`，并通过 `defaults` 提供 scalar fallback。

### R3.2: License 状态机

assets-downloader 必须用统一 license 状态机驱动 UI、import planner 和 metadata。

| 状态 | 含义 | 是否允许 import |
|---|---|---|
| `verified` | 默认 catalog/recipe 已确认许可，例如 CC0 | 允许 |
| `user_confirmed` | 用户在本机手动确认许可 | 允许，但 metadata 必须记录确认时间、用户确认来源和 license URL/文件 |
| `blocked` | 缺失、不明确、需要登录授权、禁止使用或 recipe 判定不可用 | 不允许 |
| `unknown` | planner 无法自动确认许可 | 默认不允许；必须先转为 `user_confirmed` 或由 recipe 修正为 `verified` |

要求：

- 默认推荐 URL 只能使用 `verified` 状态。
- import preview plan 必须显示 license 状态。
- `blocked` 和 `unknown` 状态下 Import 按钮默认不可执行。
- 每个已导入 cache asset 必须保存 license 文件或 license URL。
- GAMES101/GAMES202 等课程资源默认不能标记为 `verified`，除非许可明确允许下载和本地使用。

### R4: Catalog manifest and TypeScript schema

默认 catalog 记录数据源站点、分类、推荐 URL 和导入 recipe。

示例：

```yaml
sources:
  - id: polyhaven
    name: Poly Haven
    url: https://polyhaven.com/
    categories:
      - id: hdri
        recommended:
          - id: studio_small_03
            url: https://polyhaven.com/a/studio_small_03
            license: CC0
            variants:
              - 2k-hdr
              - 4k-exr
```

每个条目至少记录：

- source id
- category
- recommended URL
- expected license
- supported variants
- conversion recipe
- manual notes

首版 manifest 的角色：

- TypeScript app 负责生成、校验和更新 manifest。
- C++ runtime/offline renderer 不强依赖 catalog manifest。
- `lxe-editor` 可以读取 cache metadata，而不是直接读取下载器 catalog。
- `.scene.yaml` 与 offline profile 只消费 cache metadata 解析出的资产 URI，不依赖下载器 UI 或 catalog 实现。
- 未来 AssetRegistry 可以复用 metadata，但本 REQ 不要求完成 C++ AssetRegistry。

### R5: Cache 资产布局

下载和转换结果默认存入 cache，不进入 git。

cache root 查找规则：

1. 默认使用 repo root 下 `.asset_cache/`。
2. 如果设置 `LXENGINE_ASSET_CACHE`，则使用该环境变量指向的目录。
3. assets-downloader、`lxe-editor`、`lxe_offline_render` 必须使用同一套 resolver 规则。
4. scene 文件只保存 `cache://` URI，不保存 cache root 的绝对路径。

建议路径：

```text
.asset_cache/<source>/<asset-id>/<variant>/
  source.yaml
  license/
  raw/
  converted/
    model.glb
    textures/
    environment.exr
```

要求：

- `.asset_cache/` 保存原始下载和转换结果，默认不进 git。
- 每个 cache asset 必须有 metadata。
- metadata 包含 source URL、license、download time、content hash、converted outputs。
- converted 输出必须是 `lxe-editor` 可以识别或后续可识别的格式。
- scene 文件不直接引用 raw 下载路径。
- scene 文件引用 converted 输出时统一使用 `cache://` URI。
- repo `assets/` 只保留内置小资产、测试资产和模板，不默认承载外部下载资产。

`cache://` URI 格式：

```text
cache://<source>/<asset-id>/<variant>/<relative-converted-path>
```

示例：

```text
cache://polyhaven/studio_small_03/2k-hdr/converted/environment.exr
cache://ambientcg/wood_floor_001/1k/converted/textures/basecolor.png
cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

要求：

- `assets-downloader` 写入 metadata 时必须记录每个 converted output 对应的 `cache://` URI。
- `cache://` 只能指向 converted 输出，不能指向 raw 下载文件。
- URI 的 `<source>/<asset-id>/<variant>` 必须与 cache metadata 一致。
- 真实文件路径由统一 asset resolver 根据 cache root 解析；不得由各调用方手写路径拼接。

### R6: Git 与安全/许可边界

要求：

- 下载得到的外部资产原始文件和转换后的大体积文件都不得进入 git 仓库。
- `.asset_cache/` 和 `artifacts/` 都不得进入 git 仓库。
- 只提交 catalog metadata、recipe、scene template 或小型测试资产。
- 只允许导入默认 source 或用户确认的 URL；导入前必须显示 source 和 license 状态。
- 每个外部资产必须声明 license。
- 默认 catalog 只接受 CC0 或明确允许本地使用/再分发的资产。
- 缺失 license、license 不明确或需要账号授权的资产不得进入默认 catalog。
- `unknown` / `blocked` license 状态不得写入可用 converted cache asset。
- GAMES101/GAMES202 资产只能提供用户本地安装 recipe，除非 license 明确允许下载和再分发。

### R7: 与 `lxe-editor` 的连接

assets-downloader 不直接编辑最终场景。它提供 cache 资产，`lxe-editor` 负责把资产加入场景并导出 `.scene.yaml`。

要求：

- `lxe-editor` 能发现 `.asset_cache/` 中的 converted assets。
- cache metadata 提供 display name、kind、source、license、converted path 和 `cache://` URI。
- `lxe-editor` 可以把 cache asset 添加为 scene environment、mesh node、material texture 或 material preset。
- `lxe-editor` 保存 scene 时写入 realtime renderer 和 offline renderer 都能解析的 asset URI。
- 如果 cache 资产缺失，renderer 报 scene/material/texture path 缺失；assets-downloader 提供重新 import 的入口。

首版 editor 连接范围：

- 不实现完整 Asset Browser。
- 提供最小 cache import panel 或 command。
- 扫描默认 `.asset_cache/` 或 `LXENGINE_ASSET_CACHE`。
- 列出 converted assets：`environment`、`model`、`material`。
- 支持 `Add Environment`、`Add Model Node`、`Assign Material To Selected`。
- 保存 scene 时写入 `cache://` URI。
- 不要求缩略图网格、标签系统、复杂搜索或热重载。

### R8: 推荐数据源与 URL

默认 UI 首页至少包含以下数据源导航：

| 数据源 | 推荐 URL | 用途 |
|---|---|---|
| Poly Haven | `https://polyhaven.com/hdris` | CC0 HDRI，可用于 skybox/environment |
| ambientCG | `https://ambientcg.com/` | CC0 PBR material/texture |
| Khronos glTF Sample Assets | `https://github.com/KhronosGroup/glTF-Sample-Assets` | glTF importer/PBR 测试；逐资产确认 license |
| glTF Sample Viewer | `https://github.khronos.org/glTF-Sample-Viewer-Release/` | 用于对照 glTF PBR 表现 |
| pbrt-v4 scenes | `https://github.com/mmp/pbrt-v4-scenes` | SIGGRAPH/渲染研究常用复杂场景；多种 license，逐场景确认 |
| pbrt resources | `https://www.pbrt.org/resources` | pbrt 官方资源索引 |
| Bitterli rendering resources | `https://benedikt-bitterli.me/resources/` | 渲染研究常用场景，含 Mitsuba/pbrt/Tungsten 格式和显式 license |
| McGuire CG Archive | `https://casual-effects.com/data` | Sponza、Bistro、San Miguel 等研究常用模型索引，逐项确认 license |
| GAMES101/GAMES202 course assets | 课程作业仓库或课程页面 | 可选 catalog source；必须逐项确认课程资源许可和再分发限制 |
| Voxel51 Gaussian Splatting | `https://huggingface.co/datasets/Voxel51/gaussian_splatting` | 3DGS / 点云 PLY 样例资产，当前推荐 train iteration 7000 |

对于 pbrt/Mitsuba/Tungsten 格式场景，首版可先支持“记录、下载、cache 整理和手动说明”，转换到 LXEngine scene 的能力作为后续扩展。

### R9: Demo 2 工作流

高质量 Demo 2 的标准工作流：

1. 使用 assets-downloader 导入 HDRI、模型和 PBR texture 到 cache。
2. 打开 `lxe-editor`。
3. 从 cache browser 中选择 HDRI 作为 environment。
4. 从 cache browser 中选择模型添加到 scene。
5. 从 cache browser 中选择 PBR texture/material 绑定到材质。
6. 在 editor 中布置 camera、directional light、材质参数。
7. 保存/导出 `.scene.yaml`。
8. 使用 `lxe_offline_render` 读取该 scene，输出高质量 EXR + PNG。

### R10: 测试覆盖

覆盖：

- catalog TypeScript schema 校验。
- UI 能列出默认数据源与分类。
- 每个默认推荐资产条目都有 recipe。
- import preview plan 不写文件也能列出 URL、license、cache path。
- 缺 license 或未知 license 时拒绝进入默认 catalog。
- `unknown` / `blocked` license 状态下 Import 不会写入 converted cache asset。
- `user_confirmed` 状态写入 metadata 时包含确认时间和 license 来源。
- cache metadata 包含 source URL、license、hash 和 converted outputs。
- `lxe-editor` cache discovery 可读取一个 fake converted asset。

## 修改范围

- `src/tools/assets-downloader/`
- `.gitignore`
- `.asset_cache/` 目录约定
- `lxe-editor` cache asset discovery 后续接口
- `notes/requirements/`

## 边界与约束

- 本 REQ 不要求 CLI。
- 本 REQ 不要求 REST API；REST API 是未来外部调用入口。
- 本 REQ 不要求 editor asset browser 完整实现，但必须定义 cache metadata 供 editor 识别。
- 本 REQ 不要求自动下载所有资产；下载必须手动触发。
- 本 REQ 不把大型外部资产提交到 git。
- 本 REQ 不绕过第三方 license；缺许可记录的资产不能进入默认 catalog。
- 本 REQ 不要求首版接入 Sketchfab 账号/API。
- 本 REQ 不要求把 pbrt/Mitsuba/Tungsten 场景自动转换成 LXEngine scene。

## 依赖

- `REQ-052-a`
- 当前 asset path / scene YAML 约定

## 后续工作

- 当前 scene/profile 侧通过统一资产 URI 消费 cache 资产。
- 当前 Vulkan compute offline renderer 已可使用 cache 中的 scene/model/HDR 资产作为输入。
- PBR/Material v3 纹理质量验证由 `REQ-073-*` 与 `REQ-075-a` 承接。
- `REQ-076-a` 使用 cache 中高质量 demo 资产输出最终离线 ray tracing reference 图。
- 未来 editor AssetRegistry 可复用 cache metadata。

## 实施状态

2026-06-14 复核：保留 active。assets-downloader MVP 已可用，但本 REQ 的自动解压、完整 Asset Browser、外部 scene 转换、缩略图/搜索/热重载仍未完成。

已实现首版 MVP：

- `src/tools/assets-downloader/` 已提供 React + Vite UI、Fastify backend、共享 zod schema、catalog 和 cache metadata。
- `pnpm --dir src/tools/assets-downloader dev` 可启动本地工具。
- `pnpm --dir src/tools/assets-downloader test` 覆盖 catalog、preview plan、license gate、cache import 和 PLY 点云 manifest。
- `pnpm --dir src/tools/assets-downloader build` 可生成前端 production bundle。
- 3DGS train PLY 不进入 git；通过 `voxel51-gaussian-splatting/train_iteration_7000/iteration-7000` cache 资产管理。

未完成范围：

- 归档自动解压。
- editor 端完整 Asset Browser。
- pbrt/Mitsuba/Tungsten/GAMES scene 自动转换。
- 资产缩略图、标签搜索和热重载。

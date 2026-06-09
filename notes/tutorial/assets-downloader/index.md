# Assets Downloader：把外部资源整理进本地 Cache

Assets Downloader 像一张进货单和仓库登记台：外部网站提供原始 HDRI、模型、PBR 贴图或 PLY 点云；工具负责检查来源、预览导入计划、下载文件、写入 license 记录，并把可被 scene 引用的结果放进本地 cache。LXEngine 的 scene 不直接记住网页地址，而是引用 `cache://.../converted/...` 这样的稳定 URI。

这页讲当前已经落地的工具用法。它不是 C++ 构建的一部分，也不会在 editor、测试或 offline renderer 启动时自动联网；所有下载都由本地 Web UI 手动触发。

## 启动本地工作台

从仓库根目录启动：

```bash
corepack pnpm --dir src/tools/assets-downloader install
corepack pnpm --dir src/tools/assets-downloader dev
```

当前 `dev` 脚本会同时启动两个进程：

| 服务 | 地址 | 当前职责 |
|---|---|---|
| React UI | `http://127.0.0.1:5173/` | 选择数据源、预览导入计划、查看 job log 和 cache |
| Fastify API | `http://127.0.0.1:4731/` | 提供 catalog、import preview/start、job 查询、cache 列表和 build info |

API 端口可以用环境变量覆盖：

```bash
ASSETS_DOWNLOADER_PORT=4831 corepack pnpm --dir src/tools/assets-downloader dev
```

UI 的 Vite 端口仍由 Vite 自动管理，默认是 `5173`。

## Cache 根目录和 URI 规则

默认 cache 根目录是仓库下的 `.asset_cache/`。如果我们要把大资产放到别的磁盘，设置同一个环境变量即可：

```bash
export LXENGINE_ASSET_CACHE=/data/lxengine_asset_cache
corepack pnpm --dir src/tools/assets-downloader dev
```

Offline renderer 的 `OfflineAssetResolver` 也读取 `LXENGINE_ASSET_CACHE`。因此 assets downloader 写入哪里，scene 里的 `cache://` URI 就会从同一个位置解析。

| 形式 | 含义 |
|---|---|
| `.asset_cache/<source>/<asset>/<variant>/raw/source.bin` | 原始下载文件 |
| `.asset_cache/<source>/<asset>/<variant>/source.yaml` | cache metadata、license、hash、converted outputs |
| `.asset_cache/<source>/<asset>/<variant>/license/license.yaml` | license 状态记录 |
| `.asset_cache/<source>/<asset>/<variant>/converted/...` | scene 可以引用的整理后结果 |
| `cache://<source>/<asset>/<variant>/converted/...` | scene / offline resolver 使用的 URI |

`cache://` 当前只允许指向 `converted/` 下的输出。raw 文件保留给审计和重新转换，不作为 scene 的直接引用目标。

## 从推荐项导入

UI 左侧是 catalog 数据源，来自 `src/tools/assets-downloader/catalog/default.yaml`。当前默认 catalog 包含 Poly Haven、ambientCG、Khronos glTF Sample Assets、pbrt/Bitterli/McGuire research resources、课程资产入口和 Voxel51 Gaussian Splatting。

推荐项的典型流程是：

| 步骤 | UI 动作 | 后端发生什么 |
|---|---|---|
| 选择 source | 左侧点击数据源 | `/api/sources` 返回 catalog |
| 选择推荐资产 | 在 `Recommended URLs` 点击 `Use URL` | UI 填入推荐 URL、asset id、variant 和 kind |
| 预览计划 | 点击 `Preview Import` | `/api/import/preview` 生成 cache path、license 状态和将写文件 |
| 开始导入 | 点击 `Start Import` | `/api/import/start` 创建 job，并异步下载、转换、写 metadata |
| 观察结果 | 查看 `Job Log` 和 `Cache Browser` | `/api/jobs/:id` 轮询状态，完成后 `/api/cache` 刷新列表 |

导入只在 license 状态为 `verified` 或 `user_confirmed` 时允许。默认推荐项里已经验证的条目可以直接导入；未知或需要人工确认的来源会停在 preview 阶段。

## 手动 URL 导入

当 catalog 只有导航入口、没有可直接下载的推荐 URL 时，我们先打开原始网站，找到真实文件 URL，再粘到 Import Tool。当前工具按 URL 和 `kind` 生成第一版整理结果：

| `kind` | URL 识别 / UI 来源 | converted 输出 |
|---|---|---|
| `environment` | `.hdr` / `.exr` | `converted/environment.hdr` 或 `converted/environment.exr` |
| `model` | `.glb` / `.gltf` | `converted/model.glb` |
| `material` | 默认 fallback | `converted/material.yaml` 和四张 texture 占位复制 |
| `point-cloud` | `.ply` | `converted/point_cloud.ply` 和 `converted/point_cloud.asset.yaml` |
| `manual` | pbrt / Mitsuba / Tungsten 等复杂场景 | `converted/manual-import-notes.md` |

第一版 conversion 是整理和登记，不是完整资产烘焙器。复杂归档解压、pbrt scene 转换、完整 glTF 材质拆解和 editor Asset Browser 仍属于后续工作流；当前 scene 应引用已经落到 `converted/` 下的具体文件。

## 3DGS PLY 示例

默认 catalog 里有一个 Voxel51 3DGS PLY 推荐项。导入后会得到类似结构：

```text
.asset_cache/voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/
  source.yaml
  license/license.yaml
  raw/source.bin
  converted/point_cloud.ply
  converted/point_cloud.asset.yaml
```

scene 或后续 3DGS 教程应引用：

```text
cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

这个 URI 不依赖本机绝对路径。只要 `LXENGINE_ASSET_CACHE` 在 downloader、editor 和 offline renderer 进程中一致，解析结果就一致。

## 和 Offline Renderer 的连接

Assets Downloader 不直接编辑 scene，也不调用 renderer。它只把外部文件整理进 cache；scene 文件再通过 `cache://` URI 引用这些 converted 输出。Offline renderer 读取 scene 时，`OfflineAssetResolver` 会按同一规则解析：

| scene URI | resolver 结果 |
|---|---|
| `assets/...` | 仓库 runtime root 下的资产 |
| 相对路径 | 先按仓库 root，再按 scene 文件目录解析 |
| 绝对路径 | 直接使用 |
| `cache://source/asset/variant/converted/file` | `LXENGINE_ASSET_CACHE` 或 `.asset_cache/` 下的文件 |

当前 offline software-compute MVP 主要消费内置 primitive、材质常量、方向光、background color 和 output profile；HDR environment texture、PBR texture sampling、复杂模型导入后的离线采样仍需要后续扩展。Assets Downloader 先解决“大文件和 license 不进 git、scene 只保存稳定 URI”的问题。

## 命令行验证

工具本身的验证命令是：

```bash
corepack pnpm --dir src/tools/assets-downloader test
corepack pnpm --dir src/tools/assets-downloader build
```

| 命令 | 验证内容 |
|---|---|
| `test` | catalog schema、preview plan、license gate、cache import 和 PLY manifest |
| `build` | TypeScript typecheck 和 Vite production build |

## 我们已经学会了什么

我们已经把 assets downloader 理解为一个手动联网的本地 cache 工作台：它从 catalog 或手动 URL 生成导入计划，把 raw、license、metadata 和 converted 输出写进 `.asset_cache/`，再通过 `cache://.../converted/...` 给 scene、editor 和 offline renderer 使用。

## 下一步

继续读 [Offline Renderer](../offline-renderer/index.md)，我们会看到 scene 中的资源 URI 如何进入 `OfflineAssetResolver`、`SceneResourceTable` 和离线输出链路。

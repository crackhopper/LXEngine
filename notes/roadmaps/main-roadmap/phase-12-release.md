# Phase 12 · 打包 / 发布

> **目标**：把引擎 + 游戏打成“干净机器”上能跑的包。覆盖 **Windows + Linux 桌面 + Web（WASM）** 三目标。
>
> **依赖**：所有前序 phase（至少 Phase 1 / 2 / 3 完成；Web 目标需 Phase 1 WebGPU 后端）。
>
> **可交付**：
> - `dist/win64/mygame.exe` — 双击即玩
> - `dist/linux64/mygame` — chmod +x 即玩
> - `dist/web/index.html` — 浏览器打开即玩

## 当前实施状态（2026-04-24）

**未开工**。当前运行依赖：

- `assets/shaders/glsl/` 源文件（运行期 `shaderc` 编译）
- 仓库相对路径资产（`cdToWhereAssetsExist` 启发式）
- 显式 Vulkan SDK（`VK_LAYER_PATH` 等）

| 条目 | 状态 |
|------|------|
| 资产打包格式 | ❌ REQ-1201 |
| Shader 预编译 | ❌ REQ-1202 |
| Pipeline cache 预构建 | ❌ REQ-1203 |
| 桌面 installer（Windows / Linux） | ❌ REQ-1204 |
| WASM 构建 | ❌ REQ-1205 |
| CI / CD | ❌ REQ-1206 |
| 版本化 + 自更新 | ❌ REQ-1207 |

## 范围与边界

**做**：资产打包格式（glob + 索引） / shader 离线编译到 SPIR-V + 跨后端字节码 / `PipelineCache::preload` 离线数据序列化 / Win / Linux 桌面 installer / Emscripten → WASM / CI 自动产包 / 版本号 + 可选自更新。

**不做**：数字版权保护（DRM） / Steam / EpicGames / 商店集成（游戏自身事项） / 自带运行时库更新服务。

## 前置条件

- Phase 1 完整（含 WebGPU + Headless）
- Phase 3 资产管线（GUID + 序列化）
- Phase 10 CLI（`engine-cli build` 驱动打包流程）

## 工作分解

### REQ-1201 · 资产打包格式

- `.lxpak`：多 asset 合成单文件（类 ZIP / TAR）
- 索引：`guid → (offset, size)`
- 支持加密（可选）+ 压缩（zstd）

**验收**：`engine-cli pack --input assets/ --output game.lxpak`；运行期按 guid 读取正确。

### REQ-1202 · Shader 离线编译

- 发布版不带 shaderc 运行时
- Build step 把 `assets/shaders/glsl/*` 全部预编到 `.spv` + 各后端字节码（WGSL / MSL / ...）
- 反射结果一并序列化，避免运行期 SPIRV-Cross

**验收**：发布包不含 GLSL 源 + 运行期零 shader 编译。

### REQ-1203 · Pipeline cache 预构建

- Build 期 headless 模式 `PipelineCache::preload()` 生成全部 pipeline
- 序列化到 `pipeline.cache`
- 运行期加载文件一次性 warm-up

**验收**：首次启动无 pipeline 创建 spike。

### REQ-1204 · 桌面 installer

- Windows：MSI / NSIS
- Linux：tarball + desktop file / AppImage
- 依赖库全打包（Vulkan loader / SDL3 / ImGui fonts）

**验收**：清新 Windows / Linux 机器双击 / chmod +x 运行。

### REQ-1205 · WASM 构建

- Emscripten toolchain（Phase 1 REQ-115 基础）
- 资产通过 async fetch 加载到 virtual FS
- WebGPU / WebGL2 后端（Phase 1 REQ-112 / REQ-116）

**验收**：静态 HTTP server serve `dist/web/`，Chrome 打开即玩。

### REQ-1206 · CI / CD

- GitHub Actions / GitLab CI
- 每个 tag / release 自动产 3 目标包
- Artifact 存到 releases 页

**验收**：`git tag v0.1.0` → CI 自动产 3 份包上传。

### REQ-1207 · 版本化 + 自更新（可选）

- `engine.version` 字段
- 可选 launcher 检查远程 manifest 自更新
- 或纯分发（不自更新）

**验收**：有 launcher 时能自动更新；无 launcher 时手动下载新版本 OK。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M12.1 · 资产打包 + shader 预编 | REQ-1201 + REQ-1202 | 运行期零编译 |
| M12.2 · Pipeline cache | REQ-1203 | 首帧无 spike |
| M12.3 · 桌面包 | REQ-1204 | Win + Linux 双击玩 |
| M12.4 · Web 包 | REQ-1205 | 浏览器打开玩 |
| M12.5 · CI + 版本 | REQ-1206 + REQ-1207 | tag 自动产包 |

## 风险 / 未知

- **Vulkan loader**：Windows 发布包必须捆绑或依赖系统 loader。带 `vulkan-1.dll` 最稳。
- **资产压缩率**：zstd 贴图压缩率有限（本身已有损压缩）。主要价值在音频 / 文本。
- **WASM 体积**：Emscripten 输出几十 MB 常见。启用 `-Oz` + dynamic lib 分离。
- **热更新 vs 安全**：自更新 launcher 是攻击面。首版仅离线分发。

## 与 AI-Native 原则契合

- [P-15 版本化](principles.md#p-15-重构友好--版本化)：发布包带 `engine_version` + `schema_version`，跨版本可加载 / 迁移旧资产。
- [P-18 沙箱进程](principles.md#p-18-沙箱友好的进程模型)：发布包内 `session_root/` 完全独立，删目录即重置。
- [P-20 渲染/模拟可分](principles.md#p-20-渲染与模拟可分离)：Web 构建天然 headless 友好。

## 与现有架构契合

- `PipelineCache::preload`（已落地）是 build-time 预构建入口。
- `generic_material_loader` 的 yaml 输入可在 build time 预编译后仍保留。
- `cdToWhereAssetsExist` 作 dev fallback 保留；发布版走 Phase 3 REQ-306 的显式 asset root。

## 下一步

Roadmap 闭环。后续 phase 按使用反馈决定（例如多人网络 / VR / 更高级渲染）。

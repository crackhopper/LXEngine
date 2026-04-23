# Phase 12 · 打包 / 发布

> **目标**：把引擎 + 游戏打成一个在"干净机器"上能跑的包。覆盖 **Windows + Linux 桌面 + Web（WASM）** 三个目标。
>
> **依赖**：全部前置阶段。Web 目标强依赖 Phase 1 的 WebGPU 后端路径。
>
> **可交付**：
> - `game_ship.zip`（Windows，含 engine.exe + assets.pak）
> - `game_ship.tar.gz`（Linux AppImage）
> - `game_web/`（静态文件目录，丢到任意 HTTP 服务器就能玩）

## 范围与边界

**做**：
- Resource cooking（把源资产编译成运行时格式）
- 压缩 + 打包成资源包
- Shader 预编译（把编译后的 GPU 字节码连同元数据塞进包里，运行时不再带 shader 编译器）
- 可执行 strip + 依赖审计
- Windows / Linux 桌面打包
- Web (WASM) 打包
- 崩溃上报
- 版本号 / build info 嵌入
- 默认配置 + 用户 override（config 分层）
- **Provenance-aware 打包**：生成资产的来源信息随包发布（[P-10](principles.md#p-10-资产血统--provenance)）
- **引擎 Agent 随包**：发布版可选带 agent runtime，让玩家也能与引擎对话（Phase 10 能力下沉到玩家层）

**不做**：
- macOS / iOS / Android 平台 —— 本 roadmap 聚焦 Windows + Linux + Web，移动平台另开大阶段
- 游戏商店集成（成就 / 云存档 / overlay）—— 需要单独的 SDK 接入
- 自动更新 —— OTA patch 系统是独立话题
- 代码签名 —— 需要证书与流程，另做

---

## 前置条件

- Phase 3：资产管线，能识别每种资产类型
- Phase 8：编辑器完成（开发流程稳定）
- 所有前置阶段的 demo 都跑通

---

## 工作分解

### REQ-901 · Build Info

每次构建自动嵌入：

- Git commit hash
- Git tag / version
- 构建时间 / 构建机器
- CMake target + 配置（Debug / Release / RelWithDebInfo）

实现：CMake 生成一个 `build_info.hpp`，内容来自 `configure_file`。代码里 `#include "build_info.hpp"` 就能拿到。

**验收**：启动时日志打印 `"Renderer v0.3.0-12-g5168dc1 Release 2026-05-01"`。

### REQ-1202 · Resource Cooking

把源资产转成运行时专用格式。每种 cooker 是独立模块，可注册：

| 源类别 | 目标形态 | 理由 |
|--------|---------|------|
| 网格源格式 | 二进制直载格式（顶点/索引 buffer 可直接 memcpy） | 跳过解析 |
| 图像源格式 | GPU 原生压缩纹理 | 显存 1/4 ~ 1/8 |
| Shader 源 | 已编译 GPU 字节码 + 预计算反射 | 运行时不带编译器 |
| Scene / prefab 文本 | 二进制紧凑格式 | 解析更快 |
| 音频 | 保留原格式或 resample | 通常无压缩收益 |

- 提供 `cook` 命令行工具：输入 / 输出 / 目标平台 / 增量
- Cook 过程走 Phase 3 的 asset registry（保持稳定 ID 一致）
- 增量 cooking：输入没变不重做
- Cook 结果继承源资产的 provenance 元数据（[P-10](principles.md#p-10-资产血统--provenance)）

**验收**：cook 一个典型资产目录，产物可被运行时加载；增量 cooking 不做重复工作。

### REQ-1203 · Asset Package

把 cooked 资产打成一个或几个 pack 文件：

- Header + TOC + 数据区 三段式
- 每个条目：稳定 ID / offset / size / 压缩方式 / 校验和
- 压缩算法选型：在**压缩率 / 解压速度 / License** 三维度权衡（现代无损压缩任选）

运行期资产注册表识别 pack 并把其内容注册到索引；读取时解压交给 loader，路径和开发期完全一致。

**验收**：把源资产替换成 pack 后运行时无感知，demo 照常运行。

### REQ-1204 · 运行时剥离编译器依赖

Release 构建：

- Shader 编译器 + 反射库**不进入**运行时二进制
- Shader 字节码 + 反射数据在 cook 阶段预算好，塞进 pack
- TS → JS 编译器同样 cook 期剥离
- 提供构建开关控制是否启用运行期编译（dev 模式开、release 关）

**验收**：release 包二进制大小显著下降，不依赖 shader 编译器动态库。

### REQ-1205 · Platform Audit

梳理所有平台相关路径：

- 文件系统根目录发现：遵循各平台 XDG / AppData 约定
- cwd 启发式查找替换为"相对 exe 的固定路径" + 虚拟 overlay
- 窗口图标 + 任务栏名
- DPI 感知
- 全屏 / 窗口切换跨平台一致
- 多显示器处理

**验收**：从任意目录启动打包后的二进制都能正常工作。

### REQ-1206 · Config 分层

从低到高四层覆盖：

```
引擎默认  <  打包默认（游戏作者覆盖）  <  用户配置  <  命令行参数
```

- 每个子系统（audio / input / graphics / agent）声明自己的配置 schema（契合 [P-4 单源清单](principles.md#p-4-单源能力清单capability-manifest)）
- 缺失字段用默认值
- 用户文件放在平台特定目录

**选型参考**：TOML / JSON / YAML 任选，以生态成熟度和编辑体验为准。

**验收**：删掉用户配置后重启，默认值正确；改一个字段后重启生效。

### REQ-907 · Crash Dump

- Windows：标准 SEH + minidump
- Linux：signal handler + 调用栈抓取库
- 崩溃时写 dump 到用户目录 + 打印日志
- 崩溃后对话框提示用户提交

**不做**：
- 自动上传到服务器
- 符号化后台（需要 service 后端）

**验收**：故意制造崩溃，能看到完整调用栈 + 生成 crash 文件。

### REQ-1208 · Windows 打包

- 构建系统的 install 规则把可执行 + 依赖 + cooked 资产装到目标目录
- 选择：生成安装包 或 直接 zip
- 不依赖外部运行时（用户机器不需要装 SDK / 框架）

**验收**：在干净的目标机器上解包即运行。

### REQ-1209 · Linux 打包

- 尽量静态链接
- 动态链接的依赖限于系统自带的稳定库
- 打成单文件便携镜像 或 tarball + 启动脚本
- 为最低支持的发行版版本做构建（老版本 glibc 以保证前向兼容）

**验收**：在干净的主流 Linux 发行版上跑起来。

### REQ-1210 · Web 打包

AI-Native 引擎必须有 Web 分发。Phase 1 已经搭好 Web 构建链，本阶段把它变成可分发产物：

```
game_web/
├── index.html           # 入口
├── engine.js            # 胶水
├── engine.wasm          # 引擎 + 游戏代码
├── assets.*             # cooked 资产，按需加载
├── scripts/*.js         # 预编译脚本
└── editor/              # 可选：编辑器一并带上
```

**关键点**：
- **资产按需加载**：不把所有 pack 塞进初始下载，按场景拉取
- **传输压缩**：服务器启压缩 encoding 后整体体积大幅下降
- **后端可用性检测**：原生 WebGPU 不可用时降级到 WebGL2 或显示提示
- **用户目录模拟**：用浏览器持久化存储模拟配置 / 存档
- **Crash 处理**：用 `window.onerror` 捕获 + 上报日志

**验收**：打开 URL 能正常加载并玩通 demo。

### REQ-1211 · Agent 随包发布

Phase 10 的 agent runtime 可以随发布版一起进入玩家手里（可选构建开关）：

- 在桌面版：`engine-cli --chat` 可独立启动，也可从游戏内唤出
- 在 Web 版：页面里按一个快捷键开控制台，直接和内置 agent 对话
- 对外部模型的调用由玩家自己配置 API key / endpoint（引擎作者不代付费用）
- 也可连到玩家自建的 MCP 服务器

这让 AI-Native 体验从开发阶段延伸到**玩家阶段**：玩家自己能让 agent 改游戏。

**验收**：发布版里能通过控制台让 agent 完成基础场景操作。

### REQ-1212 · Eval 回归门禁

契合 [P-17 Eval Harness](principles.md#p-17-eval-harness-是内建设施)：

每次打包前强制运行 Phase 10 的 eval 挑战集：

- Tier 1 + Tier 2 成功率必须高于基线
- 任何 tier 的成功率回退阻断发布
- 结果随版本一起留档，形成可追踪的质量指标

**验收**：eval 门禁接入发布流程，回归时能拦下。

### REQ-1213 · 最小发布清单

写一个 `RELEASE_CHECKLIST.md`，每次发版执行：

- [ ] 所有集成测试通过
- [ ] 所有 demo 能从头玩到胜利（桌面 + Web）
- [ ] 编辑器能正常开关
- [ ] release 构建无 assert / warning
- [ ] cook 后的包和源码构建结果在可验证层面一致
- [ ] 跑完三平台烟测（Windows / Linux / Web）
- [ ] MCP server 能被标准 MCP 客户端连接
- [ ] Eval 挑战集通过（[P-17](principles.md#p-17-eval-harness-是内建设施)）
- [ ] 版本号正确
- [ ] `CHANGELOG.md` 有本版变更
- [ ] git tag + 发布

**验收**：清单能走完一轮，产出 Windows + Linux + Web 三目标产物。

---

## 里程碑

### M12.1 · 构建 metadata + cooking

- REQ-1201 + REQ-1202 + REQ-1203 完成
- demo：从 cook 产物运行游戏

### M12.2 · Release 构建精简

- REQ-1204 + REQ-1205 + REQ-1206 + REQ-1207 完成
- demo：release 二进制在干净机器上跑

### M12.3 · 三平台分发 + Agent 发布 + 回归门禁

- REQ-1208 + REQ-1209 + REQ-1210 + REQ-1211 + REQ-1212 + REQ-1213 完成
- demo：Windows + Linux + Web 三目标可分发，eval 门禁接入

---

## 风险 / 未知

- **纹理压缩的 cook 成本**：高质量模式耗时数分钟级。解决：cook 并行 + 缓存。
- **Shader 变体爆炸**：多个 define 组合需要预算。先约束 shader 作者少用 variant。
- **崩溃上报的隐私**：本地写 dump 没问题。上传要用户同意。
- **Linux 发行版差异**：glibc 版本是最常见的坑。在老版本 glibc 的容器里编包。
- **未签名二进制的安全提示**：平台会告警。本阶段不解决签名，README 里告知用户。
- **WASM 体积**：未优化版本可能数十 MB。解决：优化级别 + wasm-opt + 传输压缩。
- **Web 上的 CORS**：资产跨域被拒。解决：同源部署或配置 CORS header。
- **浏览器兼容性**：新 GPU API 在部分浏览器不完整。先声明支持主流浏览器，其他降级。

---

## 与现有架构的契合

- 资产注册表（Phase 3）已有"按稳定 ID / 按 path 查"的抽象，加一个 pack 来源插进去即可。
- Shader 编译器已经是 infra 层的实现，剥离编译器只需要换实现。
- `PipelineCache::preload`（现状）可以继续在 release 构建里跑，结果一样，因为 `PipelineBuildDesc` 是数据，不依赖运行期反射（反射可预烘焙）。
- `IWindow` 的平台差异已经由 SDL3 吸收掉了，Phase 9 没有新增"平台"工作，只有"打包"工作。
- Build info 通过 CMake 生成，和项目现有的 `shaders/CMakeLists.txt` 用 `configure_file` 是同一套做法。

---

## 收尾

到此为止，从"Vulkan 教学渲染器"到"**AI-Native 小型游戏引擎**"的完整路径走完。

**全流程 demo 链**：
- `demo_pbr_shadow_ibl` (Vulkan) / `demo_pbr_web` (WebGPU) — 渲染深度 + Web 后端
- `demo_transform_input` + `engine-cli dump-scene` — 基础层 + 文本内省
- `demo_scene_save_load` — 资产
- `demo_animated_character` — 动画
- `demo_physics_pong` — 物理
- `demo_first_game`（TypeScript 驱动）— Gameplay
- + 音效（Phase 7）+ Vue UI（Phase 8）
- `editor/index.html` — Web 编辑器
- `engine-cli --chat` / `engine-cli --mcp-stdio` — AI Agent + MCP
- AI-generated 场景资产（贴图 / 模型 / 动画 / splat）
- `game_ship.zip` / `game_ship.AppImage` / `game_web/` — 三目标发布

每个 demo 都是一个 commit-able 里程碑，能独立验证、独立 revert。

---

## AI-Native 验收准则

一个合格的 AI-Native 小型游戏引擎要满足（对应 [principles.md](principles.md) 的每条原则至少有一条落实）：

- [ ] **P-1 确定性**：`--deterministic` 模式下同输入产同输出，跨 session 可复现
- [ ] **P-2 事件流**：任意历史点可 replay
- [ ] **P-3 三层 API**：Query / Command / Primitive 清晰分离，agent 只能触达前两层
- [ ] **P-4 单源能力清单**：新增一条命令自动同步 MCP schema / TS 类型 / 编辑器 UI / 文档
- [ ] **P-5 语义查询**：agent 能按组件/标签/空间关系查节点
- [ ] **P-6 Intent Graph**：Agent 执行轨迹可视化 + 可回滚
- [ ] **P-7 多分辨率观察**：summary / outline / full 三档
- [ ] **P-8 Dry-run**：所有命令支持 preview
- [ ] **P-9 成本模型**：每个命令估算 token / time / money
- [ ] **P-10 Provenance**：每份资产记录来源
- [ ] **P-11 时间旅行**：任意历史状态可查询
- [ ] **P-12 错误即教学**：所有错误带 fix_hint + agent_tip
- [ ] **P-13 HITL 契约**：confirm / review 级别被类型系统强制
- [ ] **P-14 能力发现**：agent 运行期问"我能做什么"
- [ ] **P-15 版本化**：schema 迁移链透明工作
- [ ] **P-16 多模态**：文本 + 截图 + profile + 音频 dump 共存
- [ ] **P-17 Eval Harness**：benchmark 接入 CI
- [ ] **P-18 沙箱进程**：可 fresh start
- [ ] **P-19 命令总线**：编辑器 / agent / CLI 共享同一套 API
- [ ] **P-20 渲染/模拟可分离**：headless 模式完整可用

全部 ✓ 时，引擎达到 **AI-Native 1.0**。这 20 条就是整套 roadmap 的**最终验收依据**，而不是某一个阶段的 checklist。

---

## 长期方向（超出本 roadmap）

本 roadmap 的**不**做清单合集，留给下一个 roadmap：

- 移动端 / 主机
- 在线多人 / 回滚网络代码
- 虚拟纹理 / mesh shader / ray tracing
- GI 烘焙管线
- Agent 多人协作（A agent 写代码 + B agent 评审 + C agent 测试）
- 自训 / fine-tune 游戏专用小模型
- 可视化脚本（蓝图）
- Asset 版本迁移
- 自动化 CI + release pipeline
- Steam / Epic / itch.io 发布
- 本地化 / 配音
- 社区工具（mod support）

每一条都可以单独展开成一个"Phase 13+"。

← [README](README.md)

# Phase 7 · 音频

> **目标**：给游戏加上声音 — 2D / 3D 音效、背景音乐、混音总线。
>
> **依赖**：Phase 2（`Clock` + 命令层）+ Phase 3（音频资产加载）。与 Phase 4 / 5 / 6 独立。
>
> **可交付**：
> - `demo_audio` — 相机移动时 3D 音源距离衰减 + 背景音乐 loop
> - 音频由 TS 脚本（Phase 6）驱动 play / stop

## 当前实施状态（2026-04-24）

**未开工**。无任何音频基础设施。

| 条目 | 状态 |
|------|------|
| 音频引擎接入 | ❌ REQ-701 |
| 2D / 3D 音源 | ❌ REQ-702 |
| 混音总线 | ❌ REQ-703 |
| 音频热重载 | ❌ REQ-704 |

## 范围与边界

**做**：音频引擎（miniaudio 等） / 2D + 3D 音源组件（距离 / 方向衰减） / 混音总线（master / music / sfx / voice） / 音频资产热重载。

**不做**：DSP 自实现（reverb / EQ / pitch shift） / 声音物理（echo / occlusion 首版不做） / 长音乐流式解码（Phase 12 前再评估）。

## 前置条件

- Phase 2 `Clock` + 命令层
- Phase 3 音频 asset（`.wav` / `.ogg`）加载

## 工作分解

### REQ-701 · 音频引擎接入

选型参考（可替换）：

- **[miniaudio](https://miniaud.io/)**：单头文件 / MIT / 跨平台 / Web 可编译；首版默认
- FMOD / wwise：商业级，不适合 MIT 项目

抽象 `IAudioEngine` + `IAudioSource`，实现放 `src/infra/audio/`。

**验收**：加载 `.wav` 并 play，能听到声音。

### REQ-702 · 2D / 3D 音源组件

- `AudioSource` 组件挂在 `SceneNode` 上
- 2D：全景相等
- 3D：按 `Transform::worldPosition` 与监听者（通常是相机）计算距离 / 方向衰减

**验收**：相机靠近音源时音量增大，绕过时左右声道切换。

### REQ-703 · 混音总线

- Master / Music / SFX / Voice 四条默认总线
- 每条 bus 独立 volume / mute / solo
- 暴露给 TS 脚本 + debug 面板

**验收**：暂停时 SFX 静音，Music 不受影响。

### REQ-704 · 音频热重载

- `.wav` / `.ogg` 改动 → 重新加载 → 正在播放音源按需重 decode

**验收**：改 `.wav` 保存，正在循环播放的背景音下一 loop 切到新版本。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M7.1 · 引擎接入 | REQ-701 | `.wav` 能播放 |
| M7.2 · 3D 音源 | REQ-702 | 距离衰减正确 |
| M7.3 · 混音总线 | REQ-703 | 分 bus 调音量 |
| M7.4 · 热重载 | REQ-704 | 音频改动即刻生效 |

## 风险 / 未知

- **跨平台设备枚举**：Web / Linux / Windows 音频设备 API 各异，miniaudio 已封装。
- **延迟**：Web 上 AudioContext 需用户手势解锁；需显式 prompt。
- **音频与物理同步**：事件触发的 SFX 应走事件总线（Phase 6 REQ-604），避免脚本轮询。

## 与 AI-Native 原则契合

- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：`audio.dump_active_voices()` 让 agent 看到“谁在发声”。
- [P-19 命令总线](principles.md#p-19-双向命令总线)：play / stop / setVolume 是命令，可被 agent / 脚本 / 编辑器共用。

## 与现有架构契合

- `SceneNode` + `Transform`（Phase 2）提供 3D 音源位置源。
- `EngineLoop::setUpdateHook`（已落地）驱动 audio listener 位置更新。

## 下一步

[Phase 10 MCP + Agent](phase-10-ai-agent-mcp.md)：`audio.play` 等命令自动暴露为 MCP tool。

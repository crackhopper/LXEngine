# Phase 7 · 音频

> **目标**：给游戏加上声音 —— 2D/3D 音效、背景音乐、混音总线。
>
> **依赖**：Phase 2（Time）、Phase 3（资产）、Phase 6（组件）。
>
> **可交付**：`demo_first_game` 里的脚步声 / 射击音效 / 循环背景音乐。

> 注：在 AI-Native 版本的 roadmap 中，原 Phase 7（"音频 + UI"）被拆分为本阶段（纯音频）和 [Phase 8 · Vue UI 容器](phase-8-web-ui.md)。UI 是一个足够大的独立话题，值得单独一个阶段。

## 范围与边界

**做**：
- 音频播放（2D + 3D 空间化）
- 音频资产（wav / ogg / mp3）
- 音效池（一次多个并发）
- 音乐流式加载
- 音频总线（bus / group / mixer）
- `playOneShot` 快捷通道

**不做**：
- DSP 效果链（reverb / filter / EQ） — 中等规模才需要
- 音频烘焙 / 流式资源包 — Phase 12 处理
- 自研音频引擎 — 用 miniaudio

---

## 前置条件

- Phase 3：`AudioClip` 是 asset
- Phase 6：`AudioSource` 是 Component

---

## 工作分解

### REQ-701 · 音频设备抽象 + 第三方库接入

- `src/core/audio/audio_device.hpp` 定义 `IAudioDevice` 接口
- `src/infra/audio/<vendor>_device.cpp` 是具体实现，基于一个**跨平台音频库**
- 设备初始化 + 关闭 + 主音量

**选型参考**：优先挑选**单文件 / 零依赖 / 覆盖桌面+移动+Web 全平台**的库。评估标准包含 License、编解码支持、延迟、跨平台覆盖面。

**验收**：启动设备，播放一段正弦波一秒。

### REQ-702 · AudioClip 资源

```cpp
class AudioClip {
public:
    StringID name;
    int      channels;
    int      sampleRate;
    int      frameCount;
    std::vector<float> pcm;     // 或延迟解码的压缩数据
    bool     streaming = false; // 大文件流式加载
};
```

- wav / ogg / mp3 的 decode 由底层音频库完成
- 短音效：完整 decode 到内存
- 长音频（BGM）：`streaming = true`，按 chunk 解码
- 走 Phase 3 的 asset registry，有 GUID

**验收**：加载 wav 和 ogg，PCM 数据正确。

### REQ-703 · AudioSource 组件

```cpp
class AudioSource : public Component {
    LX_COMPONENT(AudioSource)
    LX_FIELD(AudioClipHandle, clip, {})
    LX_FIELD(float, volume, 1.0f)
    LX_FIELD(float, pitch,  1.0f)
    LX_FIELD(bool,  loop,   false)
    LX_FIELD(bool,  playOnStart, false)
    LX_FIELD(bool,  is3D,   false)
    LX_FIELD(StringID, bus, "sfx")

    void play();
    void stop();
    void pause();
};
```

- 2D 模式：直接混音到指定 bus
- 3D 模式：使用 `Transform` 位置 + `AudioListener`（挂在相机上）计算距离衰减 + pan

**验收**：场景里一个 3D AudioSource，相机绕它走时立体声左右变化。

### REQ-704 · 音频混音器

```cpp
class AudioMixer {
public:
    struct Bus {
        StringID name;
        float volume = 1.0f;
        bool  muted = false;
        std::optional<StringID> parent;
    };

    Bus& getBus(StringID name);
    void setBusVolume(StringID name, float v);
};
```

- 默认 bus：`master` → `music` / `sfx` / `voice`
- 每个 AudioSource 属于某个 bus
- 层级 bus：子 bus 的最终音量 = 自身 × 所有父 bus

**验收**：`setBusVolume("music", 0)` 后音乐静音、sfx 不受影响。

### REQ-705 · 一次性播放

```cpp
// 不想为每次播放都创建一个 AudioSource 时用
engine->audio().playOneShot(clipHandle, position, volume);
```

- 内部维护一个 `std::vector<ActiveVoice>`，播完自动回收
- 上限 32 个并发 voice，超出时按优先级抢占

**验收**：连续触发 100 次脚步声，voice pool 不增长。

### REQ-706 · 引擎侧内省

和 Phase 2 的文本内省契合：

```cpp
std::string dumpAudio(std::string_view format = "json");
// 输出：当前活跃 voice / 每个 bus 的 volume / 加载中的 streaming clips
```

让 AI agent 通过 MCP 可以问 "现在在播什么声音 / 音量怎么样"。

**验收**：`engine-cli dump audio` 输出结构化音频状态。

---

## 里程碑

### M7.1 · 音效能响

- REQ-701 + REQ-702 + REQ-703 完成
- demo：角色跳跃播放音效

### M7.2 · 音乐 + 混音

- REQ-704 + REQ-705 完成
- demo：背景音乐 + 按键调音量

### M7.3 · Agent 可查询音频状态

- REQ-706 完成
- demo：Phase 10 的 agent 能通过 MCP tool 查当前音频状态

---

## 风险 / 未知

- **音频库延迟调优**：默认 buffer 大小通常偏大（几十毫秒），对节奏游戏敏感。调小会吃 CPU 抖动风险。
- **Web 后端下的自动播放限制**：浏览器要求用户交互后才允许播放音频。第一次播放必须在用户事件回调里触发。引擎提供一个 "audio unlock" hook。
- **压缩格式的跨平台支持**：部分编解码器在 WASM 构建下表现不同，需要按目标平台选择。

---

## 与现有架构的契合

- AudioSource / AudioListener 都是 `Component` 子类（Phase 6），没有特殊路径。
- AudioClip 是 asset（Phase 3），走标准资源路径 + provenance 元数据。
- 混音器的 bus 状态暴露到 Phase 2 文本内省 API。
- 所选音频库应覆盖桌面 + 移动 + Web 全平台，无缝适配 Phase 12 的 WASM 目标。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-7 多分辨率观察](principles.md#p-7-多分辨率观察--渐进披露) | REQ-706 提供 summary/outline/full 三档音频状态 dump |
| [P-16 多模态](principles.md#p-16-文本优先--文本唯一) | 音频本身是独立感官通道，dump 格式是文本 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | `audio.play` / `audio.setBusVolume` 走命令层 |
| [P-20 渲染/模拟可分](principles.md#p-20-渲染与模拟的可分离) | 音频可 headless，eval 不需要声卡 |

---

## 下一步

[Phase 8 · Vue UI 容器](phase-8-web-ui.md) —— 游戏内的 UI 怎么做。

# Phase 7 · Audio

> 目标：提供最小但完整的游戏音频能力：加载、播放、空间化、mixer、事件驱动。

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | audio asset import：wav/ogg |
| 2 | `AudioSourceComponent` |
| 3 | mixer group：music/sfx/ui |
| 4 | 3D spatialization |
| 5 | command/script API |
| 6 | editor preview |

## 边界

首版不做 DAW 级编辑、不做复杂 DSP 图、不做网络语音。音频事件要能进入 Phase 10 的 query/dump，让 agent 能知道“当前有哪些 voice 正在播放”。

## 继续阅读

- [Phase 3 · Asset Pipeline](phase-3-asset-pipeline.md)
- [Phase 6 · Gameplay](phase-6-gameplay-layer.md)

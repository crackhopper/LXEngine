# Phase 8 · Web UI

> 目标：给游戏内 UI 提供 HTML/Vue 子集，而不是把 ImGui 当玩家 UI。

## 方向

| 主题 | 决策 |
|---|---|
| UI 表达 | HTML-like markup + CSS 子集 + Vue-like reactivity |
| 运行位置 | 桌面先嵌入 runtime，Web 后端复用浏览器能力 |
| Agent 友好性 | UI 文件可文本编辑、可 diff、可由 AI 生成 |
| 与 ImGui 关系 | ImGui 继续做 editor/debug UI，不做玩家 UI |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | UI asset 格式和 loader |
| 2 | layout/render backend 选型 |
| 3 | data binding：game state → UI |
| 4 | input event：click/hover/focus |
| 5 | style/theme |
| 6 | Phase 11 生成 UI 的校验器 |

## 继续阅读

- [Phase 6 · Gameplay](phase-6-gameplay-layer.md)
- [Phase 11 · AI Asset Generation](phase-11-ai-asset-generation.md)

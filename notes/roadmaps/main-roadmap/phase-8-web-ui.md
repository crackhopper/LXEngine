# Phase 8 · Vue UI 容器

> **目标**：给引擎提供**面向玩家**的 UI 系统 — 采用 **HTML + JavaScript + Vue 子集** 技术栈。LLM 对前端栈的掌控力远超专有游戏 UI；作为 AI-Native 引擎的天然选择。
>
> **依赖**：Phase 1（WebGPU / WebGL2 后端提供画布） + Phase 6（Gameplay TS 驱动 UI）。与 Phase 1 / 2 三路并行可行。
>
> **可交付**：
> - `demo_hud` — HUD 显示血量 / 得分 / 子弹数，数据绑定 TS 状态
> - `demo_menu` — 带动画的主菜单 / 暂停菜单

## 当前实施状态（2026-04-24）

**未开工**。无玩家向 UI 系统（ImGui 仅是开发者 debug UI）。

| 条目 | 状态 |
|------|------|
| HTML/CSS 子集渲染器 | ❌ REQ-801 |
| Vue 子集 reactivity | ❌ REQ-802 |
| 事件路由 | ❌ REQ-803 |
| UI 资产（`.vue` / `.html`）加载 | ❌ REQ-804 |
| TS 数据绑定 | ❌ REQ-805 |

## 范围与边界

**做**：HTML + CSS 子集布局 / reactive 数据绑定（Vue composition API 风格） / 事件路由（click / hover / key） / UI 资产加载（`.vue` / `.html`） / TS 状态绑定（Phase 6 组件）。

**不做**：完整 Vue / React 运行时 / SSR / 复杂动画（先做 CSS transition） / 与外部 web 框架交互（打包进引擎内嵌）。

## 前置条件

- Phase 1 Web 后端（WebGPU / WebGL2）提供画布
- Phase 6 TS runtime（UI 行为由脚本驱动）

## 工作分解

### REQ-801 · HTML/CSS 子集渲染器

- 纯 C++ 实现 HTML 子集解析器 + flexbox 布局
- 或嵌入 [litehtml](http://www.litehtml.com/) / [Ultralight](https://ultralig.ht/)
- UI 渲染走引擎 2D 图元（无独立后端）

**验收**：`<div style="display:flex">...</div>` 正确布局渲染。

### REQ-802 · Vue 子集 reactivity

- `ref` / `computed` / `watch` 响应式
- `v-bind` / `v-if` / `v-for` 指令
- 单文件组件 `.vue`（`<template>` + `<script setup>` + `<style>`）

**验收**：变量变化 → DOM 自动更新。

### REQ-803 · 事件路由

- click / hover / key 从 `IInputState`（Phase 2）路由到 UI
- UI 消耗事件后 gameplay 不再收到

**验收**：UI 按钮点击不会同时触发场景射击。

### REQ-804 · UI 资产加载

- `.vue` / `.html` 作 Phase 3 一等 asset
- 编译到中间表示（AST / 字节码）

**验收**：改 `.vue` 保存 → 热重载到运行中的 UI。

### REQ-805 · TS 数据绑定

- UI state 与 Phase 6 TS 互通
- reactive 读 TS 变量；UI 事件写 TS 变量 / 调 TS 函数

**验收**：HUD 血量绑定 TS 中的 `player.hp`，受击时 UI 自动更新。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M8.1 · HTML / CSS 渲染 | REQ-801 | 静态页面显示 |
| M8.2 · Vue reactivity | REQ-802 | 响应式 UI |
| M8.3 · 事件 + 资产 | REQ-803 + REQ-804 | UI 资产热重载 |
| M8.4 · TS 绑定 | REQ-805 | HUD 与 TS 状态同步 |

## 风险 / 未知

- **HTML 子集选型**：litehtml 支持面够；自写成本高。
- **性能**：reactivity 每帧全量 diff 过慢。首版脏标记 + DOM patch。
- **WebGPU 接入**：UI 画到引擎 render target，tone map 之后叠加。
- **文本渲染 / i18n**：CJK 字体子集化 + 字形缓存。默认 MSDF atlas。

## 与 AI-Native 原则契合

- [P-4 Capability Manifest](principles.md#p-4-单源能力清单)：UI 组件 schema 与 TS 类型同源。
- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：UI 结构可 dump 为 HTML-like 文本；agent 可阅读。
- [P-19 命令总线](principles.md#p-19-双向命令总线)：UI 操作走命令总线（打开菜单 / 暂停游戏）。

## 与现有架构契合

- Phase 2 `IInputState` 是 UI 事件源。
- Phase 6 TS runtime 是 UI 行为源。
- Phase 1 postprocess pass 之后叠加 UI 层。

## 下一步

[Phase 9 Web 编辑器](phase-9-web-editor.md)：编辑器 UI 复用本 phase 的 Vue 容器。

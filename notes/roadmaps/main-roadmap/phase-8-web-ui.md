# Phase 8 · Vue UI 容器

> **目标**：为引擎提供一套**面向玩家**的 UI 系统，采用 **HTML + JavaScript + Vue 子集** 的技术路线。AI 对这一技术栈的掌控力远超任何传统游戏 UI，因此是 AI-Native 引擎的天然选择。
>
> **依赖**：Phase 1（Web 后端路径已打通，HTML 容器能和 GPU surface 合成）、Phase 6（TypeScript 运行时）。
>
> **可交付**：
> - `demo_ui_menu` —— 游戏进来显示一个 Vue 写的主菜单（标题 / 按钮 / 滑条），点击开始后消失、运行时 HUD 显示分数和血条
> - AI 能通过 `engine-cli` "给我改一下这个按钮的文字变成'退出游戏'" 这类指令直接改 `.vue` 文件

## 为什么走 HTML + Vue 而不是自研 UI

AI-Native 引擎的核心约束之一是 **LLM 对技术栈的掌控力**。对比几种常见游戏 UI 方案：

| 方案 | AI 掌控力 | 运行时开销 | 美术资产 |
|------|---------|----------|---------|
| 自研 retained-mode UI（Unity UGUI 风） | 低 | 低 | 需要 sprite 工具链 |
| 节点图 / 可视化脚本 | 极低 | 中 | 需要编辑器 |
| ImGui（immediate） | 中（纯代码） | 低 | 无 |
| **HTML + CSS + Vue** | **极高** | 中 | 直接用 CSS |
| Flutter / SwiftUI 风 | 中 | 中 | 需要工具链 |

AI 对 **HTML + CSS + Vue / React** 的训练语料密度极高 ——  一次 LLM 请求就能产出可运行的完整组件。这条路径让 agent 写 UI 的成本几乎为零。

代价：
- 需要一个轻量 HTML/CSS 渲染路径（不是整个浏览器引擎）
- Vue 只支持子集（template + reactivity，不包括 SSR、路由、完整的编译器）
- 和 GPU 渲染的合成需要设计

收益压倒代价的关键点：**用户生成的 UI 代码 ≈ LLM 生成的 UI 代码**。这对人类开发者也不是坏事。

## 范围与边界

**做**：
- 轻量 HTML 子集解析（block / inline / 常见 tag）
- CSS 子集（flex 布局 / 颜色 / 字体 / 内边距 / 边框）
- Vue 模板子集（`{{ expr }}` / `v-if` / `v-for` / `v-model` / `@click` / `:prop`）
- Reactive 系统（`ref` / `reactive` / `computed` / `watchEffect`）
- 与 GPU 渲染的合成：把 UI 最终画面合到主 color target 上
- 字体渲染（stb_truetype）
- 事件系统（点击 / hover / focus）
- UI ↔ TypeScript 脚本通路（UI 读写组件状态）

**不做**：
- 完整 CSS（grid / animations / media queries / pseudo-classes 只支持最小集）
- 完整 Vue（不支持 SFC 的 `<style scoped>` 后处理、不支持 SSR、不支持 Suspense / Teleport）
- JavaScript DOM API（没有 `document.querySelector` 这种）
- WebGL / Canvas2D 在 UI 里跑 —— UI 只负责 UI，3D 走主渲染路径
- 嵌入 CEF / Ultralight / Servo —— 体量太大

---

## 前置条件

- Phase 1：引擎能把一张 CPU-rendered texture 合成到 color target
- Phase 6：QuickJS-NG 运行时已就位 + TS 编译链可用

---

## 工作分解

### REQ-801 · HTML/CSS 子集解析

- 输入：字符串 HTML 源码
- 输出：一棵 DOM-like 树（`UiNode`）+ 样式（计算后的 `UiStyle`）
- 支持的 HTML 标签：基础块级 / 行内 / 表单元素的最小集
- 支持的 CSS 属性：布局（flex 为主）/ 盒模型 / 常见外观 / 字体 / 绝对定位

**选型参考**：使用一个嵌入友好、纯 C/C++ 的 HTML/CSS 解析库；自研解析器通常收益很小、风险较大。

**验收**：简单 flex 布局 + 嵌套样式正确解析、计算出每个节点的 rect。

### REQ-802 · 布局引擎

- 接入一个成熟的 **flexbox 布局库**
- 输入：UiNode 树 + 计算过的 style
- 输出：每个节点的最终 rect（x, y, w, h）
- 响应式：窗口大小变化时重新布局

**选型参考**：业界有久经考验的 C/C++ flex 实现，License 友好，已被多个框架复用。自写 flex 引擎通常得不偿失。

**验收**：嵌套三层的 flex 布局，resize 窗口后所有节点 rect 正确。

### REQ-803 · UI 渲染

UI 图元都是矩形 + 图片 + 文本。渲染方案：

- 为 UI 开一个独立的 `Pass_UI`（正交投影，深度关闭）
- 每帧从 UiNode 树构建 batched vertex buffer（动态）
- Draw call 合批：相同材质的相邻节点一次提交
- 文本：stb_truetype 产生字体图集（Phase 3 的 asset），每个字符是一个 quad
- 图片：走 `Texture` + `CombinedTextureSampler` 标准路径

**验收**：一个 `<div><h1>Title</h1><button>Play</button></div>` 能正确显示在窗口中央。

### REQ-804 · Vue 模板子集

实现一个小型 Vue 编译器（TypeScript 写，运行在 QuickJS 里）。输入 Vue template 字符串，输出 render function：

```html
<!-- 输入 -->
<template>
  <div class="menu">
    <h1>{{ title }}</h1>
    <button @click="start">Start</button>
    <button @click="quit" v-if="!isRoot">Quit</button>
  </div>
</template>
```

```js
// 输出（由编译器产出，运行期执行）
function render(ctx) {
  return h('div', { class: 'menu' }, [
    h('h1', {}, [ctx.title]),
    h('button', { onClick: ctx.start }, ['Start']),
    ctx.isRoot ? null : h('button', { onClick: ctx.quit }, ['Quit']),
  ]);
}
```

支持的指令：`v-if` / `v-else` / `v-for` / `v-model` / `v-bind` (`:attr`) / `v-on` (`@event`) / 文本插值 `{{ expr }}`。

不支持：`v-slot`（先不做 slot）、`v-memo`、自定义指令。

**验收**：上面的模板渲染出的 UiNode 树与手写的 `h()` 调用一致。

### REQ-805 · Reactivity System

实现一个迷你版的 Vue 3 reactivity：

```typescript
const state = reactive({ count: 0 });
const doubled = computed(() => state.count * 2);

watchEffect(() => {
    console.log(doubled.value);  // 自动追踪依赖
});

state.count++;  // 触发 watchEffect 重跑
```

实现要点：
- `reactive()`：Proxy 拦截 get/set
- `ref()`：单值 box
- `computed()`：惰性求值 + 依赖追踪
- `watchEffect()`：runtime 推进的 effect
- 脏节点收集 → 下一帧批量重渲染

**验收**：经典的"点击 +1 计数器"demo 能增量更新，不整棵重绘。

### REQ-806 · Component 系统

Vue 组件 = 模板 + setup 函数 + 样式：

```typescript
// ui/MainMenu.vue.ts  (由 .vue SFC 编译得到)
export default defineComponent({
    props: { title: String },
    setup(props) {
        const start = () => engine.events.emit('game:start', {});
        const quit  = () => engine.events.emit('game:quit', {});
        return { start, quit };
    },
    template: `<div class="menu"> ... </div>`,
});
```

- 组件可嵌套
- props 透传
- `<style>` 范围化（加前缀 class）

**验收**：主菜单由 3 个嵌套组件组成，状态变化自动更新。

### REQ-807 · UI ↔ Engine 通信

UI 运行在 QuickJS 里，需要和引擎状态互通：

- UI 可以订阅引擎事件：`engine.events.on('health:changed', (h) => healthRef.value = h)`
- UI 可以修改组件字段：`node.getComponent('Health').current = 80`
- UI 可以触发命令：`engine.exec('scene.createNode', {...})`
- 走的是 Phase 2 REQ-213 的命令 API 和 Phase 6 的 TS binding

**验收**：HUD 的血条自动跟随 PlayerHealth 组件变化，不需要 UI 代码手动 poll。

### REQ-808 · SFC Tooling

为了让 `.vue` 文件能被 LLM 和人类开发者自然编辑，支持单文件组件形态：

```vue
<!-- ui/HUD.vue -->
<template>
  <div class="hud">
    <div class="healthbar" :style="{ width: hp + '%' }"></div>
    <span class="score">{{ score }}</span>
  </div>
</template>

<script setup lang="ts">
import { ref } from '@engine/reactivity';
const hp = ref(100);
const score = ref(0);
engine.events.on('damage', (e) => hp.value -= e.amount);
engine.events.on('score', (e) => score.value += e.delta);
</script>

<style>
.hud { position: absolute; top: 10px; left: 10px; }
.healthbar { height: 20px; background: red; }
.score { font-size: 24px; color: white; }
</style>
```

- 构建期：用官方的 SFC 编译器或自写的轻量版本把 `.vue` 拆成 template / script / style 三部分
- template → render function
- script → ES module
- style → 全局 CSS（加 scope hash 前缀）
- 与 Phase 6 REQ-611 的 TS 编译链共用同一套快速编译器

**验收**：一个 `.vue` 文件经过 cook 后产生 `.js`，引擎运行期加载。

### REQ-809 · UI 的引擎内省

对 UI 树本身提供 dump：

```bash
engine-cli dump ui --format=tree
```

输出：

```
UiRoot
├── <div class="menu">
│   ├── <h1>{{ 'Main Menu' }}</h1>
│   └── <button @click="start">Start</button>
└── <div class="hud" hidden>
    ├── <div class="healthbar" style="width:80%">
    └── <span class="score">120</span>
```

让 AI agent 不用截图就知道 UI 当前是什么样子。

**验收**：与 DOM inspector 显示的结构一致。

---

## 里程碑

### M8.1 · HTML 渲染能跑

- REQ-801 + REQ-802 + REQ-803 完成
- demo：一个硬编码的 HTML 字符串能在窗口上显示

### M8.2 · Vue 模板能编译

- REQ-804 完成
- demo：手写 template 字符串能渲染

### M8.3 · 响应式更新

- REQ-805 + REQ-806 完成
- demo：经典 counter demo

### M8.4 · 与引擎通信 + SFC

- REQ-807 + REQ-808 完成
- demo：一个 `.vue` 的 HUD 文件 + 一个主菜单 SFC 全部能跑

### M8.5 · Agent 可读 UI 树

- REQ-809 完成
- demo：CLI dump UI 树

---

## 风险 / 未知

- **HTML/CSS 子集的边界难以清晰划定**：一旦用户写出子集之外的 CSS，行为未定义。解决：明确文档的 "支持列表"，不在列表里的静默忽略 + 警告。
- **Vue 编译器的体量**：完整的官方编译器体量不小，塞进嵌入式 JS 引擎可能慢。解决：构建期 ahead-of-time 编译 `.vue` → `.js`，运行时不携带编译器。
- **Reactivity 的内存开销**：Proxy 代理会产生额外 GC 压力。嵌入式 JS 引擎的 GC 行为对短命对象友好，对长命大对象相对弱。
- **合成顺序**：UI pass 要在 post-process 之后、gamma 之前还是之后？先定规则："UI 在 sRGB 空间画，永远在最后一步 present 前合成"。
- **样式系统的作用域**：全局 CSS 会相互污染。先用 hash 前缀的 scope 方案，不做 CSS-in-JS。

---

## 与现有架构的契合

- UI pass 是一个普通的 `Pass_UI` → `FramePass`，走和其他 pass 一样的 `FrameGraph` 构建 + `PipelineCache` 预加载路径。
- UI 材质是反射驱动的普通 material，`setTexture` / `setFloat` 接口直接可用。
- Phase 6 的脚本 runtime 被 UI 子系统复用。
- 字体图集是 `Texture` 资产（Phase 3），走标准资源路径。
- `.vue` 文件被 Phase 3 的 asset registry 识别为一种资产类型。
- UI 事件与 Phase 6 的事件总线共用 —— UI 组件可以直接订阅游戏事件。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| **技术栈选择本身**：HTML+Vue 就是为了让 LLM 能直接生成可运行 UI 代码 |
| [P-7 多分辨率](principles.md#p-7-多分辨率观察--渐进披露) | UI 树有 summary / outline / full 三档 dump |
| [P-16 多模态](principles.md#p-16-文本优先--文本唯一) | UI 树的文本 dump 比截图对 agent 更有效 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | UI 事件处理器调用的是引擎命令，不是直接写组件字段 |

---

## 下一步

有了 Web 后端（Phase 1）+ 文本内省（Phase 2）+ Vue UI（本阶段），做一个**跑在浏览器里的编辑器**就水到渠成 → [Phase 9 · Web 编辑器](phase-9-web-editor.md)。

# Phase 12 · Release

> 目标：把引擎和项目打成可分发包：桌面优先，Web 目标在 Web 后端成熟后进入。

## 当前发布阻塞

| 阻塞 | 当前状态 |
|---|---|
| 资产查找 | 仍大量依赖 runtime path / dev layout |
| Shader 编译 | 当前运行期编译 GLSL |
| Pipeline warmup | 有 `PipelineCache::preload`，但无发布期预构建流程 |
| 依赖打包 | Vulkan/SDL/资源目录未成发布规范 |
| Web target | 后端未开始 |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | desktop dev package：明确 asset root |
| 2 | shader offline compile + reflection cache |
| 3 | asset pack index |
| 4 | pipeline warmup data |
| 5 | Windows/Linux bundle |
| 6 | CI artifact |
| 7 | Web/WASM package |

## 与 Phase 1 的关系

FrameGraph / shadow / G-Buffer 会增加 shader、pipeline 和 attachment 组合数量。发布流程必须能离线扫描 material/pass，提前编译 shader 和 warmup pipeline，否则复杂场景首次启动会有明显 spike。

## 继续阅读

- [Phase 1 · Rendering Depth](phase-1-rendering-depth.md)
- [Phase 3 · Asset Pipeline](phase-3-asset-pipeline.md)
- [Pipeline Cache 子系统](../../subsystems/pipeline-cache.md)

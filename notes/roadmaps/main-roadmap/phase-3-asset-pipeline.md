# Phase 3 · Asset Pipeline：资产身份与热重载

> 目标：把当前“路径 + loader”升级成 AssetRegistry、`.meta`、GUID、热重载和发布可用的 asset root。

当前代码已经能加载 `.scene.yaml`、`.material`、mesh、texture，也有内置资产目录约定。Phase 3 的重点不是再写一个 loader，而是给资产稳定身份和编辑器/agent 可查询的注册表。

## 当前缺口

| 缺口 | 影响 |
|---|---|
| Asset GUID / `.meta` | 场景引用、热重载、生成资产无法稳定追踪 |
| AssetRegistry | editor 资产面板、CLI 查询、MCP tools 没有统一数据源 |
| Runtime asset root | 发布包不能依赖 cwd 启发式 |
| Shader/material/texture hot reload | 调试材质和 shadow/G-Buffer shader 效率低 |
| Import pipeline | OBJ/MTL、glTF、HDR、生成资产需要统一入口 |
| Provenance | AI 生成资产和手工导入资产无法记录来源 |

## 实施顺序

| 顺序 | 主题 | 说明 |
|---|---|---|
| 1 | AssetRegistry v1 | 扫描 runtime assets，列出 scene/material/model/texture/shader |
| 2 | `.meta` + GUID | 新资产生成稳定 id；保留 path fallback |
| 3 | Hot reload bridge | shader/material/texture 改动触发资源重建 |
| 4 | Import API | 统一模型、纹理、材质、HDR 导入入口 |
| 5 | Provenance | 生成/导入来源写入 metadata |

## 与 pending REQ

`REQ-044-c` 是本 phase 的 pending 最小桥接入口：先服务 editor asset list 和 hot reload，不要求一口气 GUID 化所有 runtime 资源。它不进入 0.2.0-pre 基线，也不进入默认 notes 站点导航。

## 与 Phase 1 的关系

FrameGraph / shadow 可以先做，不必等 Phase 3 完成。Phase 3 会在后面提升迭代效率：

| Phase 1 需求 | Phase 3 帮助 |
|---|---|
| 改 shadow shader | shader hot reload |
| 调 PBR material | material hot reload + asset list |
| IBL HDR 资源 | HDR import + registry |
| 发布 shadow demo | asset root + packaging 前置 |

## 继续阅读

- [资产系统概念](../../concepts/assets/index.md)
- [Phase 12 · Release](phase-12-release.md)
- pending 需求编号：`REQ-044-c`

# Phase 11 · AI Asset Generation

> 目标：让引擎不只消费资产，也能通过 agent/generator 生成资产，并把来源写入 AssetRegistry。

## 前置

| 前置 | 原因 |
|---|---|
| Phase 3 AssetRegistry | 生成结果需要 GUID、`.meta`、provenance |
| Phase 10 CLI/MCP/Agent | 生成过程由工具和 agent 编排 |
| Phase 4 Animation | 角色动作/骨骼生成需要消费 animation asset |
| Phase 8 Web UI | UI 生成目标格式 |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | generator framework：submit/poll/import/provenance |
| 2 | texture/material generation |
| 3 | prop mesh generation |
| 4 | character portrait/model/rig/motion chain |
| 5 | UI generation |
| 6 | NeRF / 3DGS import and render path |

## 边界

首版不训练模型，不承诺本地推理。远程服务和本地工具都通过 `IAssetGenerator` 抽象接入。所有生成资产都必须记录 prompt、模型、成本、时间、输入引用和可复现信息。

## 继续阅读

- [Phase 3 · Asset Pipeline](phase-3-asset-pipeline.md)
- [Phase 10 · Agent / MCP / CLI](phase-10-ai-agent-mcp.md)

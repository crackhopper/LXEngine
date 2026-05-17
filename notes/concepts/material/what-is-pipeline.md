# 什么是 Pipeline

Pipeline 可以先理解成“GPU 厨房的一套固定设备配置”：这套配置决定用哪份 shader、顶点数据怎样解释、三角形怎样剔除、深度怎样测试、颜色怎样混合。多个 draw 如果结构完全相同，就可以复用同一套配置；如果结构不同，就需要另一套 pipeline。

在 LXEngine 里，pipeline 不是材质本身，也不是 shader 本身。它是一次 draw 的结构事实被归约后的结果。

## PipelineKey 回答“能不能复用”

当前 `PipelineKey` 只包一个结构化 `StringID`。它由两部分组合：

```text
PipelineKey::build(
  objectSig,    # mesh / renderable 侧结构签名
  materialSig   # material pass 侧结构签名
)
```

| Signature | 当前来源 | 表达什么 |
|---|---|---|
| `objectSig` | `SceneNode::getPipelineSignature(pass)` | mesh vertex layout / object render 侧结构 |
| `materialSig` | `MaterialInstance::getPipelineSignature(pass)` | shader name、enabled variants、render state |

`GlobalStringTable::compose()` 会把这些结构化字段 intern 成 `StringID`。所以 `PipelineKey` 本身很小，但调试时可以通过 string table 展开它的组成。

## PipelineBuildDesc 回答“怎样创建”

`PipelineKey` 只负责身份，不保存创建 pipeline 所需的所有数据。真正创建时要用 `PipelineBuildDesc`：

| 字段 | 从哪里来 |
|---|---|
| `key` | `RenderingItem.pipelineKey` |
| `stages` | shader stages |
| `bindings` | shader reflection bindings |
| `vertexLayout` | vertex buffer layout 按 shader input 过滤 |
| `renderState` | material pass render state |
| `topology` | index buffer topology |
| `pushConstant` | 当前固定 model 数据 ABI |

这也是为什么文档里要区分 pipeline identity 和 pipeline build input：前者用于 cache lookup，后者用于 cache miss 时创建对象。

## 哪些变化通常影响 pipeline

| 会影响 pipeline identity | 为什么 |
|---|---|
| 换 shader basename | shader stages 变了 |
| 改 enabled variants | 编译出来的 shader 程序可能变了 |
| 改 render state | cull/depth/blend 是固定功能 pipeline state |
| mesh vertex layout 变化 | vertex input state 变了 |
| topology 变化 | pipeline 创建输入变了 |

当前 `PipelineKey` 由 object/material signatures 组合，`PipelineBuildDesc` 还会携带 topology 等创建输入。文档讨论“是否影响 pipeline”时，要同时注意 key 和 build desc：key 是 cache 身份，build desc 是创建事实。

## 哪些变化不影响 pipeline

| 不影响 pipeline identity | 为什么 |
|---|---|
| 改 `MaterialUBO.baseColor` | 只是 buffer 字节变化 |
| 改 `albedoMap` 指向的纹理 | descriptor resource 变化，不是 pipeline state |
| 改节点 transform | push constant / per-draw data 变化 |
| 改 light 数值 | scene-level UBO 数据变化 |
| 关闭某个 pass | 影响是否产出 item，不改变该 pass 自身 key |

这些变化仍然会影响画面，但不应该触发 pipeline 重建。

## PipelineCache 放在 backend

pipeline 的最终对象是 backend 资源。当前 Vulkan backend 通过 `PipelineCache` 按 `PipelineKey` 复用 pipeline：

```text
FrameGraph::collectAllPipelineBuildDescs()
  -> backend PipelineCache::preload(descs)
  -> draw 时按 item.pipelineKey 查找 pipeline
```

如果预构建漏了，运行时 cache miss 会暴露问题；但概念上，上层应尽量从 frame graph / render queue 产物提前收集 pipeline 构建需求。

## 我们已经学会了什么

Pipeline 是 draw 结构的复用单位。`PipelineKey` 负责身份，`PipelineBuildDesc` 负责创建输入。材质会通过 shader variants 和 render state 影响 pipeline，但参数和纹理值不会。

## 下一步

- [模板如何影响 Pipeline](template-and-pipeline.md)
- [多 Pass 材质怎样变成 Draw](pass-rendering-flow.md)
- [Pipeline identity 源码分析](../../source_analysis/src/core/pipeline/pipeline_identity.md)

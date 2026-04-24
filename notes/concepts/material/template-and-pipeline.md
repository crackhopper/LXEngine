# 模板如何影响 Pipeline

## 核心结论

`MaterialTemplate` 直接影响 pipeline identity，因为它保存了会改变渲染结构的东西：

- pass 定义
- shader name + enabled variants
- render state

这些任何一项变化，`PipelineKey` 就可能变化，引擎就需要一条不同的 pipeline。

## Pipeline Signature 链路

影响通过 signature 链条传递：

```
MaterialPassDefinition.getPipelineSignature()
  = compose(shaderSet.getPipelineSignature(), renderState.getPipelineSignature())
        ↓
MaterialTemplate.getPipelinePassSignature(pass)
        ↓
MaterialInstance.getPipelineSignature(pass)
        ↓
PipelineKey.build(objectSignature, materialSignature)
```

所以 template 不是"间接影响 pipeline"——它通过 pass signature 直接进入材质侧身份。

## 什么会切 Pipeline，什么不会

把 template 和 instance 分层的直接收益：结构变化和值变化分开了。

| 会切 pipeline（template/pass 级） | 不会切 pipeline（instance 级） |
|------|------|
| shader name | `setFloat` / `setVec3` |
| enabled variants | `setTexture` |
| render state（cull / depth / blend） | `syncGpuData()` |
| pass 定义变化 | per-pass 参数覆写 |

这也是为什么 variants 固定在 template/pass 上，而不允许 instance 运行时随便改——改了就意味着 pipeline identity 变了。

## 和 Scene 校验的关系

`SceneNode` 为每个 enabled pass 重建 validated 数据时，会同时取：

- mesh 的 `getPipelineSignature(pass)` — object 侧
- material 的 `getPipelineSignature(pass)` — 材质侧

然后调用 `PipelineKey::build(...)` 得到这个 pass 下的 pipeline identity。

这意味着 template 里每个 pass 的定义，不只是给 backend 最后创建 pipeline 用的。它在 scene 前端就已经决定了：

- 这个对象在某个 pass 下是否合法
- 这个对象在某个 pass 下会不会和别的对象复用同一条 pipeline

## 继续阅读

- Pipeline identity 系统：[../../subsystems/pipeline-identity.md](../../subsystems/pipeline-identity.md)
- 先读 Pipeline 导入文档：[what-is-pipeline.md](what-is-pipeline.md)

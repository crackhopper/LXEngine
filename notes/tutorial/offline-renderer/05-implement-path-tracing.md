# 实现自己的 Path Tracing：从 Primary Ray Shader 扩成 Integrator

当前 offline renderer 可以看作一台已经通电的实验仪器：scene/resource table、render path graph、`RenderInputDesc`、compute dispatch、readback payload 都能工作。实现自己的 path tracing，重点不是重新搭 Vulkan，而是把 OfflineRT graph、render feature、hit shader、随机采样、路径循环、材质评估和输出验证逐步扩展进去。

## 先理解当前 Shader 做了什么

`assets/shaders/glsl/render_paths/OfflineRT/standard_pbr_primary_ray.comp` 现在执行的是一条很短的路径：

| 步骤 | 当前行为 | Path tracing 会怎样扩展 |
|---|---|---|
| 生成相机 ray | 每个像素按 `samples` 做 jitter | 保留，并增加 deterministic sequence / blue noise 入口 |
| 遍历 BVH | 显式栈遍历 `SceneSoftwareBvhNode` | 保留，后续可替换为更好的 BVH / Vulkan RT |
| 命中三角形 | 通过 primitive -> mesh -> index -> vertex 查询 | 扩展 texture sampling、tangent space 和更完整 hit record |
| 直接光 | 只算一个 directional light | 扩成 light sampling + MIS |
| 环境 | ray miss 时采样 infinite environment，缺省回退 background | 改成 HDR environment importance sampling |
| 输出 | 写线性 `vec4` | 保留线性 HDR，后续附加 AOV / variance |

这份 shader 的价值是“管线样板”：它证明了 compute shader 能拿到 scene、BVH、material、camera params 和 output buffer。Path tracing 不应该一开始推倒这些接口，而是先在这个壳里加路径循环。

## Integrator 的最小扩展面

| 层 | 当前文件 | 修改原因 |
|---|---|---|
| Profile / graph | `scene.outputProfiles` + `.render-path.yaml` | 新增 path tracing profile 和 compute pass shader URI |
| Scene records | `src/core/scene/scene_gpu_records.*` | 增加 path tracing 需要的材质、纹理、光源字段 |
| Asset resolve | `src/infra/offline/offline_asset_resolver.*` | 把 `cache://`、HDR、texture 路径解析到本地文件 |
| Scene compile | `src/infra/offline/offline_scene_loader.*` | 从 scene YAML / material YAML 收集离线材质数据 |
| Render feature / BVH | `src/core/frame_graph/render_feature_derived_resource_producer.*` + OfflineRT feature | 构建 software BVH，未来可替换 hardware BLAS/TLAS |
| Work compiler | `src/core/frame_graph/render_work_compiler.*` | 生成 path tracing 所需的 dispatch work、resource binding plan 和 pipeline key |
| Vulkan executor | `src/backend/vulkan/vulkan_frame_graph_executor.*` | 支持 graph 声明的 compute pass、resource sync 和 readback payload |
| Shader | `assets/shaders/glsl/*.comp` | 实现真正的 path tracing integrator |
| CLI | `src/tools/lxe_offline_render/main.cpp` | 选择 output profile、输出文件、打印验证信息 |

## 推荐的实现顺序

| 阶段 | 目标 | 验收方式 |
|---|---|---|
| 1 | 新增 path tracing compute shader 和 render path graph profile，仍输出 primary-ray 同等结果 | CLI 能通过新的 `--profile` 跑出 `.exr` / `.png` |
| 2 | 加 per-sample RNG 和 path throughput loop | 同一 seed 输出稳定，不同 samples 中心像素有限 |
| 3 | 实现 diffuse cosine sampling | 灰色地面能看到环境间接光趋势 |
| 4 | 接入 directional light 的 next-event estimation | 有光源时噪声低于纯 BSDF 采样 |
| 5 | 支持 metallic/roughness 的简化 microfacet | Helmet 的金属/粗糙度区域能反射环境色，roughness 改变高光扩散 |
| 6 | 接 HDR / KTX environment 纹理采样 | environment feature 或 scene environment asset 真正影响背景和间接光 |
| 7 | 输出 EXR/PNG 和 AOV | 同时保存 beauty、albedo、normal、depth 或 variance |

这个顺序的关键是每一步都能形成一个小的可运行结果，而不是等到完整物理模型写完才第一次 dispatch。

## C++ 与 GLSL 的 Layout 合同

离线 renderer 使用 std430 storage buffer。C++ 和 GLSL 的结构体必须同时改，并用测试固定大小。

```cpp
// src/core/scene/scene_gpu_records.hpp
struct alignas(16) SceneGpuMaterialRecord final {
  Vec4f baseColor;  // -> GLSL lxSceneMaterialRecord.baseColor
  Vec4f pbrParams;  // -> x metallic, y roughness, z/w shading params
  Vec4f emissive;   // -> GLSL lxSceneMaterialRecord.emissive
  u32 baseColorTexture;
  u32 normalTexture;
  u32 metallicRoughnessTexture;
  u32 flags;
};

static_assert(sizeof(SceneGpuMaterialRecord) == 64);
```

```glsl
// assets/shaders/glsl/render_paths/OfflineRT/standard_pbr_primary_ray.comp
struct lxSceneMaterialRecord {
  vec4 baseColor;
  vec4 pbrParams;
  vec4 emissive;
  uvec4 textureFlags;
};
```

| 改动类型 | 必须同步的位置 |
|---|---|
| 新增材质参数 | `SceneGpuMaterialRecord`、GLSL `lxSceneMaterialRecord`、`SceneResourceTable` packing、`test_scene_resource_upload_view_v2` |
| 新增 texture index | `MaterialInstance`、`SceneGpuMaterialRecord`、storage resource、shader sampling |
| 新增 light buffer | `SceneResourceTable`、GPU light struct、storage resource、shader light loop |
| 新增 output AOV | `OfflineReadbackImage` 或新增 output resource、CLI 输出模块、测试文件大小 |

## Shader 里的 Path Loop 骨架

第一版 path tracing 可以先保持单一 shader、单一 output buffer。下面是当前代码应扩展成的结构形态，不是直接复制就能工作的完整代码：

```glsl
vec3 tracePath(vec3 origin, vec3 dir, uint pixelIndex, uint sampleIndex) {
  vec3 radiance = vec3(0.0);
  vec3 throughput = vec3(1.0);
  uint rng = params.seed ^ (pixelIndex * 9781u) ^ (sampleIndex * 6271u);

  for (uint depth = 0u; depth < params.maxBounce; ++depth) {
    Hit hit = traceClosest(origin, dir);
    if (!hit.valid) {
      radiance += throughput * sampleEnvironment(dir, rng);
      break;
    }

    Material material = materials[hit.materialIndex];
    radiance += throughput * material.emissive.rgb;
    radiance += throughput * sampleDirectLighting(hit, material, rng);

    BsdfSample bsdf = sampleBsdf(hit, material, -dir, rng);
    if (bsdf.pdf <= 0.0) {
      break;
    }

    throughput *= bsdf.weight;
    origin = hit.position + hit.normal * 0.002;
    dir = bsdf.direction;

    if (depth >= 3u) {
      float keep = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
      if (random01(rng) > keep) {
        break;
      }
      throughput /= keep;
    }
  }

  return radiance;
}
```

这段骨架里最容易出错的是 pdf 和 throughput：每次 BSDF 采样都要把 `f * cosTheta / pdf` 乘进 throughput；direct lighting 如果也采样 BSDF，则需要 MIS，否则亮点噪声会很高。

## 测试策略

| 测试 | 应覆盖的风险 |
|---|---|
| `test_gltf_scene_asset_loader` | scene/profile/light/material 字段没有被丢掉 |
| `test_scene_resource_upload_view_v2` | std430 大小、upload view、primitive/material/light buffer 合同稳定 |
| `test_render_work_compiler` | graph 到 dispatch work / `RenderInputDesc` 的合同稳定 |
| CLI smoke | shader、resource binding、dispatch、readback 能跑完整链路 |
| 小尺寸 golden / statistics | 固定 seed 下中心像素、平均亮度、NaN/Inf 检查稳定 |

Path tracing 的图像测试不要一开始追求逐像素 golden。随机采样会导致细小差异；更适合先固定 seed，检查输出尺寸、有限值、平均亮度区间和关键像素大致范围。

## 当前最值得避免的耦合

| 不建议 | 原因 | 更合适的做法 |
|---|---|---|
| 绕开 render path graph 写 C++ 分支 | 会重新制造 offline-only graph | 用 output profile 指向新的 `.render-path.yaml` |
| 把 path tracing 参数塞进 realtime renderer | 实时和离线的生命周期、输出目标不同 | 放在 render feature / output profile / graph 中 |
| 为 software BVH 写死接口 | 后续 hardware RT 会迁移困难 | 通过 render feature derived resource 对齐 BLAS/TLAS 模型 |
| 先做复杂 UI | integrator 还在变，UI 会过早固化接口 | 先用 CLI 和 scene profile 固定实验合同 |

## 我们已经学会了什么

我们已经把 path tracing 从“写一个新 shader”拆成了一组合同变化：profile 要能选择 render path graph，render feature 要声明 BVH/ray program table，material scheme 要承载 hit shader，scene record 和 GLSL layout 要同步，`RenderWorkCompiler` 要生成正确 dispatch work，`FrameGraphExecutor` 要绑定资源并读回输出。当前 primary-ray OfflineRT shader 是可工作的实验壳，真正的多 bounce、MIS、importance sampling、AOV 和 PBRT 高阶材质 reference 还属于后续需求。

因此实现 path tracing 时，最稳的路线是先保持现有离线链路可运行，再逐步替换 integrator 的采样和材质评估。每一步都要有小尺寸 CLI smoke 或统计测试，而不是等完整物理模型写完后才第一次 dispatch。

## 下一步

- [运行离线渲染器](01-run-offline-renderer.md)
- [Offline Renderer 总览](index.md)
- [REQ-074-h：OfflineRT RenderPathGraph Compute Path](../../requirements/074-h-offlinert-render-path-graph-compute-path.md)
- [REQ-075-c：Advanced Offline Path Tracing Reference](../../requirements/075-c-offline-path-tracing-pbr-reference.md)

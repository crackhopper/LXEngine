# Active Requirement FrameGraph v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the active requirement queue by implementing FrameGraph v1 first, then building directional shadows, CSM, tutorial support, and architecture docs on top of that working foundation.

**Architecture:** FrameGraph v1 introduces core-only pass resource declarations and a compiled execution plan, then threads target identity into pipeline keys before Vulkan executes sequential passes. Later shadow and CSM work consumes those contracts rather than inventing a separate render path.

**Tech Stack:** C++20, CMake/Ninja, Vulkan, GLSL/glslc, LXEngine `src/core`, `src/backend/vulkan`, `src/demos/lxe_editor`, and `notes/`.

---

## File Structure

`REQ-042-a` implementation files:

- Modify: `src/core/frame_graph/render_target.hpp` for `RenderTargetDesc`, target roles, attachment descriptors, signatures, and compatibility helpers.
- Modify: `src/core/frame_graph/frame_graph.hpp` and `src/core/frame_graph/frame_graph.cpp` for frame-graph resource references, pass reads/writes, compile result, validation, and inspection.
- Modify: `src/core/frame_graph/render_queue.cpp` and `src/core/frame_graph/render_queue.hpp` only where target-aware pipeline key composition must happen during queue build.
- Modify: `src/core/pipeline/pipeline_key.hpp` and `src/core/pipeline/pipeline_key.cpp` for target-aware keys.
- Modify: `src/core/pipeline/pipeline_build_desc.hpp` and `src/core/pipeline/pipeline_build_desc.cpp` so backend pipeline creation receives target descriptions.
- Modify: `src/core/utils/string_table.hpp` and `src/core/utils/string_table.cpp` for a target signature type tag.
- Modify: `src/core/scene/object.hpp`, `src/core/scene/object.cpp`, and `src/core/scene/scene.hpp` to preserve object/material signatures until the queue can compose a target-aware key.
- Modify: `src/backend/vulkan/details/device_resources/texture.*`, `src/backend/vulkan/details/render_objects/render_pass.*`, `src/backend/vulkan/details/render_objects/framebuffer.*`, `src/backend/vulkan/details/resource_manager.*`, and `src/backend/vulkan/vulkan_renderer.cpp` for offscreen attachments and sequential pass execution.
- Modify: `openspec/specs/frame-graph/spec.md`, `openspec/specs/pipeline-key/spec.md`, `openspec/specs/pipeline-build-desc/spec.md`, `openspec/specs/pipeline-cache/spec.md`, and `openspec/specs/renderer-backend-vulkan/spec.md` to match the implemented contracts.
- Test: `src/test/integration/test_frame_graph.cpp`, `src/test/integration/test_pipeline_identity.cpp`, `src/test/integration/test_pipeline_build_info.cpp`, `src/test/integration/test_pipeline_cache.cpp`, and Vulkan integration tests under `src/test/integration/`.

Later active requirement files:

- `REQ-042-b`: `assets/shaders/glsl/`, `assets/materials/`, `src/core/scene/light*`, `src/core/frame_graph/*`, `src/backend/vulkan/*`, `src/demos/lxe_editor/`, and related tests.
- `REQ-042-c`: camera/light shadow data, CSM split/matrix utilities, layered or multi-texture shadow resources, GLSL sampling, editor/test scene integration, and tests.
- `REQ-043-a`: `notes/tutorial/`, `notes/concepts/`, tutorial scene assets, minimal editor controls, and docs/tests for pending markers.
- `REQ-043-b`: `notes/concepts-design/architecture.md`, `notes/concepts-design/project-layout.md`, `notes/concepts-design/index.md`, `notes/nav.yml` if required.

## Task 1: Prepare Isolated Worktree And Baseline

**Files:**
- Modify conditionally: `.gitignore` when `.worktrees/` is not already ignored.
- Read: `AGENTS.md`, `openspec/specs/cpp-style-guide/spec.md`, `notes/requirements/042-a-frame-graph-v1-resource-target-pass-execution.md`

- [ ] **Step 1: Detect worktree state**

Run:

```bash
GIT_DIR=$(cd "$(git rev-parse --git-dir)" && pwd -P)
GIT_COMMON=$(cd "$(git rev-parse --git-common-dir)" && pwd -P)
git rev-parse --show-superproject-working-tree 2>/dev/null
git branch --show-current
```

Expected: identify whether the current checkout is already a linked worktree.

- [ ] **Step 2: Create the integration worktree when Step 1 reports a normal checkout**

Run from `/home/lixiang/proj/LXEngine` when not already isolated:

```bash
git check-ignore -q .worktrees || printf '\n.worktrees/\n' >> .gitignore
git diff -- .gitignore
git add .gitignore
git commit -m "Ignore local worktrees"
git worktree add .worktrees/req-042a-framegraph-v1 -b req-042a-framegraph-v1
```

Expected: `.worktrees/req-042a-framegraph-v1` exists on branch `req-042a-framegraph-v1`. If `.gitignore` already ignores `.worktrees`, skip the `.gitignore` edit and commit.

- [ ] **Step 3: Configure or reuse build directory**

Run in the active implementation worktree:

```bash
cmake -S . -B build -G Ninja
```

Expected: CMake configure succeeds.

- [ ] **Step 4: Run focused baseline tests**

Run:

```bash
cmake --build build --target test_frame_graph test_pipeline_identity test_pipeline_build_info test_pipeline_cache
build/src/test/test_frame_graph
build/src/test/test_pipeline_identity
build/src/test/test_pipeline_build_info
build/src/test/test_pipeline_cache
```

Expected: all focused tests pass before implementation changes.

- [ ] **Step 5: Commit only setup changes**

Run only if `.gitignore` changed:

```bash
git status --short
git log --oneline -1
```

Expected: setup changes are committed separately from implementation.

## Task 2: Add Target Descriptions And FrameGraph Compile Tests

**Files:**
- Modify: `src/core/frame_graph/render_target.hpp`
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Write failing tests for target desc and compile validation**

Add tests to `src/test/integration/test_frame_graph.cpp`:

```cpp
void testFrameGraphCompileAcceptsColorWriteThenSampleRead() {
  using namespace LX_core;
  FrameGraph graph;
  const auto offscreen =
      FrameGraphResourceRef::colorAttachment(StringID("test.color"));
  const auto swapColor =
      FrameGraphResourceRef::colorAttachment(StringID("swapchain.color"));
  const auto swapDepth =
      FrameGraphResourceRef::depthAttachment(StringID("swapchain.depth"));

  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA8),
                          {},
                          {},
                          {FrameGraphWrite{offscreen}}});
  graph.addPass(FramePass{Pass_DebugOverlay,
                          RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                      ImageFormat::D32Float),
                          {},
                          {FrameGraphRead::sampled(offscreen.name)},
                          {FrameGraphWrite{swapColor},
                           FrameGraphWrite{swapDepth}}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "compile should accept write then sampled read");
  EXPECT(compiled.getPasses().size() == 2, "compiled pass count should be 2");
}

void testFrameGraphCompileReportsMissingRead() {
  using namespace LX_core;
  FrameGraph graph;
  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                      ImageFormat::D32Float),
                          {},
                          {FrameGraphRead::sampled(StringID("missing.depth"))},
                          {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
                              StringID("swapchain.color"))}}});

  const auto compiled = graph.compile();
  EXPECT(!compiled.isValid(), "compile should reject missing resource read");
  const std::string errors = compiled.errorText();
  EXPECT(errors.find("Forward") != std::string::npos,
         "error should include pass name");
  EXPECT(errors.find("missing.depth") != std::string::npos,
         "error should include resource name");
}
```

Add both functions to the test runner in the same file.

- [ ] **Step 2: Run the tests and confirm failure**

Run:

```bash
cmake --build build --target test_frame_graph
build/src/test/test_frame_graph
```

Expected: compile fails because `RenderTargetDesc`, `FrameGraphRead`, `FrameGraphWrite`, `FrameGraphResourceRef`, and `FrameGraph::compile()` do not exist.

- [ ] **Step 3: Implement core target/resource declarations**

In `src/core/frame_graph/render_target.hpp`, add:

```cpp
enum class RenderTargetRole : u8 {
  Swapchain,
  Offscreen,
};

enum class FrameGraphAttachmentKind : u8 {
  Color,
  Depth,
};

struct RenderTargetDesc {
  RenderTargetRole role = RenderTargetRole::Swapchain;
  std::optional<ImageFormat> colorFormat = ImageFormat::BGRA8;
  std::optional<ImageFormat> depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;
  u32 layerCount = 1;

  static RenderTargetDesc swapchain(ImageFormat color, ImageFormat depth);
  static RenderTargetDesc offscreenColor(ImageFormat color);
  static RenderTargetDesc offscreenDepth(ImageFormat depth);
  bool operator==(const RenderTargetDesc &other) const;
  bool operator!=(const RenderTargetDesc &other) const;
  usize getHash() const;
  StringID getPipelineSignature() const;
};
```

Keep `RenderTarget` available as an alias-compatible struct or wrapper until all callers are migrated.

- [ ] **Step 4: Implement compile result and pass read/write declarations**

In `src/core/frame_graph/frame_graph.hpp`, add:

```cpp
struct FrameGraphResourceRef {
  StringID name;
  FrameGraphAttachmentKind kind = FrameGraphAttachmentKind::Color;

  static FrameGraphResourceRef colorAttachment(StringID name);
  static FrameGraphResourceRef depthAttachment(StringID name);
};

struct FrameGraphRead {
  StringID resource;
  static FrameGraphRead sampled(StringID resource);
};

struct FrameGraphWrite {
  FrameGraphResourceRef resource;
};

struct CompiledFrameGraphPass {
  StringID name;
  RenderTargetDesc target;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
};

class CompiledFrameGraph {
public:
  bool isValid() const;
  const std::vector<std::string> &getErrors() const;
  std::string errorText() const;
  const std::vector<CompiledFrameGraphPass> &getPasses() const;

private:
  friend class FrameGraph;
  std::vector<CompiledFrameGraphPass> m_passes;
  std::vector<std::string> m_errors;
};
```

Extend `FramePass` with `std::vector<FrameGraphRead> reads` and `std::vector<FrameGraphWrite> writes`.

- [ ] **Step 5: Implement compile validation**

In `src/core/frame_graph/frame_graph.cpp`, implement `FrameGraph::compile()` with declared-order validation:

```cpp
CompiledFrameGraph FrameGraph::compile() const {
  CompiledFrameGraph out;
  std::unordered_set<StringID, StringID::Hash> available;
  std::unordered_set<StringID, StringID::Hash> written;

  for (const auto &pass : m_passes) {
    for (const auto &read : pass.reads) {
      if (available.find(read.resource) == available.end()) {
        out.m_errors.push_back("pass " +
                               GlobalStringTable::get().toDebugString(pass.name) +
                               " reads missing resource " +
                               GlobalStringTable::get().toDebugString(read.resource));
      }
    }

    for (const auto &write : pass.writes) {
      if (write.resource.name == StringID{}) {
        out.m_errors.push_back("pass " +
                               GlobalStringTable::get().toDebugString(pass.name) +
                               " writes unnamed resource");
        continue;
      }
      if (written.find(write.resource.name) != written.end()) {
        out.m_errors.push_back("pass " +
                               GlobalStringTable::get().toDebugString(pass.name) +
                               " writes duplicate resource " +
                               GlobalStringTable::get().toDebugString(write.resource.name));
        continue;
      }
      written.insert(write.resource.name);
      available.insert(write.resource.name);
    }

    out.m_passes.push_back(
        CompiledFrameGraphPass{pass.name, pass.target, pass.reads, pass.writes});
  }
  return out;
}
```

Adjust the exact empty `StringID` check to match the available `StringID` API.

- [ ] **Step 6: Run test and commit**

Run:

```bash
cmake --build build --target test_frame_graph
build/src/test/test_frame_graph
git add src/core/frame_graph src/test/integration/test_frame_graph.cpp
git commit -m "Add frame graph compile resource declarations"
```

Expected: `test_frame_graph` passes and commit succeeds.

## Task 3: Make Pipeline Identity Target-Aware

**Files:**
- Modify: `src/core/utils/string_table.hpp`
- Modify: `src/core/utils/string_table.cpp`
- Modify: `src/core/pipeline/pipeline_key.hpp`
- Modify: `src/core/pipeline/pipeline_key.cpp`
- Modify: `src/core/pipeline/pipeline_build_desc.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/scene/object.hpp`, `src/core/scene/object.cpp`, `src/core/scene/scene.hpp`
- Test: `src/test/integration/test_pipeline_identity.cpp`
- Test: `src/test/integration/test_pipeline_build_info.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Write failing target identity tests**

Add to `src/test/integration/test_pipeline_identity.cpp`:

```cpp
void testPipelineKeyIncludesTargetSignature() {
  using namespace LX_core;
  Fixture f;
  const StringID objectSig = f.node->getPipelineSignature(Pass_Forward);
  const StringID materialSig = f.material->getPipelineSignature(Pass_Forward);

  const auto swapchain =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  const auto offscreen = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);
  const auto depthOnly = RenderTargetDesc::offscreenDepth(ImageFormat::D32Float);

  const PipelineKey kSwap =
      PipelineKey::build(objectSig, materialSig, swapchain.getPipelineSignature());
  const PipelineKey kOff =
      PipelineKey::build(objectSig, materialSig, offscreen.getPipelineSignature());
  const PipelineKey kDepth =
      PipelineKey::build(objectSig, materialSig, depthOnly.getPipelineSignature());

  EXPECT(kSwap != kOff, "swapchain and offscreen targets must not collide");
  EXPECT(kSwap != kDepth, "swapchain and depth-only targets must not collide");
  EXPECT(kOff != kDepth, "offscreen color and depth-only targets must not collide");
}
```

Add to `src/test/integration/test_frame_graph.cpp`:

```cpp
void testFrameGraphKeepsDifferentTargetsAsDifferentBuildDescs() {
  using namespace LX_core;
  auto scene = makeSceneWithRenderable(true);
  const auto targetA = RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                  ImageFormat::D32Float);
  const auto targetB = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, targetA, {}});
  fg.addPass(FramePass{Pass_Forward, targetB, {}});
  fg.buildFromScene(*scene);

  const auto infos = fg.collectAllPipelineBuildDescs();
  EXPECT(infos.size() == 2,
         "same object/material on different targets should keep two build descs");
}
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cmake --build build --target test_pipeline_identity test_frame_graph
build/src/test/test_pipeline_identity
build/src/test/test_frame_graph
```

Expected: compile fails until target-aware key APIs and target-aware queue build exist.

- [ ] **Step 3: Add target type tag and key overload**

Add a `TargetRender` or equivalent enum value to `TypeTag` in `src/core/utils/string_table.hpp`, update `tagName()` in `string_table.cpp`, then change `PipelineKey`:

```cpp
static PipelineKey build(StringID objectSig, StringID materialSig,
                         StringID targetSig);
```

Implement it in `pipeline_key.cpp` by composing three fields with `TypeTag::PipelineKey`.

- [ ] **Step 4: Store target desc on rendering items/build descs**

Add `RenderTargetDesc target;` to the render item or build-desc path that is available to `PipelineBuildDesc::fromRenderingItem()`. In `PipelineBuildDesc`, add:

```cpp
RenderTargetDesc target;
```

Set `info.target = item.target;` in `PipelineBuildDesc::fromRenderingItem()`.

- [ ] **Step 5: Compose final key where target is known**

In `RenderQueue::buildFromScene(const Scene &scene, StringID pass, const RenderTargetDesc &target)`, after copying validated render data into a `RenderingItem`, set:

```cpp
item.target = target;
item.pipelineKey =
    PipelineKey::build(item.objectSignature, item.materialSignature,
                       target.getPipelineSignature());
```

If `RenderingItem` does not yet expose object/material signatures, add those fields to validated pass data and copy them into the item. Preserve the old pass-aware material validation behavior.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
cmake --build build --target test_pipeline_identity test_pipeline_build_info test_frame_graph test_pipeline_cache
build/src/test/test_pipeline_identity
build/src/test/test_pipeline_build_info
build/src/test/test_frame_graph
build/src/test/test_pipeline_cache
git add src/core src/test/integration/test_pipeline_identity.cpp src/test/integration/test_pipeline_build_info.cpp src/test/integration/test_frame_graph.cpp
git commit -m "Include render target identity in pipeline keys"
```

Expected: focused core/pipeline tests pass.

## Task 4: Add Backend Attachment Registry And Offscreen Resource Support

**Files:**
- Modify: `src/backend/vulkan/details/device_resources/texture.hpp`
- Modify: `src/backend/vulkan/details/device_resources/texture.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.hpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.*`
- Test: existing Vulkan tests under `src/test/integration/`

- [ ] **Step 1: Write failing Vulkan resource test**

Add a focused test to `src/test/integration/test_vulkan_texture.cpp`:

```cpp
void testSampledAttachmentTextureCreatesSamplerAndShaderReadUsage() {
  VulkanFixture fixture;
  auto texture = VulkanTexture::createForAttachment(
      *fixture.device, 256, 256, VK_FORMAT_D32_SFLOAT,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT);

  EXPECT(texture->getImageView() != VK_NULL_HANDLE,
         "sampled attachment should expose image view");
  EXPECT(texture->getSampler() != VK_NULL_HANDLE,
         "sampled attachment should create sampler");
}
```

Use the fixture style already present in the target file.

- [ ] **Step 2: Run test and confirm failure**

Run:

```bash
cmake --build build --target test_vulkan_texture
xvfb-run -a build/src/test/test_vulkan_texture
```

Expected: compile or runtime failure until sampled attachment support exists.

- [ ] **Step 3: Extend attachment texture creation**

Update `VulkanTexture::createForAttachment()` so usage flags include the caller-provided `VK_IMAGE_USAGE_SAMPLED_BIT` and create a sampler when sampled usage is present. The sampler can use clamp-to-edge addressing and linear filtering for color; depth sampling can use the same sampler until comparison sampling is introduced by `REQ-042-b`.

- [ ] **Step 4: Add frame-graph attachment registry skeleton**

In `VulkanResourceManager`, add a backend-only map keyed by resource debug name or `StringID`:

```cpp
struct VulkanFrameGraphAttachment {
  std::unique_ptr<VulkanTexture> texture;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags aspect = 0;
  VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkExtent2D extent{};
};
```

Add methods to create or retrieve offscreen attachments from compiled frame graph resources. Keep swapchain resources external to this registry.

- [ ] **Step 5: Run tests and commit**

Run:

```bash
cmake --build build --target test_vulkan_texture test_vulkan_resource_manager
xvfb-run -a build/src/test/test_vulkan_texture
xvfb-run -a build/src/test/test_vulkan_resource_manager
git add src/backend/vulkan/details src/test/integration
git commit -m "Add Vulkan sampled attachment resources"
```

Expected: Vulkan resource tests pass.

## Task 5: Execute Compiled FrameGraph Passes Sequentially

**Files:**
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/backend/vulkan/details/render_objects/render_pass.*`
- Modify: `src/backend/vulkan/details/render_objects/framebuffer.*`
- Modify: `src/backend/vulkan/details/resource_manager.*`
- Modify: `src/backend/vulkan/details/commands/command_buffer.*`
- Test: Vulkan integration tests and smoke tests

- [ ] **Step 1: Add a smoke test or probe for two sequential passes**

Create or extend a Vulkan integration test that builds a frame graph with:

```text
Pass_Shadow writes shadow.depth
Pass_Forward reads shadow.depth and writes swapchain.color/swapchain.depth
```

The test should assert that renderer initialization and one draw frame do not fail when the graph contains an offscreen depth pass before the swapchain pass.

- [ ] **Step 2: Run the smoke test and confirm failure**

Run:

```bash
cmake --build build --target test_vulkan_frame_graph
xvfb-run -a build/src/test/test_vulkan_frame_graph
```

Expected: target or test does not compile until the renderer has sequential pass execution support.

- [ ] **Step 3: Compile the frame graph in renderer scene initialization**

In `VulkanRendererImpl::initScene`, after `m_frameGraph.buildFromScene(*m_scene)`, call:

```cpp
m_compiledFrameGraph = m_frameGraph.compile();
if (!m_compiledFrameGraph.isValid()) {
  throw std::runtime_error(m_compiledFrameGraph.errorText());
}
```

Store `CompiledFrameGraph m_compiledFrameGraph;` on `VulkanRendererImpl`.

- [ ] **Step 4: Replace single render pass loop with pass execution loop**

In `VulkanRendererImpl::draw()`, keep acquire, frame begin, descriptor begin, and resource begin. Replace the single `beginRenderPass` covering every pass with a loop over `m_compiledFrameGraph.getPasses()`. For each pass:

```cpp
preparePassAttachments(compiledPass, imageIndex, extent, *cmd);
cmd->beginRenderPass(passRenderPass, passFramebuffer, passExtent, passClearValues);
drawQueueForPass(compiledPass.name, *cmd);
if (isFinalSwapchainPass(compiledPass)) {
  beginAndEndGuiFrameInsideThisPass(*cmd);
}
cmd->endRenderPass();
transitionPassWritesForNextReads(compiledPass, *cmd);
```

Implement helper methods as private methods on `VulkanRendererImpl` first. Split into smaller backend classes only if the file becomes difficult to verify.

- [ ] **Step 5: Keep ImGui inside final swapchain pass only**

Move the existing `m_gui.beginFrame()`, `m_drawUiCallback()`, and `m_gui.endFrame()` calls so they execute only when the current compiled pass writes `swapchain.color`.

- [ ] **Step 6: Run Vulkan tests and commit**

Run:

```bash
cmake --build build --target test_vulkan_texture test_vulkan_framebuffer test_vulkan_command_buffer test_vulkan_pipeline test_vulkan_resource_manager
xvfb-run -a build/src/test/test_vulkan_texture
xvfb-run -a build/src/test/test_vulkan_framebuffer
xvfb-run -a build/src/test/test_vulkan_command_buffer
xvfb-run -a build/src/test/test_vulkan_pipeline
xvfb-run -a build/src/test/test_vulkan_resource_manager
git add src/backend/vulkan src/test/integration
git commit -m "Execute frame graph passes sequentially in Vulkan"
```

Expected: focused Vulkan tests pass.

## Task 6: Update Specs And Close REQ-042-a

**Files:**
- Modify: `openspec/specs/frame-graph/spec.md`
- Modify: `openspec/specs/pipeline-key/spec.md`
- Modify: `openspec/specs/pipeline-build-desc/spec.md`
- Modify: `openspec/specs/pipeline-cache/spec.md`
- Modify: `openspec/specs/renderer-backend-vulkan/spec.md`
- Modify: `notes/requirements/042-a-frame-graph-v1-resource-target-pass-execution.md`

- [ ] **Step 1: Update OpenSpec contracts**

Update the specs so they describe:

- `RenderTargetDesc` and frame graph resources instead of the old thin-only `RenderTarget` contract.
- `FramePass` reads/writes and `FrameGraph::compile()`.
- target-aware `PipelineKey`.
- `PipelineBuildDesc` carrying target description.
- Vulkan sequential pass execution and same-queue barriers.

- [ ] **Step 2: Update REQ status**

In `notes/requirements/042-a-frame-graph-v1-resource-target-pass-execution.md`, change implementation status from "未开始" to a concise implemented status with the verification commands that passed.

- [ ] **Step 3: Run focused and broad verification**

Run:

```bash
cmake --build build --target test_frame_graph test_pipeline_identity test_pipeline_build_info test_pipeline_cache
build/src/test/test_frame_graph
build/src/test/test_pipeline_identity
build/src/test/test_pipeline_build_info
build/src/test/test_pipeline_cache
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: all relevant tests pass, or any environment-only Vulkan/display limitation is recorded in the REQ status.

- [ ] **Step 4: Commit REQ-042-a closure**

Run:

```bash
git add openspec/specs notes/requirements/042-a-frame-graph-v1-resource-target-pass-execution.md
git commit -m "Document FrameGraph v1 implementation"
```

Expected: specs and requirement status are committed.

## Task 7: Implement REQ-042-b Directional Shadow Map

**Files:**
- Modify: `assets/shaders/glsl/`
- Modify: `assets/materials/`
- Modify: `src/core/scene/light*`
- Modify: `src/core/frame_graph/*`
- Modify: `src/backend/vulkan/*`
- Modify: `src/demos/lxe_editor/`
- Test: frame graph, material, Vulkan, and editor smoke tests

- [ ] **Step 1: Re-read requirement and write focused shadow tests**

Read `notes/requirements/042-b-directional-shadow-map-depth-pass.md`. Add tests for:

- FrameGraph contains `Pass_Shadow` writing `shadow.depth` before `Pass_Forward`.
- `Pass_Shadow` queue contains only renderables that support shadow.
- depth-only pipeline build desc has no color attachment.
- forward pass descriptor path can see the shadow map binding.

- [ ] **Step 2: Implement depth-only material/pass**

Add a depth-only shader pair or vertex-only-compatible path under `assets/shaders/glsl/`, add material template support under `assets/materials/`, and ensure shadow-capable scene nodes validate `Pass_Shadow`.

- [ ] **Step 3: Add directional shadow data**

Extend directional light data with shadow enabled, shadow view-projection, map size, strength, and bias. Keep the first implementation to one main directional light.

- [ ] **Step 4: Add forward shader sampling**

Update forward GLSL to transform world position into light space, sample `shadow.depth`, apply bias, support hard shadow, and add a minimal 3x3 PCF path.

- [ ] **Step 5: Add observable editor/test scene path**

Add or update a scene with ground receiver, caster, directional light, and camera. Avoid new complex UI unless tests prove the tutorial cannot adjust the needed parameters.

- [ ] **Step 6: Verify and commit**

Run focused shader, material, frame graph, and Vulkan smoke tests. Commit with:

```bash
git add assets src notes/requirements/042-b-directional-shadow-map-depth-pass.md
git commit -m "Add directional shadow map pass"
```

## Task 8: Implement REQ-042-c Cascaded Shadow Maps

**Files:**
- Modify: `src/core/scene/camera*`
- Modify: `src/core/scene/light*`
- Modify: `src/core/frame_graph/*`
- Modify: `src/backend/vulkan/*`
- Modify: `assets/shaders/glsl/`
- Modify: `assets/materials/`
- Modify: `src/demos/lxe_editor/`
- Test: CSM split/matrix tests, descriptor tests, Vulkan smoke tests

- [ ] **Step 1: Add CSM split tests**

Write tests for fixed 4-cascade split calculation using camera near/far, split lambda, and shadow distance. Expected outputs must be deterministic for a chosen camera configuration.

- [ ] **Step 2: Implement cascade matrix generation**

Generate one light view-projection per camera frustum slice, use directional light direction, orthographic projection, and texel snapping.

- [ ] **Step 3: Implement layered or multi-texture depth resources**

Use the backend representation that best fits the completed FrameGraph v1 contract. Ensure cascade count matches resource layers or texture count.

- [ ] **Step 4: Execute shadow pass per cascade**

Reuse `Pass_Shadow` queue. Update per-cascade data through the chosen UBO, push constant, or descriptor path.

- [ ] **Step 5: Update forward shader cascade selection**

Select cascade by view-space depth, sample the matching depth region, apply per-cascade bias and PCF, and provide debug inspection for split/matrix/index data.

- [ ] **Step 6: Verify and commit**

Run CSM unit tests and Vulkan smoke tests. Commit with:

```bash
git add src assets notes/requirements/042-c-cascaded-shadow-maps.md
git commit -m "Add cascaded directional shadows"
```

## Task 9: Implement REQ-043-a Shadow Tutorial Support

**Files:**
- Modify: `notes/tutorial/`
- Modify: `notes/concepts/`
- Modify: `notes/source_analysis/` links when the tutorial references source-analysis pages that changed during FrameGraph/shadow work.
- Modify: `assets/scenes/` or test assets
- Modify: `src/demos/lxe_editor/` only for minimal tutorial controls
- Test: notes build and scene load tests

- [ ] **Step 1: Re-read notes writing style**

Read `openspec/specs/notes-writing-style/spec.md` and `notes/requirements/043-a-shadow-era-tutorial-support.md`.

- [ ] **Step 2: Add tutorial scene**

Add a loadable scene with ground receiver, caster, directional light, and camera. Verify it opens, saves, and reloads through existing editor paths.

- [ ] **Step 3: Update tutorial docs**

Explain FrameGraph shadow write/read flow and CSM cascades using links to current specs/source analysis. Mark light registry, toolbar registry, custom node registry, Web Editor, Engine CLI/MCP, and AssetRegistry hot reload as pending when mentioned.

- [ ] **Step 4: Add minimal editor affordances when the existing UI cannot edit tutorial-required shadow values**

If the tutorial cannot adjust required values through existing UI, add directional light shadow enabled, shadow strength, shadow distance, or cascade debug mode controls.

- [ ] **Step 5: Verify and commit**

Run notes build and scene load tests. Commit with:

```bash
git add notes assets src/demos/lxe_editor notes/requirements/043-a-shadow-era-tutorial-support.md
git commit -m "Add shadow tutorial support"
```

## Task 10: Implement REQ-043-b Architecture Concepts And Mermaid Diagrams

**Files:**
- Modify: `notes/concepts-design/architecture.md`
- Modify: `notes/concepts-design/project-layout.md`
- Modify: `notes/concepts-design/index.md`
- Modify if required: `notes/nav.yml`
- Test: notes site build

- [ ] **Step 1: Re-read docs inputs**

Read `openspec/specs/notes-writing-style/spec.md`, `notes/requirements/043-b-architecture-concepts-mermaid.md`, and the completed FrameGraph/shadow/CSM code.

- [ ] **Step 2: Add layer dependency diagram**

Add a Mermaid diagram that shows `src/core` independent from `src/infra` and `src/backend/vulkan`, with editor depending on all runtime layers.

- [ ] **Step 3: Add runtime render flow diagram**

Add a Mermaid diagram covering Scene/SceneNode, FrameGraph, RenderQueue, PipelineBuildDesc/PipelineCache, VulkanRenderer, and command buffer.

- [ ] **Step 4: Add multi-pass shadow flow diagram**

Add a resource-flow diagram for scene + directional light to shadow pass writing `shadow.depth`, forward pass reading it, and swapchain present.

- [ ] **Step 5: Add module ownership table and pending boundaries**

Cover Scene, Asset/Material, FrameGraph, Pipeline, Backend, Editor, and Notes/Requirements. Mark HDR/Post, PBR full pipeline, G-Buffer/Deferred, task-based pass build, Web Editor, Engine CLI/MCP, and AssetRegistry/hot reload as pending.

- [ ] **Step 6: Verify and commit**

Run:

```bash
scripts/notes/serve_site.sh --build
git add notes/concepts-design notes/nav.yml notes/requirements/043-b-architecture-concepts-mermaid.md
git commit -m "Document shadow-era architecture flow"
```

Expected: notes build succeeds and diagrams are in Mermaid code blocks.

## Final Verification

- [ ] **Step 1: Run full build target set**

Run:

```bash
cmake --build build --target lxe_editor
cmake --build build --target test_frame_graph test_pipeline_identity test_pipeline_build_info test_pipeline_cache
```

Expected: build succeeds.

- [ ] **Step 2: Run automated tests**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: tests pass, with any environment limitation recorded precisely.

- [ ] **Step 3: Archive or finish active requirements**

Use the repository's `finish-req` workflow for each completed active requirement, update the affected notes pages named by that workflow, and ensure `notes/requirements/README.md` no longer lists completed work as unfinished.

- [ ] **Step 4: Final commit**

Run:

```bash
git status --short
git log --oneline -10
```

Expected: no unintended uncommitted changes; recent commits show the active requirement sequence.

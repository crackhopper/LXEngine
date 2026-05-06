## ADDED Requirements

### Requirement: VulkanRenderer integrates the debug overlay pass
`VulkanRenderer::initScene(SceneSharedPtr)` SHALL register a `FramePass{Pass_DebugOverlay, swapchainTarget, {}}` after the forward pass when debug-draw support is enabled in the scene binding. The renderer SHALL build queues for both passes, preload pipelines for both passes, and draw them in frame-graph order.

#### Scenario: Debug overlay pass is drawn after the forward pass
- **WHEN** a scene contains forward renderables and debug overlay renderables bound to the swapchain target
- **THEN** `draw()` records the forward-pass draw calls first
- **AND** records the debug overlay draw calls afterward using the same command-buffer lifecycle

### Requirement: Vulkan debug overlay pipeline honors the fixed line render state
The Vulkan backend SHALL build the debug overlay pipeline from the shared pipeline-build contract using line-list topology, position-plus-color vertex inputs, depth test enabled, depth write disabled, and alpha blending enabled. The backend SHALL reuse the normal descriptor-binding-by-name path for the camera UBO used by the debug-line shader pair.

#### Scenario: Debug overlay pipeline binds camera UBO by name
- **WHEN** a debug overlay `RenderingItem` exposes a reflected `CameraUBO` binding and its descriptor resources include the matching camera UBO
- **THEN** command recording binds that buffer through the normal name-based descriptor update path
- **AND** the resulting pipeline uses the fixed line render state required by the debug overlay contract

## Context

Current device selection combines suitability and preference in one check. That makes the engine fail on otherwise valid adapters. The renderer already has enough structural checks for queue families, extensions, and swapchain support; GPU type should not override those.

## Goals / Non-Goals

**Goals:**
- Allow integrated GPUs and other non-discrete adapters that satisfy Vulkan runtime requirements.
- Preserve discrete preference where multiple suitable adapters exist.

**Non-Goals:**
- No multi-GPU scheduling.
- No backend-specific vendor tuning.

## Decisions

- Split device filtering into suitability and preference stages.
- Treat GPU type as a sort preference only.
- Keep validation/logging explicit so fallback-to-integrated behavior is observable.

## Risks / Trade-offs

- [Lower-performance adapters may now run] → Acceptable; startup availability is more important than hard-coding one class of GPU.
- [Existing tests may assume a discrete GPU] → Update assumptions in docs and smoke-test expectations.

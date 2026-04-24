## Context

Tests already exist as standalone binaries, so the main missing piece is orchestration. Sanitizers also need to remain optional because not every environment will support the same flags or runtime overhead.

## Goals / Non-Goals

**Goals:**
- Make existing tests discoverable and runnable via `ctest`.
- Add an opt-in sanitizer build flag suitable for Linux development.
- Keep app-style tests such as persistent render loops clearly separated from auto-exit tests.

**Non-Goals:**
- No immediate migration to GoogleTest or Catch2.
- No requirement that every test runs in every headless environment without Xvfb.

## Decisions

- Build on existing standalone test executables instead of introducing a new framework first.
- Make sanitizer support opt-in at CMake configure time.
- Encode Linux/Xvfb handling in test notes/documentation rather than burying it in tribal knowledge.

## Risks / Trade-offs

- [Some Vulkan tests need a video device] → Mark/document them clearly and support Xvfb-driven execution paths.
- [Sanitizer flags vary by compiler/platform] → Scope the initial option to the currently supported development environment.

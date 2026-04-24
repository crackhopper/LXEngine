## Why

The project builds many test executables but has no `ctest` registration, no single run entrypoint, and no sanitizer switch for catching UB early. That keeps known issues in core utilities and renderer paths harder to detect and automate.

This change makes the existing test suite runnable through standard CMake flows and adds an opt-in sanitizer build mode for developer and CI use.

## What Changes

- Register existing test executables with CTest.
- Add a CMake option to enable sanitizer-friendly builds.
- Define which tests remain app-style/manual and which are part of automated execution.
- Document Linux/Xvfb expectations for windowed Vulkan tests under `ctest`.

## Capabilities

### New Capabilities
- `test-build-execution`: Standardized CTest registration and optional sanitizer-enabled build configuration.

### Modified Capabilities

## Impact

- Affected code: root `CMakeLists.txt`, `src/test/CMakeLists.txt`
- Affected APIs: build options only
- Affected systems: local verification, CI, sanitizer readiness

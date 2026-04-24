## 1. Test Registration

- [x] 1.1 Enable CTest in the project and register existing auto-exit test executables.
- [x] 1.2 Distinguish persistent/manual tests from normal automated tests.

## 2. Sanitizer Build Option

- [x] 2.1 Add a CMake option for sanitizer-enabled builds on supported toolchains.
- [x] 2.2 Apply the sanitizer flags to test/development targets without changing default release behavior.

## 3. Verification

- [x] 3.1 Run `ctest --output-on-failure` on the supported automated subset.
- [x] 3.2 Validate the documented Linux/Xvfb flow for windowed Vulkan tests.

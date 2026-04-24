## ADDED Requirements

### Requirement: Automated tests are discoverable through CTest
The project SHALL register supported automated test executables with CTest so they can be run through `ctest` instead of only by manual binary invocation.

#### Scenario: CTest discovers supported tests
- **WHEN** a developer configures and builds the project with testing enabled
- **THEN** `ctest` lists the supported automated test executables

### Requirement: Sanitizer builds are opt-in and supported
The build system SHALL provide an opt-in configuration for sanitizer-enabled development builds on supported toolchains.

#### Scenario: sanitizer build is explicitly enabled
- **WHEN** a developer configures the project with the sanitizer option enabled
- **THEN** the resulting build uses sanitizer flags without changing default non-sanitized builds

### Requirement: Windowed Vulkan tests document video-device expectations
Automated test execution guidance SHALL distinguish between tests that can run in a standard headless environment and tests that require a real or virtual video device such as Xvfb.

#### Scenario: headless test guidance is explicit
- **WHEN** a developer runs Vulkan-related tests on Linux without a desktop session
- **THEN** the documented test flow explains when `xvfb-run` is required

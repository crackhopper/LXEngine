## 1. Core Input 头文件

- [x] 1.1 创建 `src/core/input/key_code.hpp`（`KeyCode` 枚举，A-Z / Num0-9 / 控制键 / 方向键 / F1-F4 / Count）
- [x] 1.2 创建 `src/core/input/mouse_button.hpp`（`MouseButton` 枚举，Left/Right/Middle/Count）
- [x] 1.3 创建 `src/core/input/input_state.hpp`（`IInputState` 纯虚接口 + `InputStateSharedPtr` 别名）
- [x] 1.4 创建 `src/core/input/dummy_input_state.hpp`（`DummyInputState` 全零内联实现）

## 2. Window 接口扩展

- [x] 2.1 在 `src/core/platform/window.hpp` 新增 `virtual InputStateSharedPtr getInputState() const = 0`
- [x] 2.2 在 `src/infra/window/window.hpp` 新增 `getInputState() const override` 声明
- [x] 2.3 在 `src/infra/window/sdl_window.cpp` 持有 `DummyInputState` 并实现 `getInputState()`
- [x] 2.4 在 `src/infra/window/glfw_window.cpp` 持有 `DummyInputState` 并实现 `getInputState()`

## 3. 构建配置

- [x] 3.1 更新 `src/core/CMakeLists.txt`，将新增头文件纳入构建可见范围
- [x] 3.2 验证全量构建通过

## 4. 集成测试

- [x] 4.1 创建 `src/test/integration/test_input_state.cpp`（DummyInputState 零值语义 + nextFrame 幂等性）
- [x] 4.2 在 `src/test/CMakeLists.txt` 注册 `test_input_state`
- [x] 4.3 运行测试验证通过

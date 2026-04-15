#include "core/gpu/engine_loop.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace LX_core;
using namespace LX_core::gpu;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

class FakeWindow final : public Window {
public:
  explicit FakeWindow(bool closeImmediately = false)
      : m_closeImmediately(closeImmediately) {}

  int getWidth() const override { return 800; }
  int getHeight() const override { return 600; }
  void updateSize(bool *closed, int *width, int *height) override {
    if (closed) {
      *closed = m_shouldClose;
    }
    if (width) {
      *width = getWidth();
    }
    if (height) {
      *height = getHeight();
    }
  }
  void getRequiredExtensions(std::vector<const char *> &) const override {}
  WindowGraphicsHandle createGraphicsHandle(GraphicsAPI,
                                            GraphicsInstanceHandle) const override {
    return nullptr;
  }
  void destroyGraphicsHandle(GraphicsAPI, GraphicsInstanceHandle,
                             WindowGraphicsHandle) const override {}
  void onClose(std::function<void()> cb) override { m_onClose = std::move(cb); }
  bool shouldClose() override {
    ++shouldCloseCalls;
    if (m_closeImmediately) {
      m_shouldClose = true;
    }
    if (m_shouldClose && m_onClose) {
      m_onClose();
    }
    return m_shouldClose;
  }

  void requestClose() { m_shouldClose = true; }

  int shouldCloseCalls = 0;

private:
  bool m_closeImmediately = false;
  bool m_shouldClose = false;
  std::function<void()> m_onClose;
};

class FakeRenderer final : public Renderer {
public:
  void initialize(WindowPtr, const char *) override {}
  void shutdown() override {}
  void initScene(ScenePtr scene) override {
    ++initSceneCalls;
    lastScene = std::move(scene);
    events.push_back("init");
  }
  void uploadData() override {
    ++uploadCalls;
    events.push_back("upload");
  }
  void draw() override {
    ++drawCalls;
    events.push_back("draw");
  }

  int initSceneCalls = 0;
  int uploadCalls = 0;
  int drawCalls = 0;
  ScenePtr lastScene;
  std::vector<std::string> events;
};

ScenePtr makeScene() { return Scene::create(nullptr); }

void testStartSceneNotPerFrame() {
  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(makeScene());

  loop.tickFrame();
  loop.tickFrame();

  EXPECT(renderer->initSceneCalls == 1, "startScene should initialize once");
  EXPECT(renderer->uploadCalls == 2, "tickFrame uploads each frame");
  EXPECT(renderer->drawCalls == 2, "tickFrame draws each frame");
}

void testUpdateHookRunsBeforeUploadAndDraw() {
  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(makeScene());

  loop.setUpdateHook([&](Scene &, const Clock &) { renderer->events.push_back("hook"); });
  renderer->events.clear();

  loop.tickFrame();

  EXPECT(renderer->events.size() == 3, "hook/upload/draw should all run");
  if (renderer->events.size() == 3) {
    EXPECT(renderer->events[0] == "hook", "hook runs first");
    EXPECT(renderer->events[1] == "upload", "upload runs second");
    EXPECT(renderer->events[2] == "draw", "draw runs third");
  }
}

void testRequestSceneRebuildIsExplicit() {
  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(makeScene());

  loop.requestSceneRebuild();
  loop.tickFrame();

  EXPECT(renderer->initSceneCalls == 2,
         "requestSceneRebuild should trigger exactly one extra initScene");
}

void testInitializeResetsRuntimeState() {
  auto windowA = std::make_shared<FakeWindow>();
  auto windowB = std::make_shared<FakeWindow>();
  auto rendererA = std::make_shared<FakeRenderer>();
  auto rendererB = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(windowA, rendererA);
  loop.startScene(makeScene());
  loop.setUpdateHook([&](Scene &, const Clock &) { rendererA->events.push_back("old_hook"); });
  loop.tickFrame();

  loop.initialize(windowB, rendererB);
  loop.startScene(makeScene());
  loop.tickFrame();

  EXPECT(rendererA->drawCalls == 1, "old renderer should not receive frames after reinitialize");
  EXPECT(rendererB->initSceneCalls == 1, "new renderer should be initialized once after reinitialize");
  EXPECT(rendererB->uploadCalls == 1, "new renderer should upload after reinitialize");
  EXPECT(rendererB->drawCalls == 1, "new renderer should draw after reinitialize");
}

void testRunStopsAfterStopCalled() {
  auto window = std::make_shared<FakeWindow>();
  auto renderer = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(makeScene());

  int hookCalls = 0;
  loop.setUpdateHook([&](Scene &, const Clock &) {
    ++hookCalls;
    loop.stop();
  });

  loop.run();

  EXPECT(hookCalls == 1, "run should stop after first hook-triggered stop");
  EXPECT(renderer->uploadCalls == 1, "current frame still uploads before exit");
  EXPECT(renderer->drawCalls == 1, "current frame still draws before exit");
}

void testRunStopsOnWindowClose() {
  auto window = std::make_shared<FakeWindow>(true);
  auto renderer = std::make_shared<FakeRenderer>();
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(makeScene());

  loop.run();

  EXPECT(window->shouldCloseCalls >= 1, "run should poll window close state");
  EXPECT(renderer->uploadCalls == 0,
         "no frame should run when window closes immediately");
  EXPECT(renderer->drawCalls == 0,
         "no draw should happen when window closes immediately");
}

} // namespace

int main() {
  testStartSceneNotPerFrame();
  testUpdateHookRunsBeforeUploadAndDraw();
  testRequestSceneRebuildIsExplicit();
  testInitializeResetsRuntimeState();
  testRunStopsAfterStopCalled();
  testRunStopsOnWindowClose();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all engine_loop tests passed\n";
  return 0;
}

#pragma once
#include "core/gpu/renderer.hpp"
#include <vector>

namespace LX_core::graphic_backend {
class VulkanRenderer;
using VulkanRendererPtr = std::unique_ptr<VulkanRenderer>;
class VulkanRenderer : public gpu::Renderer {
public:
  struct Token {};
  explicit VulkanRenderer(Token token);
  ~VulkanRenderer() override;
  static VulkanRendererPtr create(Token token){
    return std::make_unique<VulkanRenderer>(token);
  }

  void initialize(WindowPtr window, const char *appName) override{p_impl->initialize(window, appName);}
  void shutdown() override{p_impl->shutdown();}
  void initScene(ScenePtr scene) override{p_impl->initScene(scene);}

  void uploadData() override{p_impl->uploadData();}
  void draw() override{p_impl->draw();}

private:
  gpu::Renderer* p_impl = nullptr;
};

} // namespace LX_core::graphic_backend
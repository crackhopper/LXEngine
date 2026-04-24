#pragma once
#include "core/input/input_state.hpp"
#include "core/scene/camera.hpp"
#include <memory>

namespace LX_core {

class ICameraController {
public:
  virtual ~ICameraController() = default;

  /// Update camera spatial parameters based on input.
  /// Does NOT call camera.updateMatrices(); the caller decides when to update matrices.
  virtual void update(Camera &camera, const IInputState &input, float dt) = 0;
};

using CameraControllerSharedPtr = std::shared_ptr<ICameraController>;

} // namespace LX_core

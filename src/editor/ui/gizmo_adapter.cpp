#include "editor/ui/gizmo_adapter.hpp"

#include <imgui.h>
#include "ImGuizmo.h"

namespace LX_core {

void GizmoAdapter::toFloat16(const Mat4f &matrix, float out[16]) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      out[c * 4 + r] = matrix.m[c][r];
    }
  }
}

Mat4f GizmoAdapter::fromFloat16(const float in[16]) {
  return Mat4f(in);
}

GizmoTransformComponents GizmoAdapter::decompose(const Mat4f &matrix) {
  float packed[16] = {};
  toFloat16(matrix, packed);

  float translation[3] = {};
  float rotation[3] = {};
  float scale[3] = {1.0f, 1.0f, 1.0f};
  ImGuizmo::DecomposeMatrixToComponents(packed, translation, rotation, scale);

  GizmoTransformComponents out;
  out.translation = Vec3f{translation[0], translation[1], translation[2]};
  out.rotationEulerDegrees = Vec3f{rotation[0], rotation[1], rotation[2]};
  out.scale = Vec3f{scale[0], scale[1], scale[2]};
  return out;
}

Mat4f GizmoAdapter::compose(const GizmoTransformComponents &components) {
  float packed[16] = {};
  const float translation[3] = {components.translation.x, components.translation.y,
                                components.translation.z};
  const float rotation[3] = {components.rotationEulerDegrees.x,
                             components.rotationEulerDegrees.y,
                             components.rotationEulerDegrees.z};
  const float scale[3] = {components.scale.x, components.scale.y,
                          components.scale.z};
  ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, packed);
  return fromFloat16(packed);
}

} // namespace LX_core

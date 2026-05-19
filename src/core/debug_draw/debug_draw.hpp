#pragma once

#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/math/quat.hpp"
#include "core/math/vec.hpp"
#include "core/asset/material_instance.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/visibility_mask.hpp"

#include <functional>
#include <memory>

namespace LX_core::DebugDraw {

struct Color final {
  static Vec4f white();
  static Vec4f red();
  static Vec4f green();
  static Vec4f blue();
  static Vec4f yellow();
};

class LayerScope final {
public:
  explicit LayerScope(VisibilityLayerMask mask);
  ~LayerScope();

  LayerScope(const LayerScope &) = delete;
  LayerScope &operator=(const LayerScope &) = delete;

private:
  VisibilityLayerMask m_previousMask = Layer_EditorOverlay;
};

void reset();
void attachScene(SceneSharedPtr scene);
void setMaterialProvider(
    std::function<MaterialInstanceSharedPtr()> materialProvider);

void beginFrame();
bool endFrame();

void drawLine(Vec3f a, Vec3f b, Vec4f color = Color::white());
void drawTriangle(Vec3f a, Vec3f b, Vec3f c, Vec4f color = Color::white());
void wireSphere(Vec3f center, float radius, Vec4f color = Color::white(),
                int segments = 24);
void wireOctahedron(Vec3f center, float radius, Vec4f color = Color::white());
void wireCircle(Vec3f center, Vec3f normal, float radius,
                Vec4f color = Color::white(), int segments = 24);
void wireBox(const BoundingBox &bounds, Vec4f color = Color::white());
void wireBox(Vec3f center, Vec3f extent, Quatf rotation,
             Vec4f color = Color::white());
void cone(Vec3f apex, Vec3f direction, float length, float halfAngleRad,
          Vec4f color = Color::white(), int segments = 16);
void arrow(Vec3f from, Vec3f to, Vec4f color = Color::white(),
           float headSize = 0.1f);
void axis(const Mat4f &transform, float length = 1.0f);
void frustum(const Mat4f &viewProj, Vec4f color = Color::white());

namespace testing {

usize queuedLineCount();
usize flushedVertexCount(VisibilityLayerMask mask);
usize reservedVertexCapacity(VisibilityLayerMask mask);
usize reservedIndexCapacity(VisibilityLayerMask mask);
usize bufferedVertexCapacity(VisibilityLayerMask mask);
usize bufferedIndexCapacity(VisibilityLayerMask mask);
ResourceCacheIdentity vertexBufferIdentity(VisibilityLayerMask mask);
ResourceCacheIdentity indexBufferIdentity(VisibilityLayerMask mask);
bool hasRenderable(VisibilityLayerMask mask);

} // namespace testing

} // namespace LX_core::DebugDraw

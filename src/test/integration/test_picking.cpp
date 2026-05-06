#include "core/asset/mesh.hpp"
#include "core/math/quat.hpp"
#include "core/math/ray.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

using namespace LX_core;

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

constexpr float kEps = 1e-5f;

bool approx(float a, float b, float eps = kEps) {
  return std::fabs(a - b) <= eps;
}

bool approxVec3(const Vec3f &a, const Vec3f &b, float eps = kEps) {
  return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) &&
         approx(a.z, b.z, eps);
}

MeshSharedPtr makeUnitSquareMesh() {
  auto vb = VertexBuffer<VertexPos>::create(
      std::vector<VertexPos>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}});
  auto ib = IndexBuffer::create({0, 1, 2});
  return Mesh::create(vb, ib, BoundingBox{{0, 0, 0}, {1, 1, 0}});
}

SceneNodeSharedPtr makeBoundedNode(const std::string &name, const Vec3f &translation,
                                   VisibilityLayerMask mask = VisibilityMask_All) {
  auto node = SceneNode::create(name);
  node->addComponent<MeshComponent>(makeUnitSquareMesh());
  node->setTranslation(translation);
  node->setVisibilityLayerMask(mask);
  return node;
}

void testIntersectRayBoxReturnsEntryDistance() {
  const BoundingBox box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
  const Ray ray{{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -2.0f}};
  const auto hit = intersectRayBox(ray, box);
  EXPECT(hit.has_value(), "forward ray must hit box");
  EXPECT(hit.has_value() && approx(*hit, 2.0f),
         "non-unit direction keeps parametric entry distance");
}

void testIntersectRayBoxInsideAndMissCases() {
  const BoundingBox box{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  const auto insideHit =
      intersectRayBox(Ray{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, box);
  EXPECT(insideHit.has_value() && approx(*insideHit, 0.0f),
         "origin inside box returns zero");

  const auto tangentHit =
      intersectRayBox(Ray{{1.0f, 2.0f, 0.5f}, {0.0f, -1.0f, 0.0f}}, box);
  EXPECT(tangentHit.has_value() && approx(*tangentHit, 1.0f),
         "ray tangent to box face still reports first forward contact");

  const auto miss =
      intersectRayBox(Ray{{2.0f, 2.0f, 2.0f}, {1.0f, 0.0f, 0.0f}}, box);
  EXPECT(!miss.has_value(), "ray missing box returns nullopt");
}

void testSceneNodeWorldBoundsFollowTransform() {
  auto node = makeBoundedNode("rotated_box", {3.0f, 4.0f, 0.0f});
  node->setScale({2.0f, 3.0f, 1.0f});
  node->setRotation(
      Quatf::fromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, 3.14159265358979323846f * 0.5f)
          .normalized());

  const BoundingBox localBounds = node->getLocalBounds();
  const BoundingBox worldBounds = node->getWorldBounds();

  EXPECT(localBounds.isValid(), "local mesh bounds stay valid");
  EXPECT(approxVec3(localBounds.min, Vec3f{0.0f, 0.0f, 0.0f}),
         "local bounds min preserved");
  EXPECT(approxVec3(localBounds.max, Vec3f{1.0f, 1.0f, 0.0f}),
         "local bounds max preserved");
  EXPECT(approxVec3(worldBounds.min, Vec3f{0.0f, 4.0f, 0.0f}),
         "world bounds min reflects rotation plus non-uniform scale");
  EXPECT(approxVec3(worldBounds.max, Vec3f{3.0f, 6.0f, 0.0f}),
         "world bounds max reflects rotation plus non-uniform scale");
}

void testScenePickReturnsNearestHitAndRespectsLayerMask() {
  auto nearNode = makeBoundedNode("near_pick", {0.0f, 0.0f, 0.0f}, 0x1u);
  auto farNode = makeBoundedNode("far_pick", {0.0f, 0.0f, -5.0f}, 0x2u);

  auto scene = Scene::create(nearNode);
  scene->addRenderable(farNode);

  const Ray ray{{0.5f, 0.5f, 5.0f}, {0.0f, 0.0f, -1.0f}};

  const auto nearest = scene->pick(ray);
  EXPECT(nearest.has_value(), "scene pick finds at least one hit");
  EXPECT(nearest.has_value() && nearest->node.get() == nearNode.get(),
         "nearest world-space bounds win");
  EXPECT(nearest.has_value() && approx(nearest->distance, 5.0f),
         "nearest hit distance is preserved");

  const auto masked = scene->pick(ray, 0x2u);
  EXPECT(masked.has_value() && masked->node.get() == farNode.get(),
         "layer mask excludes nearer non-overlapping node");
  EXPECT(masked.has_value() && approx(masked->distance, 10.0f),
         "masked pick returns farther compatible hit");
}

void testScenePickSkipsInvalidBounds() {
  auto scene = Scene::create(nullptr);
  auto invalidNode = SceneNode::create("invalid_pick");
  scene->addRenderable(invalidNode);

  const auto hit = scene->pick(Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}});
  EXPECT(!hit.has_value(), "nodes without valid mesh bounds are ignored");
}

void testCameraComponentPickRayUsesOwnerPose() {
  auto cameraNode = SceneNode::create("pick_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  cameraNode->setTranslation({2.0f, 3.0f, 4.0f});
  camera->get().aspect = 1.0f;
  camera->get().fovY = 90.0f;

  const Ray perspectiveRay = camera->get().pickRay({0.0f, 0.0f}, {1.0f, 1.0f});
  EXPECT(approxVec3(perspectiveRay.origin, Vec3f{2.0f, 3.0f, 4.0f}),
         "perspective pick ray starts at eye position");
  EXPECT(approxVec3(perspectiveRay.direction, Vec3f{0.0f, 0.0f, -1.0f}),
         "center perspective ray follows camera forward");

  camera->get().type = CameraType::Orthographic;
  camera->get().left = -2.0f;
  camera->get().right = 2.0f;
  camera->get().bottom = -1.0f;
  camera->get().top = 1.0f;
  camera->get().nearPlane = 0.5f;
  const Ray orthoRay = camera->get().pickRay({0.0f, 0.0f}, {1.0f, 1.0f});
  EXPECT(approxVec3(orthoRay.origin, Vec3f{2.0f, 3.0f, 3.5f}),
         "orthographic center ray originates on near plane");
  EXPECT(approxVec3(orthoRay.direction, Vec3f{0.0f, 0.0f, -1.0f}),
         "orthographic ray direction stays forward");
}

} // namespace

int main() {
  testIntersectRayBoxReturnsEntryDistance();
  testIntersectRayBoxInsideAndMissCases();
  testSceneNodeWorldBoundsFollowTransform();
  testScenePickReturnsNearestHitAndRespectsLayerMask();
  testScenePickSkipsInvalidBounds();
  testCameraComponentPickRayUsesOwnerPose();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: picking tests passed\n";
  return 0;
}

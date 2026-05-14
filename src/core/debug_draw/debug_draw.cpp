#include "core/debug_draw/debug_draw.hpp"

#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LX_core::DebugDraw {

namespace {

constexpr usize kMaxLinesPerFrame = 100000;
constexpr usize kMinBucketCapacity = 256;
constexpr usize kMaxVerticesPerFrame = kMaxLinesPerFrame * 2;
constexpr usize kMaxIndicesPerFrame = kMaxLinesPerFrame * 2;
constexpr const char *kDebugLineShaderName = "debug_line";
constexpr float kPi = 3.14159265358979323846f;

struct DebugLineVertex {
  Vec3f position;
  Vec4f color;

  static const VertexLayout &getLayout() {
    static const VertexLayout layout{
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(DebugLineVertex, position)},
         {"inColor", 1, DataType::Float4, sizeof(Vec4f),
          offsetof(DebugLineVertex, color)}},
        sizeof(DebugLineVertex)};
    return layout;
  }
};

class DebugLineShader final : public IShader {
public:
  explicit DebugLineShader(std::vector<ShaderStageCode> stages)
      : m_stages(std::move(stages)) {
    m_bindings.push_back(ShaderResourceBinding{
        "CameraUBO", 0, 0, ShaderPropertyType::UniformBuffer, 1,
        CameraData::ResourceSize, 0, ShaderStage::Vertex, {}});
    m_vertexInputs.push_back(
        VertexInputAttribute{"inPos", 0, DataType::Float3});
    m_vertexInputs.push_back(
        VertexInputAttribute{"inColor", 1, DataType::Float4});
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  const std::vector<VertexInputAttribute> &getVertexInputs() const override {
    return m_vertexInputs;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &entry : m_bindings) {
      if (entry.set == set && entry.binding == binding) {
        return std::cref(entry);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &entry : m_bindings) {
      if (entry.name == name) {
        return std::cref(entry);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const VertexInputAttribute>>
  findVertexInput(u32 location) const override {
    for (const auto &entry : m_vertexInputs) {
      if (entry.location == location) {
        return std::cref(entry);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return m_hash; }
  std::string getShaderName() const override { return kDebugLineShaderName; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
  std::vector<VertexInputAttribute> m_vertexInputs;
  usize m_hash = 0xD06D06u;
};

struct BucketState {
  VisibilityLayerMask mask = Layer_EditorOverlay;
  SceneNodeSharedPtr node;
  std::shared_ptr<VertexBuffer<DebugLineVertex>> vertexBuffer;
  IndexBufferSharedPtr indexBuffer;
  MeshSharedPtr mesh;
  usize flushedVertexCount = 0;
  usize reservedVertexCount = 0;
  usize reservedIndexCount = 0;
};

struct State {
  SceneSharedPtr scene;
  VisibilityLayerMask currentMask = Layer_EditorOverlay;
  std::unordered_map<VisibilityLayerMask, std::vector<DebugLineVertex>>
      queuedVertices;
  std::unordered_map<VisibilityLayerMask, BucketState> buckets;
  MaterialInstanceSharedPtr material;
  IShaderSharedPtr shader;
  usize acceptedLines = 0;
  bool warnedThisFrame = false;
  bool sceneStructureDirty = false;
};

State &state() {
  static State s;
  return s;
}

Vec3f transformPoint(const Mat4f &matrix, const Vec3f &point) {
  return (matrix * Vec4f{point.x, point.y, point.z, 1.0f}).toVec3();
}

std::vector<u32> makeSequentialIndices(usize vertexCount) {
  std::vector<u32> indices;
  indices.reserve(vertexCount);
  for (usize i = 0; i < vertexCount; ++i) {
    indices.push_back(static_cast<u32>(i));
  }
  return indices;
}

usize nextDebugCapacity(const usize currentCapacity,
                        const usize requiredCapacity,
                        const usize maxCapacity) {
  if (requiredCapacity == 0) {
    return currentCapacity;
  }

  usize capacity = std::max(currentCapacity, kMinBucketCapacity);
  while (capacity < requiredCapacity && capacity < maxCapacity) {
    capacity = std::min(capacity * 2, maxCapacity);
  }

  if (capacity < requiredCapacity) {
    throw std::runtime_error("DebugDraw required capacity exceeds hard frame limit");
  }

  return capacity;
}

std::vector<DebugLineVertex>
padVerticesToCapacity(const std::vector<DebugLineVertex> &vertices,
                      const usize reservedVertexCount) {
  // DebugDraw retains CPU-side payload size at reserved capacity so a fresh
  // backend resource identity allocates large enough GPU buffers on first sync.
  std::vector<DebugLineVertex> padded = vertices;
  padded.reserve(reservedVertexCount);

  const DebugLineVertex filler = vertices.empty()
                                     ? DebugLineVertex{{0.0f, 0.0f, 0.0f},
                                                       {0.0f, 0.0f, 0.0f, 0.0f}}
                                     : vertices.back();
  while (padded.size() < reservedVertexCount) {
    padded.push_back(filler);
  }
  return padded;
}

std::vector<u32> makeDegenerateLineIndices(const usize visibleVertexCount,
                                           const usize reservedVertexCount,
                                           const usize reservedIndexCount) {
  std::vector<u32> indices = makeSequentialIndices(visibleVertexCount);
  indices.reserve(reservedIndexCount);

  if (indices.size() >= reservedIndexCount) {
    return indices;
  }

  const u32 fillerIndex = reservedVertexCount == 0
                              ? 0
                              : static_cast<u32>(std::min(
                                    visibleVertexCount, reservedVertexCount) -
                                    (visibleVertexCount == 0 ? 0 : 1));
  // Extra line-list indices intentionally collapse to zero-length segments so
  // retained capacity does not change visible geometry or bounds.
  while (indices.size() < reservedIndexCount) {
    indices.push_back(fillerIndex);
  }
  return indices;
}

BoundingBox computeBounds(const std::vector<DebugLineVertex> &vertices) {
  BoundingBox bounds;
  for (const auto &vertex : vertices) {
    bounds.merge(vertex.position);
  }
  return bounds;
}

std::vector<ShaderStageCode> loadDebugLineStages() {
  const auto shaderDir = getRuntimeShaderBinaryDir();
  const auto loadStage = [&](const char *suffix,
                             ShaderStage stage) -> ShaderStageCode {
    const auto bytes =
        readFile((shaderDir / (std::string(kDebugLineShaderName) + "." + suffix))
                     .string());
    if ((bytes.size() % sizeof(u32)) != 0) {
      throw std::runtime_error("DebugDraw shader bytecode size is not 4-byte aligned");
    }

    ShaderStageCode code;
    code.stage = stage;
    code.bytecode.resize(bytes.size() / sizeof(u32));
    std::memcpy(code.bytecode.data(), bytes.data(), bytes.size());
    return code;
  };

  return {loadStage("vert.spv", ShaderStage::Vertex),
          loadStage("frag.spv", ShaderStage::Fragment)};
}

MaterialInstanceSharedPtr createMaterial() {
  auto &s = state();
  if (!s.shader) {
    s.shader = std::make_shared<DebugLineShader>(loadDebugLineStages());
  }

  auto tmpl = MaterialTemplate::create(kDebugLineShaderName);
  ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kDebugLineShaderName;
  shaderProgram.shader = s.shader;

  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = shaderProgram;
  passDefinition.renderState.cullMode = CullMode::None;
  passDefinition.renderState.depthTestEnable = true;
  passDefinition.renderState.depthWriteEnable = false;
  passDefinition.renderState.blendEnable = true;
  passDefinition.renderState.srcBlend = BlendFactor::SrcAlpha;
  passDefinition.renderState.dstBlend = BlendFactor::OneMinusSrcAlpha;

  tmpl->setPassDefinition(Pass_DebugOverlay, std::move(passDefinition));
  tmpl->rebuildMaterialInterface();
  return MaterialInstance::create(std::move(tmpl));
}

MaterialInstanceSharedPtr ensureMaterial() {
  auto &s = state();
  if (!s.material) {
    s.material = createMaterial();
  }
  return s.material;
}

BucketState &ensureBucket(VisibilityLayerMask mask) {
  auto &s = state();
  auto it = s.buckets.find(mask);
  if (it != s.buckets.end()) {
    return it->second;
  }

  BucketState bucket;
  bucket.mask = mask;
  bucket.vertexBuffer = VertexBuffer<DebugLineVertex>::create({});
  bucket.indexBuffer =
      IndexBuffer::create({}, PrimitiveTopology::LineList);
  bucket.mesh = Mesh::create(bucket.vertexBuffer, bucket.indexBuffer);
  bucket.node = SceneNode::create(
      "debug_draw_" + std::to_string(static_cast<u32>(mask)));
  bucket.node->setVisibilityLayerMask(mask);
  bucket.node->addComponent<MeshComponent>(bucket.mesh);
  bucket.node->addComponent<MaterialComponent>(ensureMaterial());

  if (!s.scene) {
    throw std::runtime_error("DebugDraw requires an attached scene before creating renderables");
  }
  s.scene->addRenderable(bucket.node);
  s.sceneStructureDirty = true;

  auto [insertedIt, inserted] = s.buckets.emplace(mask, std::move(bucket));
  (void)inserted;
  return insertedIt->second;
}

void rebuildBucketCapacity(BucketState &bucket,
                           const usize reservedVertexCount,
                           const usize reservedIndexCount) {
  auto &s = state();
  const bool replacingExistingResources =
      bucket.vertexBuffer || bucket.indexBuffer || bucket.mesh;
  bucket.vertexBuffer = VertexBuffer<DebugLineVertex>::create({});
  bucket.indexBuffer = IndexBuffer::create({}, PrimitiveTopology::LineList);
  bucket.mesh = Mesh::create(bucket.vertexBuffer, bucket.indexBuffer);
  bucket.mesh->bounds = BoundingBox{};

  const auto meshComponent = bucket.node->getComponent<MeshComponent>();
  if (!meshComponent.has_value()) {
    throw std::runtime_error("DebugDraw bucket missing MeshComponent");
  }
  meshComponent->get().setMesh(bucket.mesh);

  bucket.reservedVertexCount = reservedVertexCount;
  bucket.reservedIndexCount = reservedIndexCount;
  if (replacingExistingResources) {
    s.sceneStructureDirty = true;
  }
}

void updateBucket(BucketState &bucket,
                  const std::vector<DebugLineVertex> &vertices) {
  const usize requiredVertexCount = vertices.size();
  const usize requiredIndexCount = requiredVertexCount;
  if (requiredVertexCount > kMaxVerticesPerFrame ||
      requiredIndexCount > kMaxIndicesPerFrame) {
    throw std::runtime_error("DebugDraw frame exceeded maximum buffered geometry");
  }

  if (requiredVertexCount > bucket.reservedVertexCount ||
      requiredIndexCount > bucket.reservedIndexCount) {
    const usize reservedVertexCount =
        nextDebugCapacity(bucket.reservedVertexCount, requiredVertexCount,
                          kMaxVerticesPerFrame);
    const usize reservedIndexCount =
        nextDebugCapacity(bucket.reservedIndexCount, requiredIndexCount,
                          kMaxIndicesPerFrame);
    rebuildBucketCapacity(bucket, reservedVertexCount, reservedIndexCount);
  }

  bucket.vertexBuffer->update(
      padVerticesToCapacity(vertices, bucket.reservedVertexCount));
  bucket.indexBuffer->update(makeDegenerateLineIndices(
      requiredIndexCount, bucket.reservedVertexCount, bucket.reservedIndexCount));
  bucket.mesh->bounds = computeBounds(vertices);
  bucket.flushedVertexCount = requiredVertexCount;
}

void pushLine(Vec3f a, Vec3f b, Vec4f color) {
  auto &s = state();
  if (s.acceptedLines >= kMaxLinesPerFrame) {
    if (!s.warnedThisFrame) {
      std::cerr << "[WARN] DebugDraw clipped line submissions after "
                << kMaxLinesPerFrame << " lines in one frame\n";
      s.warnedThisFrame = true;
    }
    return;
  }

  auto &bucket = s.queuedVertices[s.currentMask];
  bucket.push_back(DebugLineVertex{a, color});
  bucket.push_back(DebugLineVertex{b, color});
  ++s.acceptedLines;
}

Vec3f choosePerpendicular(const Vec3f &normal) {
  const Vec3f axisX{1.0f, 0.0f, 0.0f};
  const Vec3f axisY{0.0f, 1.0f, 0.0f};
  return std::abs(normal.dot(axisY)) < 0.99f ? axisY : axisX;
}

Mat4f invertMatrix(const Mat4f &matrix) {
  float m[16];
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      m[row * 4 + col] = matrix(row, col);
    }
  }

  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] -
           m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
           m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] -
           m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
           m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] -
           m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] +
           m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] -
           m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] +
           m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] +
           m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] -
           m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det =
      m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::abs(det) < 1e-8f) {
    throw std::runtime_error("DebugDraw::frustum requires invertible viewProj matrix");
  }

  det = 1.0f / det;
  Mat4f result{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      result(row, col) = inv[row * 4 + col] * det;
    }
  }
  return result;
}

} // namespace

Vec4f Color::white() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
Vec4f Color::red() { return {1.0f, 0.0f, 0.0f, 1.0f}; }
Vec4f Color::green() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
Vec4f Color::blue() { return {0.0f, 0.0f, 1.0f, 1.0f}; }
Vec4f Color::yellow() { return {1.0f, 1.0f, 0.0f, 1.0f}; }

LayerScope::LayerScope(VisibilityLayerMask mask) {
  auto &s = state();
  m_previousMask = s.currentMask;
  s.currentMask = mask;
}

LayerScope::~LayerScope() { state().currentMask = m_previousMask; }

void reset() { state() = State{}; }

void attachScene(SceneSharedPtr scene) {
  auto &s = state();
  if (s.scene == scene) {
    return;
  }
  s = State{};
  s.scene = std::move(scene);
}

void beginFrame() {
  auto &s = state();
  s.queuedVertices.clear();
  s.acceptedLines = 0;
  s.warnedThisFrame = false;
  s.currentMask = Layer_EditorOverlay;
}

bool endFrame() {
  auto &s = state();
  if (!s.scene) {
    return false;
  }

  for (const auto &[mask, vertices] : s.queuedVertices) {
    auto &bucket = ensureBucket(mask);
    updateBucket(bucket, vertices);
  }

  for (auto &[mask, bucket] : s.buckets) {
    if (s.queuedVertices.find(mask) == s.queuedVertices.end()) {
      updateBucket(bucket, {});
    }
  }

  const bool dirty = s.sceneStructureDirty;
  s.sceneStructureDirty = false;
  return dirty;
}

void drawLine(Vec3f a, Vec3f b, Vec4f color) { pushLine(a, b, color); }

void drawTriangle(Vec3f a, Vec3f b, Vec3f c, Vec4f color) {
  drawLine(a, b, color);
  drawLine(b, c, color);
  drawLine(c, a, color);
}

void wireCircle(Vec3f center, Vec3f normal, float radius, Vec4f color,
                int segments) {
  if (radius <= 0.0f || segments < 3) {
    return;
  }

  const Vec3f axis = normal.length2() > 0.0f ? normal.normalized()
                                              : Vec3f{0.0f, 1.0f, 0.0f};
  const Vec3f tangent =
      choosePerpendicular(axis).cross(axis).normalized();
  const Vec3f bitangent = axis.cross(tangent).normalized();

  for (int i = 0; i < segments; ++i) {
    const float a0 = (2.0f * kPi * static_cast<float>(i)) /
                     static_cast<float>(segments);
    const float a1 =
        (2.0f * kPi * static_cast<float>(i + 1)) /
        static_cast<float>(segments);
    const Vec3f p0 =
        center + radius * (std::cos(a0) * tangent + std::sin(a0) * bitangent);
    const Vec3f p1 =
        center + radius * (std::cos(a1) * tangent + std::sin(a1) * bitangent);
    drawLine(p0, p1, color);
  }
}

void wireSphere(Vec3f center, float radius, Vec4f color, int segments) {
  wireCircle(center, Vec3f{1.0f, 0.0f, 0.0f}, radius, color, segments);
  wireCircle(center, Vec3f{0.0f, 1.0f, 0.0f}, radius, color, segments);
  wireCircle(center, Vec3f{0.0f, 0.0f, 1.0f}, radius, color, segments);
}

void wireOctahedron(Vec3f center, float radius, Vec4f color) {
  if (radius <= 0.0f) {
    return;
  }

  const Vec3f top = center + Vec3f{0.0f, radius, 0.0f};
  const Vec3f bottom = center + Vec3f{0.0f, -radius, 0.0f};
  const Vec3f right = center + Vec3f{radius, 0.0f, 0.0f};
  const Vec3f left = center + Vec3f{-radius, 0.0f, 0.0f};
  const Vec3f front = center + Vec3f{0.0f, 0.0f, radius};
  const Vec3f back = center + Vec3f{0.0f, 0.0f, -radius};

  drawLine(top, right, color);
  drawLine(top, front, color);
  drawLine(top, left, color);
  drawLine(top, back, color);
  drawLine(bottom, right, color);
  drawLine(bottom, front, color);
  drawLine(bottom, left, color);
  drawLine(bottom, back, color);
  drawLine(right, front, color);
  drawLine(front, left, color);
  drawLine(left, back, color);
  drawLine(back, right, color);
}

void wireBox(const BoundingBox &bounds, Vec4f color) {
  if (!bounds.isValid()) {
    return;
  }

  const Vec3f min = bounds.min;
  const Vec3f max = bounds.max;
  const Vec3f p000{min.x, min.y, min.z};
  const Vec3f p001{min.x, min.y, max.z};
  const Vec3f p010{min.x, max.y, min.z};
  const Vec3f p011{min.x, max.y, max.z};
  const Vec3f p100{max.x, min.y, min.z};
  const Vec3f p101{max.x, min.y, max.z};
  const Vec3f p110{max.x, max.y, min.z};
  const Vec3f p111{max.x, max.y, max.z};

  drawLine(p000, p001, color);
  drawLine(p001, p011, color);
  drawLine(p011, p010, color);
  drawLine(p010, p000, color);

  drawLine(p100, p101, color);
  drawLine(p101, p111, color);
  drawLine(p111, p110, color);
  drawLine(p110, p100, color);

  drawLine(p000, p100, color);
  drawLine(p001, p101, color);
  drawLine(p010, p110, color);
  drawLine(p011, p111, color);
}

void wireBox(Vec3f center, Vec3f extent, Quatf rotation, Vec4f color) {
  const Vec3f corners[8] = {
      {-extent.x, -extent.y, -extent.z},
      {-extent.x, -extent.y, extent.z},
      {-extent.x, extent.y, -extent.z},
      {-extent.x, extent.y, extent.z},
      {extent.x, -extent.y, -extent.z},
      {extent.x, -extent.y, extent.z},
      {extent.x, extent.y, -extent.z},
      {extent.x, extent.y, extent.z},
  };

  Vec3f world[8];
  for (int i = 0; i < 8; ++i) {
    world[i] = center + rotation.normalized().rotate(corners[i]);
  }

  drawLine(world[0], world[1], color);
  drawLine(world[1], world[3], color);
  drawLine(world[3], world[2], color);
  drawLine(world[2], world[0], color);
  drawLine(world[4], world[5], color);
  drawLine(world[5], world[7], color);
  drawLine(world[7], world[6], color);
  drawLine(world[6], world[4], color);
  drawLine(world[0], world[4], color);
  drawLine(world[1], world[5], color);
  drawLine(world[2], world[6], color);
  drawLine(world[3], world[7], color);
}

void cone(Vec3f apex, Vec3f direction, float length, float halfAngleRad,
          Vec4f color, int segments) {
  if (length <= 0.0f || segments < 3 || direction.length2() == 0.0f) {
    return;
  }

  const Vec3f dir = direction.normalized();
  const Vec3f baseCenter = apex + dir * length;
  const float radius = std::tan(halfAngleRad) * length;
  const Vec3f tangent = choosePerpendicular(dir).cross(dir).normalized();
  const Vec3f bitangent = dir.cross(tangent).normalized();

  std::vector<Vec3f> ring;
  ring.reserve(static_cast<usize>(segments));
  for (int i = 0; i < segments; ++i) {
    const float angle =
        (2.0f * kPi * static_cast<float>(i)) /
        static_cast<float>(segments);
    ring.push_back(baseCenter +
                   radius * (std::cos(angle) * tangent +
                             std::sin(angle) * bitangent));
  }

  for (int i = 0; i < segments; ++i) {
    const Vec3f &p0 = ring[static_cast<usize>(i)];
    const Vec3f &p1 = ring[static_cast<usize>((i + 1) % segments)];
    drawLine(p0, p1, color);
    drawLine(apex, p0, color);
  }
}

void arrow(Vec3f from, Vec3f to, Vec4f color, float headSize) {
  drawLine(from, to, color);
  const Vec3f dir = to - from;
  const float length = dir.length();
  if (length <= 1e-6f) {
    return;
  }

  const Vec3f forward = dir / length;
  const float headLength = std::min(length * 0.25f, headSize * length);
  const Vec3f headBase = to - forward * headLength;
  const Vec3f tangent = choosePerpendicular(forward).cross(forward).normalized();
  const Vec3f bitangent = forward.cross(tangent).normalized();
  const float headRadius = headLength * 0.4f;

  drawLine(to, headBase + tangent * headRadius, color);
  drawLine(to, headBase - tangent * headRadius, color);
  drawLine(to, headBase + bitangent * headRadius, color);
  drawLine(to, headBase - bitangent * headRadius, color);
}

void axis(const Mat4f &transform, float length) {
  const Vec3f origin = transformPoint(transform, Vec3f{0.0f, 0.0f, 0.0f});
  arrow(origin, transformPoint(transform, Vec3f{length, 0.0f, 0.0f}),
        Color::red());
  arrow(origin, transformPoint(transform, Vec3f{0.0f, length, 0.0f}),
        Color::green());
  arrow(origin, transformPoint(transform, Vec3f{0.0f, 0.0f, length}),
        Color::blue());
}

void frustum(const Mat4f &viewProj, Vec4f color) {
  const Mat4f invViewProj = invertMatrix(viewProj);
  const Vec4f clipCorners[8] = {
      {-1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, -1.0f, 0.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},  {1.0f, 1.0f, 0.0f, 1.0f},
      {-1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f, 1.0f},
      {-1.0f, 1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f, 1.0f},
  };

  Vec3f corners[8];
  for (int i = 0; i < 8; ++i) {
    corners[i] = (invViewProj * clipCorners[i]).toVec3();
  }

  drawLine(corners[0], corners[1], color);
  drawLine(corners[1], corners[3], color);
  drawLine(corners[3], corners[2], color);
  drawLine(corners[2], corners[0], color);
  drawLine(corners[4], corners[5], color);
  drawLine(corners[5], corners[7], color);
  drawLine(corners[7], corners[6], color);
  drawLine(corners[6], corners[4], color);
  drawLine(corners[0], corners[4], color);
  drawLine(corners[1], corners[5], color);
  drawLine(corners[2], corners[6], color);
  drawLine(corners[3], corners[7], color);
}

usize testing::queuedLineCount() { return state().acceptedLines; }

usize testing::flushedVertexCount(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  return it == state().buckets.end() ? 0 : it->second.flushedVertexCount;
}

usize testing::reservedVertexCapacity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  return it == state().buckets.end() ? 0 : it->second.reservedVertexCount;
}

usize testing::reservedIndexCapacity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  return it == state().buckets.end() ? 0 : it->second.reservedIndexCount;
}

usize testing::bufferedVertexCapacity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  if (it == state().buckets.end() || !it->second.vertexBuffer) {
    return 0;
  }
  return it->second.vertexBuffer->getVertexCount();
}

usize testing::bufferedIndexCapacity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  if (it == state().buckets.end() || !it->second.indexBuffer) {
    return 0;
  }
  return it->second.indexBuffer->indexCount();
}

ResourceCacheIdentity testing::vertexBufferIdentity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  if (it == state().buckets.end() || !it->second.vertexBuffer) {
    return 0;
  }
  return it->second.vertexBuffer->getBackendCacheIdentity();
}

ResourceCacheIdentity testing::indexBufferIdentity(VisibilityLayerMask mask) {
  auto it = state().buckets.find(mask);
  if (it == state().buckets.end() || !it->second.indexBuffer) {
    return 0;
  }
  return it->second.indexBuffer->getBackendCacheIdentity();
}

bool testing::hasRenderable(VisibilityLayerMask mask) {
  return state().buckets.find(mask) != state().buckets.end();
}

} // namespace LX_core::DebugDraw

#include "gltf_mesh_loader.hpp"

#include "cgltf/cgltf.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace infra {

struct GLTFLoader::Impl {
  std::vector<LX_core::Vec3f> positions;
  std::vector<LX_core::Vec3f> normals;
  std::vector<LX_core::Vec2f> texCoords;
  std::vector<LX_core::Vec4f> tangents;
  std::vector<MeshIndex32> indices;
  GLTFPbrMaterial material;
};

namespace {

const char *cgltfResultToString(cgltf_result r) {
  switch (r) {
  case cgltf_result_success:
    return "success";
  case cgltf_result_data_too_short:
    return "data_too_short";
  case cgltf_result_unknown_format:
    return "unknown_format";
  case cgltf_result_invalid_json:
    return "invalid_json";
  case cgltf_result_invalid_gltf:
    return "invalid_gltf";
  case cgltf_result_invalid_options:
    return "invalid_options";
  case cgltf_result_file_not_found:
    return "file_not_found";
  case cgltf_result_io_error:
    return "io_error";
  case cgltf_result_out_of_memory:
    return "out_of_memory";
  case cgltf_result_legacy_gltf:
    return "legacy_gltf";
  default:
    return "unknown";
  }
}

[[noreturn]] void fail(const std::string &file, const std::string &message) {
  std::ostringstream oss;
  oss << "[GLTFLoader " << file << "] " << message;
  throw std::runtime_error(oss.str());
}

[[noreturn]] void failCgltf(const std::string &file, const char *stage,
                             cgltf_result r) {
  std::ostringstream oss;
  oss << stage << " failed (" << cgltfResultToString(r) << ")";
  fail(file, oss.str());
}

// REQ-011: texture URI is preserved verbatim — relative to the .gltf dir.
// data URI / buffer_view-backed (inline/base64) images are explicitly
// rejected so callers see a clear error instead of a silently-missing path.
std::string extractTextureUri(const std::string &file,
                              const cgltf_texture_view &view,
                              const char *label) {
  if (!view.texture) {
    return {};
  }
  const cgltf_image *image = view.texture->image;
  if (!image) {
    return {};
  }
  if (image->uri == nullptr) {
    if (image->buffer_view != nullptr) {
      fail(file, std::string("inline / buffer_view image is not supported "
                             "for ")
                     + label);
    }
    return {};
  }
  const std::string uri(image->uri);
  if (uri.rfind("data:", 0) == 0) {
    fail(file,
         std::string("data URI image is not supported for ") + label);
  }
  return uri;
}

const cgltf_attribute *findAttribute(const cgltf_primitive &prim,
                                     cgltf_attribute_type type, int setIndex) {
  for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
    const cgltf_attribute &a = prim.attributes[i];
    if (a.type == type && a.index == setIndex) {
      return &a;
    }
  }
  return nullptr;
}

void readVec3Attribute(const std::string &file, const cgltf_accessor &acc,
                       std::vector<LX_core::Vec3f> &out, const char *label) {
  out.resize(acc.count);
  for (cgltf_size i = 0; i < acc.count; ++i) {
    float tmp[3] = {0.0f, 0.0f, 0.0f};
    if (!cgltf_accessor_read_float(&acc, i, tmp, 3)) {
      fail(file, std::string("failed to read ") + label + " element "
                     + std::to_string(i));
    }
    out[i] = LX_core::Vec3f(tmp[0], tmp[1], tmp[2]);
  }
}

void readVec2Attribute(const std::string &file, const cgltf_accessor &acc,
                       std::vector<LX_core::Vec2f> &out, const char *label) {
  out.resize(acc.count);
  for (cgltf_size i = 0; i < acc.count; ++i) {
    float tmp[2] = {0.0f, 0.0f};
    if (!cgltf_accessor_read_float(&acc, i, tmp, 2)) {
      fail(file, std::string("failed to read ") + label + " element "
                     + std::to_string(i));
    }
    out[i] = LX_core::Vec2f(tmp[0], tmp[1]);
  }
}

void readVec4Attribute(const std::string &file, const cgltf_accessor &acc,
                       std::vector<LX_core::Vec4f> &out, const char *label) {
  out.resize(acc.count);
  for (cgltf_size i = 0; i < acc.count; ++i) {
    float tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!cgltf_accessor_read_float(&acc, i, tmp, 4)) {
      fail(file, std::string("failed to read ") + label + " element "
                     + std::to_string(i));
    }
    out[i] = LX_core::Vec4f(tmp[0], tmp[1], tmp[2], tmp[3]);
  }
}

void readIndices(const std::string &file, const cgltf_accessor &acc,
                 std::vector<MeshIndex32> &out) {
  if (acc.component_type != cgltf_component_type_r_8u
      && acc.component_type != cgltf_component_type_r_16u
      && acc.component_type != cgltf_component_type_r_32u) {
    fail(file, "unsupported index component type (must be u8/u16/u32)");
  }
  out.resize(acc.count);
  for (cgltf_size i = 0; i < acc.count; ++i) {
    cgltf_size idx = cgltf_accessor_read_index(&acc, i);
    out[i] = static_cast<MeshIndex32>(idx);
  }
}

void extractMaterial(const std::string &file, const cgltf_material *m,
                     GLTFPbrMaterial &out) {
  // Struct-initialized defaults survive if there is no material at all.
  if (m == nullptr) {
    return;
  }

  if (m->has_pbr_metallic_roughness) {
    const cgltf_pbr_metallic_roughness &pbr = m->pbr_metallic_roughness;
    out.baseColorFactor = LX_core::Vec4f(
        pbr.base_color_factor[0], pbr.base_color_factor[1],
        pbr.base_color_factor[2], pbr.base_color_factor[3]);
    out.metallicFactor = pbr.metallic_factor;
    out.roughnessFactor = pbr.roughness_factor;
    out.baseColorTexture =
        extractTextureUri(file, pbr.base_color_texture, "baseColorTexture");
    out.metallicRoughnessTexture = extractTextureUri(
        file, pbr.metallic_roughness_texture, "metallicRoughnessTexture");
  }

  out.emissiveFactor = LX_core::Vec3f(m->emissive_factor[0],
                                      m->emissive_factor[1],
                                      m->emissive_factor[2]);
  out.normalTexture = extractTextureUri(file, m->normal_texture, "normalTexture");
  out.occlusionTexture =
      extractTextureUri(file, m->occlusion_texture, "occlusionTexture");
  out.emissiveTexture =
      extractTextureUri(file, m->emissive_texture, "emissiveTexture");
}

struct CgltfDeleter {
  void operator()(cgltf_data *d) const noexcept {
    if (d) {
      cgltf_free(d);
    }
  }
};
using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

} // namespace

GLTFLoader::GLTFLoader() : pImpl(new Impl) {}

GLTFLoader::~GLTFLoader() { delete pImpl; }

void GLTFLoader::load(const std::string &filename) {
  // Reset any previous load so repeated use on the same instance is safe.
  pImpl->positions.clear();
  pImpl->normals.clear();
  pImpl->texCoords.clear();
  pImpl->tangents.clear();
  pImpl->indices.clear();
  pImpl->material = GLTFPbrMaterial{};

  cgltf_options options{};
  cgltf_data *rawData = nullptr;
  cgltf_result res = cgltf_parse_file(&options, filename.c_str(), &rawData);
  if (res != cgltf_result_success) {
    failCgltf(filename, "cgltf_parse_file", res);
  }
  CgltfDataPtr data(rawData);

  res = cgltf_load_buffers(&options, data.get(), filename.c_str());
  if (res != cgltf_result_success) {
    failCgltf(filename, "cgltf_load_buffers", res);
  }

  if (data->meshes_count == 0) {
    fail(filename, "no meshes in glTF document");
  }
  if (data->meshes[0].primitives_count == 0) {
    fail(filename, "meshes[0] has no primitives");
  }
  if (data->meshes_count > 1) {
    std::cerr << "[GLTFLoader] warning: " << filename << " has "
              << data->meshes_count
              << " meshes, only meshes[0] will be loaded\n";
  }
  if (data->meshes[0].primitives_count > 1) {
    std::cerr << "[GLTFLoader] warning: " << filename
              << " meshes[0] has " << data->meshes[0].primitives_count
              << " primitives, only primitives[0] will be loaded\n";
  }

  const cgltf_primitive &prim = data->meshes[0].primitives[0];
  if (prim.type != cgltf_primitive_type_triangles) {
    fail(filename,
         "primitives[0] is not triangles (cgltf_primitive_type != triangles)");
  }

  const cgltf_attribute *posAttr =
      findAttribute(prim, cgltf_attribute_type_position, 0);
  if (posAttr == nullptr || posAttr->data == nullptr) {
    fail(filename, "primitives[0] is missing POSITION attribute");
  }
  readVec3Attribute(filename, *posAttr->data, pImpl->positions, "POSITION");

  if (const cgltf_attribute *normAttr =
          findAttribute(prim, cgltf_attribute_type_normal, 0)) {
    if (normAttr->data) {
      readVec3Attribute(filename, *normAttr->data, pImpl->normals, "NORMAL");
    }
  }

  if (const cgltf_attribute *uvAttr =
          findAttribute(prim, cgltf_attribute_type_texcoord, 0)) {
    if (uvAttr->data) {
      readVec2Attribute(filename, *uvAttr->data, pImpl->texCoords,
                        "TEXCOORD_0");
    }
  }

  if (const cgltf_attribute *tanAttr =
          findAttribute(prim, cgltf_attribute_type_tangent, 0)) {
    if (tanAttr->data) {
      readVec4Attribute(filename, *tanAttr->data, pImpl->tangents, "TANGENT");
    }
  }

  if (prim.indices != nullptr) {
    readIndices(filename, *prim.indices, pImpl->indices);
  } else {
    // Non-indexed primitive: synthesize a sequential index buffer so the
    // caller can always rely on indices.size() % 3 == 0.
    pImpl->indices.resize(pImpl->positions.size());
    for (size_t i = 0; i < pImpl->positions.size(); ++i) {
      pImpl->indices[i] = static_cast<MeshIndex32>(i);
    }
  }

  extractMaterial(filename, prim.material, pImpl->material);
}

const std::vector<LX_core::Vec3f> &GLTFLoader::getPositions() const {
  return pImpl->positions;
}

const std::vector<LX_core::Vec3f> &GLTFLoader::getNormals() const {
  return pImpl->normals;
}

const std::vector<LX_core::Vec2f> &GLTFLoader::getTexCoords() const {
  return pImpl->texCoords;
}

const std::vector<MeshIndex32> &GLTFLoader::getIndices() const {
  return pImpl->indices;
}

const std::vector<LX_core::Vec4f> &GLTFLoader::getTangents() const {
  return pImpl->tangents;
}

const GLTFPbrMaterial &GLTFLoader::getMaterial() const {
  return pImpl->material;
}

} // namespace infra

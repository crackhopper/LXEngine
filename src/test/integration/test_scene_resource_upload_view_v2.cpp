#include "core/asset/material_contract.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/render_effect.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/resource/resource_metadata.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/ibl_bake_manifest.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct TestVertex final {
  Vec3f position;

  static VertexLayout getLayout() {
    return VertexLayout(std::vector<VertexLayoutItem>{VertexLayoutItem{
                            "position", 0, DataType::Float3, sizeof(Vec3f), 0}},
                        sizeof(TestVertex));
  }
};

MeshBufferUniquePtr makeTriangleMesh() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(vb, ib,
                            BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

MaterialContractReflection makeMaterialContract(const char *uri,
                                                const char *type,
                                                const char *reflectionHash) {
  MaterialContractReflection contract;
  contract.sourceUri = ResourceUri(uri);
  contract.declaredType = type;
  contract.reflectionHash = reflectionHash;
  contract.storageAbiHash = "storage-v1";
  contract.accessorAbiHash = "material-surface-v1";
  return contract;
}

MaterialContractStorageField makeStorageField(const char *name,
                                              const char *parameterName) {
  MaterialContractStorageField field;
  field.name = name;
  field.type = MaterialContractStorageFieldType::Vec4;
  field.inputKind = MaterialContractStorageInputKind::ParameterValue;
  field.parameterName = parameterName;
  field.defaultValue = Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
  return field;
}

MaterialContractStorageField makeTextureSlotField(const char *name,
                                                  const char *parameterName) {
  MaterialContractStorageField field;
  field.name = name;
  field.type = MaterialContractStorageFieldType::TextureSlot;
  field.inputKind = MaterialContractStorageInputKind::ParameterTexture;
  field.parameterName = parameterName;
  field.defaultTextureSemantic = "white";
  return field;
}

MaterialInstanceUniquePtr
makeSourceMaterial(MaterialContractReflection contract) {
  auto material = MaterialInstance::createUnique(
      MaterialTemplate::create(contract.declaredType));
  material->setBsdfType(contract.declaredType);
  material->setMaterialSourceUri(contract.sourceUri);
  material->setMaterialSourceSignature(contract.sourceSignature());
  material->setMaterialSourceReflectionHash(contract.reflectionHash);
  material->setMaterialContractReflection(std::move(contract));
  return material;
}

void registerObject(SceneResourceTable &table, MeshHandle mesh,
                    MaterialHandle material) {
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);
}

void requireSceneRenderableGeometry(RenderPassNode &pass) {
  pass.input.geometry = RenderPathGeometryContract{};
}

bool buildUploadThrows(SceneResourceTable &table,
                       const std::string &expectedText) {
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    return std::string(error.what()).find(expectedText) != std::string::npos;
  }
  return false;
}

const SceneSourceLocalMaterialStorageView *
findSourceStorage(const SceneResourceTableUploadView &view,
                  StringID sourceSignature) {
  const auto found = std::find_if(
      view.sourceMaterialStorages.begin(), view.sourceMaterialStorages.end(),
      [sourceSignature](const SceneSourceLocalMaterialStorageView &storage) {
        return storage.sourceSignature == sourceSignature;
      });
  return found == view.sourceMaterialStorages.end() ? nullptr : &*found;
}

const IGpuResource *findBindingResource(const DescriptorResourceList &resources,
                                        StringID bindingName) {
  for (const DescriptorResourceRef &resource : resources) {
    if (!resource.isResource() || !resource.resource().isValid()) {
      continue;
    }
    if (resource.getBindingName() == bindingName) {
      return &resource.resource().get();
    }
  }
  return nullptr;
}

bool sourceRecordRangeHasContiguousLocalIndices(
    const SceneResourceTableUploadView &view,
    const SceneSourceLocalMaterialStorageView &storage) {
  if (storage.recordOffset + storage.recordCount >
      view.sourceMaterialRecords.size()) {
    return false;
  }
  for (u32 i = 0; i < storage.recordCount; ++i) {
    if (view.sourceMaterialRecords[storage.recordOffset + i]
            .sourceLocalMaterialIndex != i) {
      return false;
    }
  }
  return true;
}

std::vector<ResourceUri> shaderSourceFixture() {
  return {
      ResourceUri("memory://shaders/surface_lit.vert"),
      ResourceUri("memory://shaders/surface_lit.frag"),
  };
}

Sh9IrradiancePayload sh9PayloadFixture(float scale = 1.0f) {
  Sh9IrradiancePayload payload;
  for (u32 i = 0; i < payload.coefficients.size(); ++i) {
    const float value = scale * static_cast<float>(i + 1u);
    payload.coefficients[i] = Vec3f{value, value + 0.25f, value + 0.5f};
  }
  return payload;
}

CombinedTextureSamplerSharedPtr makeTexturePayload(TextureDesc desc,
                                                   StringID binding) {
  auto sampler =
      std::make_shared<CombinedTextureSampler>(std::make_shared<Texture>(
          desc, std::vector<u8>(expectedTextureByteCount(desc), 0x7fu)));
  sampler->setBindingName(binding);
  return sampler;
}

CombinedTextureSamplerUniquePtr makeSolidHdrCubePayload(StringID binding) {
  TextureDesc desc;
  desc.width = 1;
  desc.height = 1;
  desc.format = TextureFormat::RGBA16Float;
  desc.content = TextureContent::Environment;
  desc.dimension = TextureDimension::TextureCube;
  desc.mipLevels = 1;
  desc.arrayLayers = 6;
  std::vector<u8> bytes(expectedTextureByteCount(desc), 0u);
  for (usize offset = 0; offset < bytes.size(); offset += 8u) {
    bytes[offset + 0u] = 0x00u;
    bytes[offset + 1u] = 0x3cu;
    bytes[offset + 2u] = 0x00u;
    bytes[offset + 3u] = 0x3cu;
    bytes[offset + 4u] = 0x00u;
    bytes[offset + 5u] = 0x3cu;
    bytes[offset + 6u] = 0x00u;
    bytes[offset + 7u] = 0x3cu;
  }
  auto sampler = std::make_unique<CombinedTextureSampler>(
      std::make_shared<Texture>(desc, std::move(bytes)));
  sampler->setBindingName(binding);
  return sampler;
}

CombinedTextureSamplerSharedPtr specularPrefilteredPayloadFixture() {
  TextureDesc desc;
  desc.width = 4;
  desc.height = 4;
  desc.format = TextureFormat::RGBA16Float;
  desc.content = TextureContent::Environment;
  desc.dimension = TextureDimension::TextureCube;
  desc.mipLevels = 3;
  desc.arrayLayers = 6;
  return makeTexturePayload(desc, StringID("PrefilteredEnvMap"));
}

CombinedTextureSamplerSharedPtr irradianceMapPayloadFixture() {
  TextureDesc desc;
  desc.width = 4;
  desc.height = 4;
  desc.format = TextureFormat::RGBA16Float;
  desc.content = TextureContent::Environment;
  desc.dimension = TextureDimension::TextureCube;
  desc.mipLevels = 1;
  desc.arrayLayers = 6;
  return makeTexturePayload(desc, StringID("IrradianceMap"));
}

CombinedTextureSamplerSharedPtr brdfLutPayloadFixture() {
  TextureDesc desc;
  desc.width = 256;
  desc.height = 256;
  desc.format = TextureFormat::RG16Float;
  desc.content = TextureContent::Data;
  desc.dimension = TextureDimension::Texture2D;
  desc.mipLevels = 1;
  desc.arrayLayers = 1;
  return makeTexturePayload(desc, StringID("BrdfLut"));
}

IblEnvironmentActivationPayload iblActivationPayloadFixture(u64 generation) {
  IblEnvironmentActivationPayload payload;
  payload.generation = generation;
  payload.diffuseSh = sh9PayloadFixture();
  payload.specularPrefilteredCubemap = specularPrefilteredPayloadFixture();
  payload.standardPbrBrdfLut = brdfLutPayloadFixture();
  return payload;
}

IblEnvironmentResources iblDescriptorResourceFixture() {
  IblEnvironmentResources resources;
  resources.irradianceCubemap = irradianceMapPayloadFixture();
  resources.prefilteredRadianceCubemap = specularPrefilteredPayloadFixture();
  resources.brdfLut = brdfLutPayloadFixture();
  resources.environmentUbo = std::make_unique<EnvironmentData>(1.0f, 3.0f);
  return resources;
}

void testBuiltinDefaultTexturesAreStableSceneResources() {
  SceneResourceTable table;
  const ResourceUri white("builtin://textures/default/white");
  const ResourceUri black("builtin://textures/default/black");
  const ResourceUri flatNormal("builtin://textures/default/flat-normal");

  const auto whiteHandle = table.findTexture(white);
  const auto blackHandle = table.findTexture(black);
  const auto flatNormalHandle = table.findTexture(flatNormal);
  EXPECT(whiteHandle.has_value(),
         "white default texture should have stable resource identity");
  EXPECT(blackHandle.has_value(),
         "black default texture should have stable resource identity");
  EXPECT(flatNormalHandle.has_value(),
         "flat normal default texture should have stable resource identity");
  EXPECT(table.textureCount() == 3,
         "new table should register exactly the three builtin default "
         "textures");

  const SceneResourceTableUploadView firstView = table.buildUploadView();
  EXPECT(firstView.textures.size() == 3,
         "default textures should enter upload texture table");

  const SceneResourceTableUploadView secondView = table.buildUploadView();
  EXPECT(secondView.textures.size() == 3,
         "rebuilding upload view should not duplicate default texture slots");
  EXPECT(table.textureCount() == 3,
         "rebuilding upload view should not duplicate default resources");
}

RenderFeature makeEnvironmentLightingFeature(ResourceUri uri) {
  RenderFeature feature;
  feature.name = "EnvironmentLighting";
  feature.feature = "environmentLighting";
  feature.parameters["environmentMap"] = RenderFeatureParameter{
      .kind = "textureCube",
      .uri = std::move(uri),
      .valueType = "linear-radiance",
      .binding = "SkyboxMap",
      .required = true,
  };
  feature.parameters["color"] = RenderFeatureParameter{
      .kind = "vec3",
      .value = "[0.08, 0.08, 0.10]",
      .binding = "EnvironmentLightingUBO",
      .member = "color",
      .required = true,
  };
  feature.parameters["intensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "EnvironmentLightingUBO",
      .member = "intensity",
      .required = true,
  };
  feature.parameters["rotation"] = RenderFeatureParameter{
      .kind = "float",
      .value = "0.0",
      .binding = "EnvironmentLightingUBO",
      .member = "rotation",
      .required = true,
  };
  return feature;
}

RenderFeature makeSurfaceLightingFeature() {
  RenderFeature feature;
  feature.name = "SurfaceLighting";
  feature.feature = "surfaceLighting";
  feature.level = RenderFeatureLevel::Shader;
  feature.shader = RenderFeatureShaderContract{
      .uri = ResourceUri("features/surface_lighting"),
  };
  feature.parameters["enableIblLighting"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "true",
      .binding = "SurfaceLightingUBO",
      .member = "enableIblLighting",
      .required = true,
  };
  feature.parameters["diffuseIblIntensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "diffuseIblIntensity",
      .required = true,
  };
  feature.parameters["specularIblIntensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "SurfaceLightingUBO",
      .member = "specularIblIntensity",
      .required = true,
  };
  feature.parameters["environmentIblReady"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "environmentIblReady",
      .required = true,
  };
  feature.parameters["standardPbrIblReady"] = RenderFeatureParameter{
      .kind = "bool",
      .value = "false",
      .binding = "SurfaceLightingUBO",
      .member = "standardPbrIblReady",
      .required = true,
  };
  return feature;
}

const SurfaceLightingData::Param *
findSurfaceLightingParam(const SceneResourceTable &table,
                         RenderFeatureHandle feature) {
  for (const GpuResourceRef &resource :
       table.getSurfaceLightingResources(feature)) {
    if (resource.isValid() &&
        resource.getBindingName() == StringID("SurfaceLightingUBO") &&
        resource.get().getByteSize() == sizeof(SurfaceLightingData::Param)) {
      return static_cast<const SurfaceLightingData::Param *>(
          resource.get().getRawData());
    }
  }
  return nullptr;
}

void testEnvironmentFeatureBuiltinWhiteCubeRegistersLiveSkyboxMap() {
  SceneResourceTable table;
  const RenderFeatureHandle handle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting.render-feature"),
      makeEnvironmentLightingFeature(ResourceUri("builtin:env/white_cube")));
  EXPECT(handle.isValid(), "environment feature should register");

  const auto resources = table.getEnvironmentLightingResources();
  const auto hasBinding = [&](StringID bindingName) {
    return std::any_of(resources.begin(), resources.end(),
                       [&](const GpuResourceRef &resource) {
                         return resource.isValid() &&
                                resource.getBindingName() == bindingName;
                       });
  };
  EXPECT(hasBinding(StringID("SkyboxMap")),
         "builtin white cube should register live SkyboxMap");
  EXPECT(hasBinding(StringID("EnvironmentLightingUBO")),
         "environment feature should register EnvironmentLightingUBO");
  EXPECT(!hasBinding(StringID("EnvironmentLightingFiniteBoxUBO")),
         "environment feature should not register retired finite box bounds "
         "UBO");
}

void testEnvironmentFeatureMissingUriDoesNotRegisterSkyboxMap() {
  SceneResourceTable table;
  RenderFeature feature = makeEnvironmentLightingFeature(ResourceUri{});
  const RenderFeatureHandle handle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting_missing_uri"),
      std::move(feature));
  EXPECT(handle.isValid(), "environment feature payload should register");
  const auto resources = table.getEnvironmentLightingResources();
  EXPECT(resources.empty(),
         "missing environmentMap.uri must not create default SkyboxMap");
}

void testEnvironmentFeatureHdrTextureCubeActivatesSurfaceLightingIbl() {
  SceneResourceTable table;
  const RenderFeatureHandle surfaceHandle = table.registerRenderFeature(
      ResourceUri("memory://features/surface_lighting.render-feature"),
      makeSurfaceLightingFeature());
  EXPECT(surfaceHandle.isValid(), "surface lighting feature should register");

  const SurfaceLightingData::Param *before =
      findSurfaceLightingParam(table, surfaceHandle);
  EXPECT(before != nullptr, "surface lighting UBO should exist before env");
  if (before != nullptr) {
    EXPECT(before->environmentIblReady == 0u,
           "surface lighting must start without active environment IBL");
    EXPECT(before->standardPbrIblReady == 0u,
           "surface lighting must start without active standard-pbr IBL");
  }

  const ResourceUri envUri("memory://env/live-neutral-specular.ktx2");
  const TextureHandle envTexture =
      table.registerTexture(envUri, makeSolidHdrCubePayload(StringID{}));
  EXPECT(envTexture.isValid(), "HDR cubemap texture should register");

  const RenderFeatureHandle envHandle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting.render-feature"),
      makeEnvironmentLightingFeature(envUri));
  EXPECT(envHandle.isValid(), "environment lighting feature should register");

  const SceneResourceTableUploadView uploadView = table.buildUploadView();
  EXPECT(uploadView.activeIblGeneration != 0u,
         "live HDR textureCube environment feature should activate IBL");

  const SurfaceLightingData::Param *after =
      findSurfaceLightingParam(table, surfaceHandle);
  EXPECT(after != nullptr, "surface lighting UBO should remain available");
  if (after != nullptr) {
    EXPECT(after->environmentIblReady == 1u,
           "HDR textureCube environment feature should publish environment "
           "IBL readiness");
    EXPECT(after->standardPbrIblReady == 1u,
           "HDR textureCube environment feature should publish standard-pbr "
           "IBL readiness");
  }
}

void testRepeatedEnvironmentFeatureRegistrationReusesActiveIblResources() {
  SceneResourceTable table;
  const ResourceUri envUri("memory://env/live-neutral-specular.ktx2");
  const TextureHandle envTexture =
      table.registerTexture(envUri, makeSolidHdrCubePayload(StringID{}));
  EXPECT(envTexture.isValid(), "HDR cubemap texture should register");

  const ResourceUri featureUri(
      "memory://features/environment_lighting.render-feature");
  const RenderFeatureHandle firstHandle = table.registerRenderFeature(
      featureUri, makeEnvironmentLightingFeature(envUri));
  EXPECT(firstHandle.isValid(), "environment lighting feature should register");
  const std::optional<ActiveIblEnvironmentResources> firstActive =
      table.activeIblEnvironment();
  EXPECT(firstActive.has_value(),
         "textureCube environment should activate IBL");
  const u64 firstFeatureGeneration = table.featureGeneration();

  const RenderFeatureHandle secondHandle = table.registerRenderFeature(
      featureUri, makeEnvironmentLightingFeature(envUri));
  EXPECT(secondHandle == firstHandle,
         "re-registering the same render feature URI should reuse the handle");
  const std::optional<ActiveIblEnvironmentResources> secondActive =
      table.activeIblEnvironment();
  EXPECT(secondActive.has_value(),
         "reused environment feature should keep active IBL");

  if (firstActive.has_value() && secondActive.has_value()) {
    EXPECT(secondActive->generation == firstActive->generation,
           "reused environment feature should not reactivate IBL generation");
    EXPECT(secondActive->specularPrefilteredCubemap ==
               firstActive->specularPrefilteredCubemap,
           "reused environment feature should not recreate specular IBL "
           "payload");
    EXPECT(secondActive->diffuseSh == firstActive->diffuseSh,
           "reused environment feature should not recompute diffuse SH "
           "payload");
  }
  EXPECT(table.featureGeneration() == firstFeatureGeneration,
         "reused environment feature should not advance feature generation");
}

void testEnvironmentRuntimeStateTracksSceneEnvironmentNode() {
  SceneResourceTable table;
  EXPECT(!table.hasEnvironmentNode(), "new table has no environment node");
  EXPECT(!table.environmentRuntimeState().has_value(),
         "new table has no environment runtime state");

  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting.render-feature"),
      makeEnvironmentLightingFeature(ResourceUri("builtin:env/white_cube")));
  EXPECT(featureHandle.isValid(), "environment feature should register");

  table.setEnvironmentRuntimeState(SceneEnvironmentRuntimeState{
      .feature = featureHandle, .nodePresent = true, .bakeRequested = true});

  EXPECT(table.hasEnvironmentNode(),
         "environment node state should be visible");
  const auto state = table.environmentRuntimeState();
  EXPECT(state.has_value(), "environment runtime state should be stored");
  if (state.has_value()) {
    EXPECT(state->feature == featureHandle,
           "environment runtime state should retain feature handle");
    EXPECT(state->bakeRequested,
           "environment runtime state should retain bake request");
  }
}

void testIblActivationUpdatesUploadViewGenerationOnlyWhenPayloadsReady() {
  SceneResourceTable table;

  const IblEnvironmentActivationResult first =
      table.activateIblEnvironment(iblActivationPayloadFixture(7));
  EXPECT(first.ok, "complete IBL activation payload should succeed");

  const SceneResourceTableUploadView firstView = table.buildUploadView();
  EXPECT(firstView.activeIblGeneration == 7,
         "upload view should expose active IBL generation");
  EXPECT(firstView.activeIbl.diffuseSh.isValid(),
         "active upload view should expose diffuse SH handle");
  EXPECT(firstView.activeIbl.specularPrefilteredCubemap.isValid(),
         "active upload view should expose specular cubemap handle");
  EXPECT(firstView.activeIbl.standardPbrBrdfLut.isValid(),
         "active upload view should expose standard-pbr BRDF LUT handle");
  EXPECT(firstView.environmentDiffuseShPayloads.size() == 1,
         "diffuse SH payload should be a live typed upload record");
  EXPECT(firstView.environmentSpecularPrefilteredCubemaps.size() == 1,
         "specular prefiltered cubemap should be a live typed upload record");
  EXPECT(firstView.standardPbrBrdfLuts.size() == 1,
         "standard-pbr BRDF LUT should be a live typed upload record");

  IblEnvironmentActivationPayload missingBrdf = iblActivationPayloadFixture(8);
  missingBrdf.standardPbrBrdfLut.reset();
  const IblEnvironmentActivationResult failed =
      table.activateIblEnvironment(std::move(missingBrdf));
  EXPECT(!failed.ok,
         "activation missing standard-pbr BRDF LUT payload should fail");

  const SceneResourceTableUploadView afterFailed = table.buildUploadView();
  EXPECT(afterFailed.activeIblGeneration == 7,
         "failed activation must preserve old active IBL generation");
  EXPECT(afterFailed.activeIbl.diffuseSh == firstView.activeIbl.diffuseSh,
         "failed activation must preserve old diffuse SH handle");
  EXPECT(afterFailed.activeIbl.specularPrefilteredCubemap ==
             firstView.activeIbl.specularPrefilteredCubemap,
         "failed activation must preserve old specular cubemap handle");
  EXPECT(afterFailed.activeIbl.standardPbrBrdfLut ==
             firstView.activeIbl.standardPbrBrdfLut,
         "failed activation must preserve old BRDF LUT handle");
}

void testIblActivationRejectsMetadataOnlyPayloads() {
  SceneResourceTable table;
  ResourceMetadata metadataOnlySpecular;
  metadataOnlySpecular.type = SceneResourceType::Texture;
  metadataOnlySpecular.uri =
      ResourceUri("memory://bakes/specular_prefilter.ktx2");
  metadataOnlySpecular.state = ResourceState::Ready;
  const ResourceIdentityHandle metadataHandle =
      table.internResourceMetadata(std::move(metadataOnlySpecular));

  IblEnvironmentActivationPayload payload;
  payload.generation = 1;
  payload.diffuseSh = sh9PayloadFixture();
  payload.standardPbrBrdfLut = brdfLutPayloadFixture();
  const IblEnvironmentActivationResult result =
      table.activateIblEnvironment(std::move(payload));

  EXPECT(metadataHandle.isValid(), "metadata-only texture should be interned");
  EXPECT(!result.ok,
         "metadata-only texture records must not satisfy IBL activation");
  EXPECT(table.buildUploadView().activeIblGeneration == 0,
         "failed metadata-only activation must not publish a generation");
}

void testIblDescriptorResourcesUseActiveBakePayloadsWhenActiveIblExists() {
  SceneResourceTable table;
  table.setIblEnvironmentResources(iblDescriptorResourceFixture());
  IblEnvironmentActivationPayload payload = iblActivationPayloadFixture(2);
  const ResourceCacheIdentity expectedSpecularIdentity =
      payload.specularPrefilteredCubemap->getBackendCacheIdentity();
  const ResourceCacheIdentity expectedBrdfIdentity =
      payload.standardPbrBrdfLut->getBackendCacheIdentity();
  const IblEnvironmentActivationResult activated =
      table.activateIblEnvironment(std::move(payload));
  EXPECT(activated.ok, "active IBL fixture should activate");

  const std::vector<GpuResourceRef> resources =
      table.getIblEnvironmentResources();
  const auto findBinding = [&](StringID bindingName) -> const GpuResourceRef * {
    const auto it = std::find_if(
        resources.begin(), resources.end(),
        [&](const GpuResourceRef &resource) {
          return resource.isValid() && resource.getBindingName() == bindingName;
        });
    return it == resources.end() ? nullptr : &*it;
  };

  const GpuResourceRef *irradiance = findBinding(StringID("IrradianceMap"));
  EXPECT(irradiance != nullptr,
         "active IBL descriptor resources should include an IrradianceMap "
         "derived from diffuse SH");
  if (irradiance != nullptr) {
    const auto *sampler =
        dynamic_cast<const CombinedTextureSampler *>(&irradiance->get());
    EXPECT(sampler != nullptr,
           "active IBL IrradianceMap should be a live texture sampler");
    if (sampler != nullptr && sampler->texture()) {
      EXPECT(sampler->texture()->desc().dimension ==
                 TextureDimension::TextureCube,
             "active IBL IrradianceMap should be a cubemap");
      EXPECT(sampler->texture()->desc().format == TextureFormat::RGBA16Float,
             "active IBL IrradianceMap should preserve HDR half-float format");
    }
  }

  const GpuResourceRef *specular = findBinding(StringID("PrefilteredEnvMap"));
  EXPECT(specular != nullptr,
         "active IBL descriptor resources should include PrefilteredEnvMap");
  if (specular != nullptr) {
    EXPECT(specular->getBackendCacheIdentity() == expectedSpecularIdentity,
           "active IBL PrefilteredEnvMap should come from the activated bake "
           "payload, not the previous descriptor package");
  }

  const GpuResourceRef *brdf = findBinding(StringID("BrdfLut"));
  EXPECT(brdf != nullptr,
         "active IBL descriptor resources should include BrdfLut");
  if (brdf != nullptr) {
    EXPECT(brdf->getBackendCacheIdentity() == expectedBrdfIdentity,
           "active IBL BrdfLut should come from the activated bake payload, "
           "not the previous descriptor package");
  }

  EXPECT(findBinding(StringID("EnvironmentUBO")) == nullptr,
         "active IBL descriptor resources must not keep the legacy "
         "EnvironmentUBO package path as the IBL truth");
}

void testIblDescriptorResourcesIncludeDefaultSamplersWhenEnvironmentIsMissing() {
  SceneResourceTable table;
  IblEnvironmentResources resources;
  resources.environmentUbo = std::make_unique<EnvironmentData>();
  table.setIblEnvironmentResources(std::move(resources));

  const std::vector<GpuResourceRef> descriptors =
      table.getIblEnvironmentResources();
  const auto hasSamplerBinding = [&](StringID bindingName) {
    return std::any_of(descriptors.begin(), descriptors.end(),
                       [&](const GpuResourceRef &resource) {
                         return resource.isValid() &&
                                resource.getType() ==
                                    ResourceType::CombinedImageSampler &&
                                resource.getBindingName() == bindingName;
                       });
  };

  EXPECT(hasSamplerBinding(StringID("IrradianceMap")),
         "default IBL descriptor package should include IrradianceMap");
  EXPECT(hasSamplerBinding(StringID("PrefilteredEnvMap")),
         "default IBL descriptor package should include PrefilteredEnvMap");
  EXPECT(hasSamplerBinding(StringID("BrdfLut")),
         "default IBL descriptor package should include BrdfLut");
}

void testDefaultSceneResourceTableExportsIblSamplerFallbacks() {
  SceneResourceTable table;
  const std::vector<GpuResourceRef> descriptors =
      table.getIblEnvironmentResources();
  const auto hasSamplerBinding = [&](StringID bindingName) {
    return std::any_of(descriptors.begin(), descriptors.end(),
                       [&](const GpuResourceRef &resource) {
                         return resource.isValid() &&
                                resource.getType() ==
                                    ResourceType::CombinedImageSampler &&
                                resource.getBindingName() == bindingName;
                       });
  };

  EXPECT(hasSamplerBinding(StringID("IrradianceMap")),
         "default SceneResourceTable should export an IrradianceMap fallback");
  EXPECT(hasSamplerBinding(StringID("PrefilteredEnvMap")),
         "default SceneResourceTable should export a PrefilteredEnvMap "
         "fallback");
  EXPECT(hasSamplerBinding(StringID("BrdfLut")),
         "default SceneResourceTable should export a BrdfLut fallback");
}

void testSurfaceLightingReadinessFollowsActiveIblActivation() {
  SceneResourceTable table;
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/surface_lighting.render-feature"),
      makeSurfaceLightingFeature());
  EXPECT(featureHandle.isValid(), "surface lighting feature should register");

  const SurfaceLightingData::Param *before =
      findSurfaceLightingParam(table, featureHandle);
  EXPECT(before != nullptr, "surface lighting UBO should be exported");
  if (before != nullptr) {
    EXPECT(before->enableIblLighting == 1u,
           "surface lighting should keep the authored IBL enable flag");
    EXPECT(before->environmentIblReady == 0u,
           "surface lighting should start with environment IBL unavailable");
    EXPECT(before->standardPbrIblReady == 0u,
           "surface lighting should start with standard-PBR IBL unavailable");
  }

  const IblEnvironmentActivationResult activated =
      table.activateIblEnvironment(iblActivationPayloadFixture(5));
  EXPECT(activated.ok, "complete active IBL payload should activate");

  const SurfaceLightingData::Param *after =
      findSurfaceLightingParam(table, featureHandle);
  EXPECT(after != nullptr, "surface lighting UBO should remain exported");
  if (after != nullptr) {
    EXPECT(after->enableIblLighting == 1u,
           "activation should preserve the authored IBL enable flag");
    EXPECT(after->environmentIblReady == 1u,
           "activation should publish environment IBL readiness");
    EXPECT(after->standardPbrIblReady == 1u,
           "activation should publish standard-PBR IBL readiness");
  }
}

void testSurfaceLightingReadinessUsesAlreadyActiveIblOnRegistration() {
  SceneResourceTable table;
  const IblEnvironmentActivationResult activated =
      table.activateIblEnvironment(iblActivationPayloadFixture(6));
  EXPECT(activated.ok, "complete active IBL payload should activate");

  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/surface_lighting.render-feature"),
      makeSurfaceLightingFeature());
  EXPECT(featureHandle.isValid(), "surface lighting feature should register");

  const SurfaceLightingData::Param *param =
      findSurfaceLightingParam(table, featureHandle);
  EXPECT(param != nullptr, "surface lighting UBO should be exported");
  if (param != nullptr) {
    EXPECT(param->environmentIblReady == 1u,
           "surface lighting registration should observe active environment "
           "IBL");
    EXPECT(param->standardPbrIblReady == 1u,
           "surface lighting registration should observe active standard-PBR "
           "IBL");
  }
}

void testSceneLevelResourcesLeaveIblFallbacksToPassFeatureReads() {
  Scene scene("SceneLevelIblFallbacks");
  const auto hasSamplerBinding = [](const DescriptorResourceList &resources,
                                    StringID bindingName) {
    return std::any_of(resources.begin(), resources.end(),
                       [&](const DescriptorResourceRef &resource) {
                         return resource.isResource() &&
                                resource.resource().isValid() &&
                                resource.resource().getType() ==
                                    ResourceType::CombinedImageSampler &&
                                resource.getBindingName() == bindingName;
                       });
  };

  const DescriptorResourceList deferredResources =
      scene.getSceneLevelResources(Pass_DeferredLighting, RenderTarget{});
  EXPECT(!hasSamplerBinding(deferredResources, StringID("IrradianceMap")),
         "scene-level resources should leave IrradianceMap fallback to pass "
         "feature reads");
  EXPECT(!hasSamplerBinding(deferredResources, StringID("PrefilteredEnvMap")),
         "scene-level resources should leave PrefilteredEnvMap fallback to "
         "pass feature reads");
  EXPECT(!hasSamplerBinding(deferredResources, StringID("BrdfLut")),
         "scene-level resources should leave BrdfLut fallback to pass feature "
         "reads");

  const DescriptorResourceList forwardResources =
      scene.getSceneLevelResources(Pass_Forward, RenderTarget{});
  EXPECT(!hasSamplerBinding(forwardResources, StringID("IrradianceMap")),
         "Forward renderables should receive IBL descriptors through shader "
         "reflection, not unconditional scene-level resources");
}

void testForwardSceneLevelResourcesIncludeZeroLightUboWithoutLights() {
  Scene scene("ForwardNoDirectLight");
  const DescriptorResourceList resources =
      scene.getSceneLevelResources(Pass_Forward, RenderTarget{});

  const auto lightIt =
      std::find_if(resources.begin(), resources.end(),
                   [](const DescriptorResourceRef &resource) {
                     return resource.isResource() &&
                            resource.resource().isValid() &&
                            resource.getBindingName() == StringID("LightUBO") &&
                            resource.resource().get().getType() ==
                                ResourceType::UniformBuffer &&
                            resource.resource().get().getByteSize() ==
                                sizeof(DirectionalLightData::Param);
                   });
  EXPECT(lightIt != resources.end(),
         "Forward pass should bind a LightUBO even when the scene has no "
         "direct lights");
  if (lightIt == resources.end()) {
    return;
  }

  const auto *param = static_cast<const DirectionalLightData::Param *>(
      lightIt->resource().get().getRawData());
  EXPECT(param != nullptr, "zero LightUBO should expose directional data");
  if (param != nullptr) {
    EXPECT(param->color.w == 0.0f,
           "zero LightUBO must not contribute direct light intensity");
  }
}

void testLiveCameraSceneLevelResourcesReuseStableCameraUbo() {
  Scene scene("StableLiveCameraUbo");

  CameraResource firstCamera;
  firstCamera.active = true;
  firstCamera.pose.eye = Vec3f{0.0f, 1.0f, 5.0f};
  firstCamera.pose.forward = Vec3f{0.0f, 0.0f, -1.0f};
  firstCamera.pose.up = Vec3f{0.0f, 1.0f, 0.0f};
  firstCamera.view = makeCameraViewMatrix(firstCamera.pose);
  firstCamera.proj = makeCameraProjectionMatrix(firstCamera.projection);

  const DescriptorResourceList firstResources =
      scene.getSceneLevelResources(Pass_Forward, firstCamera);
  const IGpuResource *firstCameraUbo =
      findBindingResource(firstResources, StringID("CameraUBO"));
  EXPECT(firstCameraUbo != nullptr,
         "live camera scene resources should include CameraUBO");
  const ResourceCacheIdentity firstIdentity =
      firstCameraUbo != nullptr ? firstCameraUbo->getBackendCacheIdentity() : 0;

  CameraResource secondCamera = firstCamera;
  secondCamera.pose.eye = Vec3f{1.0f, 2.0f, 6.0f};
  secondCamera.view = makeCameraViewMatrix(secondCamera.pose);
  secondCamera.proj = makeCameraProjectionMatrix(secondCamera.projection);

  const DescriptorResourceList secondResources =
      scene.getSceneLevelResources(Pass_Forward, secondCamera);
  const IGpuResource *secondCameraUbo =
      findBindingResource(secondResources, StringID("CameraUBO"));
  EXPECT(secondCameraUbo != nullptr,
         "updated live camera scene resources should include CameraUBO");
  const ResourceCacheIdentity secondIdentity =
      secondCameraUbo != nullptr ? secondCameraUbo->getBackendCacheIdentity()
                                 : 0;

  EXPECT(
      firstIdentity != 0 && secondIdentity == firstIdentity,
      "live camera updates should reuse the same CameraUBO resource identity");
  EXPECT(secondCameraUbo != nullptr && secondCameraUbo->isDirty(),
         "live camera updates should dirty the stable CameraUBO for upload");
}

void testRealtimeSceneObjectTransformUpdatesDirtyStablePayloadResource() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeTriangleMesh());
  const MaterialHandle material = table.registerMaterial(
      MaterialInstance::createUnique(MaterialTemplate::create("matte")));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  const ObjectHandle objectHandle = table.registerObject(object);
  EXPECT(objectHandle.isValid(), "object fixture should register");

  const DescriptorResourceList firstResources =
      table.getRealtimeSceneDescriptorResources();
  const IGpuResource *firstObjects =
      findBindingResource(firstResources, StringID("SceneObjects"));
  EXPECT(firstObjects != nullptr,
         "realtime scene descriptors should include SceneObjects");
  if (firstObjects == nullptr) {
    return;
  }
  const ResourceCacheIdentity firstIdentity =
      firstObjects->getBackendCacheIdentity();
  firstObjects->clearDirty();

  const u64 beforeSelection = table.descriptorResourceSelectionGeneration();
  const u64 beforeDescriptor = table.descriptorUploadGeneration();
  const u64 beforeVolatile = table.volatileUploadGeneration();
  const u64 beforeUpload = table.uploadGeneration();

  object.objectToWorld = Mat4f::translate(Vec3f{2.0f, 0.0f, -1.0f});
  object.worldToObject = Mat4f::translate(Vec3f{-2.0f, 0.0f, 1.0f});
  object.worldBounds = BoundingBox{{2.0f, 0.0f, -1.0f}, {3.0f, 1.0f, 0.0f}};
  table.updateObject(objectHandle, object);

  EXPECT(table.descriptorResourceSelectionGeneration() == beforeSelection,
         "transform-only updateObject should not rebuild render inputs");
  EXPECT(table.descriptorUploadGeneration() == beforeDescriptor,
         "transform-only updateObject should not rebuild descriptor upload "
         "plans");
  EXPECT(table.volatileUploadGeneration() == beforeVolatile + 1,
         "transform-only updateObject should use the dirty resource upload "
         "path");
  EXPECT(table.uploadGeneration() == beforeUpload + 1,
         "transform-only updateObject should still advance upload generation");

  table.refreshDirtyRealtimeScenePayloadResources();
  const DescriptorResourceList secondResources =
      table.getRealtimeSceneDescriptorResources();
  const IGpuResource *secondObjects =
      findBindingResource(secondResources, StringID("SceneObjects"));
  EXPECT(secondObjects != nullptr,
         "refreshed realtime descriptors should keep SceneObjects");
  if (secondObjects == nullptr) {
    return;
  }
  EXPECT(secondObjects->getBackendCacheIdentity() == firstIdentity,
         "transform-only updates should keep the SceneObjects resource "
         "identity stable");
  EXPECT(secondObjects->isDirty(),
         "transform-only updates should dirty the stable SceneObjects payload");
  EXPECT(secondObjects->getByteSize() == sizeof(SceneGpuObjectRecord),
         "single object fixture should upload one SceneObjects record");

  const auto *records =
      static_cast<const SceneGpuObjectRecord *>(secondObjects->getRawData());
  EXPECT(records != nullptr, "SceneObjects resource should expose CPU bytes");
  if (records != nullptr) {
    EXPECT(records[0].objectToWorld[3].x == 2.0f &&
               records[0].objectToWorld[3].y == 0.0f &&
               records[0].objectToWorld[3].z == -1.0f,
           "dirty SceneObjects payload should contain the updated transform");
  }
}

void testIblActivationReplacesOldLiveHandlesOnSuccess() {
  SceneResourceTable table;
  const IblEnvironmentActivationResult first =
      table.activateIblEnvironment(iblActivationPayloadFixture(3));
  EXPECT(first.ok, "first activation should succeed");
  const SceneResourceTableUploadView firstView = table.buildUploadView();
  const SceneActiveIblUploadState firstActive = firstView.activeIbl;

  const IblEnvironmentActivationResult second =
      table.activateIblEnvironment(iblActivationPayloadFixture(4));
  EXPECT(second.ok, "second activation should succeed");
  const SceneResourceTableUploadView secondView = table.buildUploadView();

  EXPECT(secondView.activeIblGeneration == 4,
         "second activation should publish the new generation");
  EXPECT(!table.isAlive(firstActive.diffuseSh),
         "old diffuse SH handle should no longer be live");
  EXPECT(!table.isAlive(firstActive.specularPrefilteredCubemap),
         "old specular cubemap handle should no longer be live");
  EXPECT(!table.isAlive(firstActive.standardPbrBrdfLut),
         "old BRDF LUT handle should no longer be live");
  EXPECT(secondView.environmentDiffuseShPayloads.size() == 1,
         "upload view should expose one active diffuse SH payload");
  EXPECT(secondView.environmentSpecularPrefilteredCubemaps.size() == 1,
         "upload view should expose one active specular cubemap payload");
  EXPECT(secondView.standardPbrBrdfLuts.size() == 1,
         "upload view should expose one active BRDF LUT payload");
}

class TestShader final : public IShader {
public:
  TestShader() {
    m_stages.push_back(
        ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203u}});
    m_bindings.push_back(ShaderResourceBinding{
        .name = "CameraUBO",
        .set = 0,
        .binding = 0,
        .type = ShaderPropertyType::UniformBuffer,
        .size = 64,
        .stageFlags = ShaderStage::Vertex,
    });
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 1; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

IShaderSharedPtr shaderPayloadFixture() {
  return std::make_shared<TestShader>();
}

class SourceMaterialRecordsShader final : public IShader {
public:
  SourceMaterialRecordsShader() {
    m_stages.push_back(
        ShaderStageCode{ShaderStage::Fragment, std::vector<u32>{0x07230203u}});
    m_bindings.push_back(ShaderResourceBinding{
        .name = "SceneSourceMaterialRecords",
        .set = 0,
        .binding = 13,
        .type = ShaderPropertyType::StorageBuffer,
        .size = 16,
        .stageFlags = ShaderStage::Fragment,
    });
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 2; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

void testPackageReadyGraphExport() {
  SceneResourceTable table;
  ResourceMetadata material;
  material.type = SceneResourceType::Material;
  material.uri = ResourceUri("assets/materials/paint.material");
  material.contentHash = "material-hash";
  material.dependencies.push_back(ResourceUri("assets/textures/paint.png"));

  ResourceMetadata texture;
  texture.type = SceneResourceType::Texture;
  texture.uri = ResourceUri("assets/textures/paint.png");
  texture.contentHash = "texture-hash";

  const auto materialHandle = table.internResourceMetadata(material);
  const auto textureHandle = table.internResourceMetadata(texture);

  const auto graph = table.exportResourceGraph();
  EXPECT(graph.resources.size() >= 2,
         "graph should export material and texture resources");
  EXPECT(graph.handleToIndex(materialHandle) != u32_max,
         "graph should map material handle to index");
  EXPECT(graph.handleToIndex(textureHandle) != u32_max,
         "graph should map texture handle to index");
  EXPECT(!graph.resources.empty() &&
             graph.resources[graph.handleToIndex(materialHandle)]
                     .dependencies.size() == 1,
         "graph should preserve material-to-texture dependency edge");
}

void testOverrideIdentityUsesStableHash() {
  SceneResourceTable table;
  const ResourceIdentityHandle base = table.internMaterialInstanceIdentity(
      ResourceUri("assets/materials/base.material"), "");
  const ResourceIdentityHandle firstOverride =
      table.internMaterialInstanceIdentity(
          ResourceUri("assets/materials/base.material"), "override-a");
  const ResourceIdentityHandle sameOverride =
      table.internMaterialInstanceIdentity(
          ResourceUri("assets/materials/base.material"), "override-a");
  const ResourceIdentityHandle otherOverride =
      table.internMaterialInstanceIdentity(
          ResourceUri("assets/materials/base.material"), "override-b");

  EXPECT(base.isValid(), "base material identity should be valid");
  EXPECT(!(base == firstOverride),
         "override material identity should differ from base");
  EXPECT(firstOverride == sameOverride,
         "same override hash should reuse material identity");
  EXPECT(!(firstOverride == otherOverride),
         "different override hash should split material identity");
}

void testRenderPathGraphResourceGraphExportsFeatureAndShaderDependencies() {
  SceneResourceTable table;
  const ResourceIdentityHandle renderer = table.loadOrGetResource(
      SceneResourceType::Renderer, ResourceUri("memory://renderer/default"));
  const ResourceIdentityHandle camera = table.loadOrGetResource(
      SceneResourceType::Camera, ResourceUri("memory://camera/main"));

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));
  table.addDependency(renderer, graphHandle);
  table.addDependency(camera, graphHandle);

  const auto exported = table.exportResourceGraph();
  const u32 rendererIndex = exported.handleToIndex(renderer);
  const u32 cameraIndex = exported.handleToIndex(camera);
  const u32 graphIndex =
      exported.handleToIndex(table.metadataHandle(graphHandle));
  EXPECT(rendererIndex != u32_max, "renderer resource should export");
  EXPECT(cameraIndex != u32_max, "camera resource should export");
  EXPECT(graphIndex != u32_max, "render path graph resource should export");
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  const auto dependsOn = [&](u32 ownerIndex, u32 dependencyIndex) {
    if (ownerIndex == u32_max || dependencyIndex == u32_max) {
      return false;
    }
    const auto &dependencies = exported.resources[ownerIndex].dependencyHandles;
    return std::find(dependencies.begin(), dependencies.end(),
                     exported.handles[dependencyIndex]) != dependencies.end();
  };

  EXPECT(dependsOn(rendererIndex, graphIndex),
         "renderer should depend on RenderPathGraph");
  EXPECT(dependsOn(cameraIndex, graphIndex),
         "camera should depend on RenderPathGraph");
  EXPECT(exported.resources[graphIndex].dependencyHandles.size() == 2,
         "RenderPathGraph should depend on feature and shader metadata");
}

void testUploadViewExportsRenderPathGraphPassFeatureAndShaderIndices() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));

  const SceneResourceTableUploadView view = table.buildUploadView();
  const auto graphIt =
      std::find_if(view.renderPathGraphIndexByHandle.begin(),
                   view.renderPathGraphIndexByHandle.end(),
                   [&](const SceneResourceRenderPathGraphUploadIndex &entry) {
                     return entry.handle == graphHandle;
                   });
  EXPECT(graphIt != view.renderPathGraphIndexByHandle.end(),
         "upload view should map RenderPathGraphHandle to typed graph index");
  EXPECT(graphIt != view.renderPathGraphIndexByHandle.end() &&
             graphIt->typedIndex < view.renderPathGraphs.size(),
         "RenderPathGraphHandle should map inside graph typed span");
  if (graphIt == view.renderPathGraphIndexByHandle.end() ||
      graphIt->typedIndex >= view.renderPathGraphs.size()) {
    return;
  }

  const SceneGpuRenderPathGraphRecord &graphRecord =
      view.renderPathGraphs[graphIt->typedIndex];
  EXPECT(graphRecord.passOffset < view.renderPathGraphPasses.size(),
         "graph record should point at pass record range");
  EXPECT(graphRecord.passCount == 1,
         "graph record should export one pass record");
  EXPECT(graphRecord.featureOffset < view.renderPathGraphFeatures.size(),
         "graph record should point at feature record range");
  EXPECT(graphRecord.featureCount == 1,
         "graph record should export one feature record");

  const SceneGpuRenderPathGraphPassRecord &passRecord =
      view.renderPathGraphPasses[graphRecord.passOffset];
  EXPECT(passRecord.shaderIndex < view.renderPathGraphShaders.size(),
         "pass record should point at shader metadata index");
}

void testUploadViewGroupsSourceLocalMaterialsWithSameSignature() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection contract = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const StringID sourceSignature = contract.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(contract));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(contract));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 2,
         "draws should reference source-local material refs");
  EXPECT(view.sourceMaterialStorages.size() == 1,
         "same source signature should produce one source-local storage");
  const SceneSourceLocalMaterialStorageView *storage =
      findSourceStorage(view, sourceSignature);
  EXPECT(storage != nullptr,
         "source-local storage should be keyed by source signature");
  EXPECT(storage != nullptr && storage->recordOffset == 0,
         "same source storage should start at first source-local record");
  EXPECT(storage != nullptr && storage->recordCount == 2,
         "same source storage should cover both material records");
  EXPECT(view.sourceMaterialRecords.size() == 2,
         "source-local record span should contain both material records");
  EXPECT(view.draws.size() == 2 && view.draws[0].materialIndex == u32_max &&
             view.draws[1].materialIndex == u32_max,
         "source-contract draws should not point at legacy material records");
  EXPECT(view.draws.size() == 2 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceStorageIndex == 0 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceStorageIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceLocalMaterialIndex == 1,
         "draw material refs should resolve to source storage and local index");
  EXPECT(storage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *storage),
         "same source storage range should contain contiguous source-local "
         "indices");
}

void testUploadViewSplitsSourceLocalMaterialsBySignature() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection matte = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const MaterialContractReflection metal = makeMaterialContract(
      "memory://materials/metal.contract.glsl", "metal", "metal-reflect-v1");
  const StringID matteSignature = matte.sourceSignature();
  const StringID metalSignature = metal.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(metal));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 2,
         "source-contract materials should create material refs");
  EXPECT(view.sourceMaterialStorages.size() == 2,
         "different source signatures should produce separate storages");

  const SceneSourceLocalMaterialStorageView *matteStorage =
      findSourceStorage(view, matteSignature);
  const SceneSourceLocalMaterialStorageView *metalStorage =
      findSourceStorage(view, metalSignature);
  EXPECT(matteStorage != nullptr,
         "matte source-local storage should be present");
  EXPECT(metalStorage != nullptr,
         "metal source-local storage should be present");
  EXPECT(matteStorage != nullptr && matteStorage->recordOffset == 0 &&
             matteStorage->recordCount == 1,
         "first source storage should cover the first material record");
  EXPECT(metalStorage != nullptr && metalStorage->recordOffset == 1 &&
             metalStorage->recordCount == 1,
         "second source storage should cover the second material record");
  EXPECT(view.sourceMaterialRecords.size() == 2,
         "source-local record span should contain both source records");
  EXPECT(view.draws.size() == 2 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceStorageIndex !=
                 view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceStorageIndex,
         "draw material refs should split different source storages");
  EXPECT(matteStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *matteStorage),
         "matte storage range should contain contiguous source-local indices");
  EXPECT(metalStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *metalStorage),
         "metal storage range should contain contiguous source-local indices");
}

void testUploadViewSourceLocalMaterialRangesAreNotLegacyInterleaved() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle thirdMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection matte = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const MaterialContractReflection metal = makeMaterialContract(
      "memory://materials/metal.contract.glsl", "metal", "metal-reflect-v1");
  const StringID matteSignature = matte.sourceSignature();
  const StringID metalSignature = metal.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(metal));
  const MaterialHandle thirdMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);
  registerObject(table, thirdMesh, thirdMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 3,
         "source-contract draws should preserve one material ref per draw");
  EXPECT(view.draws.size() == 3, "legacy draw span should preserve all draws");
  EXPECT(view.draws.size() == 3 && view.draws[0].materialIndex == u32_max &&
             view.draws[1].materialIndex == u32_max &&
             view.draws[2].materialIndex == u32_max,
         "source-contract draw material indices should not use legacy path");
  EXPECT(view.sourceMaterialStorages.size() == 2,
         "source-local storages should still group by source signature");
  EXPECT(view.sourceMaterialRecords.size() == 3,
         "source-local record span should contain one record per uploaded "
         "material");
  EXPECT(view.draws.size() == 3 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[2].materialRefIndex]
                     .sourceLocalMaterialIndex == 1,
         "material refs should point at local records inside their source "
         "storage");

  const SceneSourceLocalMaterialStorageView *matteStorage =
      findSourceStorage(view, matteSignature);
  const SceneSourceLocalMaterialStorageView *metalStorage =
      findSourceStorage(view, metalSignature);
  EXPECT(matteStorage != nullptr,
         "matte source-local storage should be present");
  EXPECT(metalStorage != nullptr,
         "metal source-local storage should be present");
  EXPECT(matteStorage != nullptr && matteStorage->recordOffset == 0 &&
             matteStorage->recordCount == 2,
         "matte source-local records should be contiguous despite A,B,A "
         "legacy order");
  EXPECT(metalStorage != nullptr && metalStorage->recordOffset == 2 &&
             metalStorage->recordCount == 1,
         "metal source-local record range should follow first-seen source "
         "ordering");
  EXPECT(matteStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *matteStorage),
         "matte source-local range should contain contiguous local indices");
  EXPECT(metalStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *metalStorage),
         "metal source-local range should contain contiguous local indices");
}

void testSourceMaterialDescriptorBindsPerSourceStorage() {
  Scene scene("source-material-descriptor-test");
  SceneResourceTable &table = scene.resources();
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());

  MaterialContractReflection large = makeMaterialContract(
      "memory://materials/large.contract.glsl", "large", "large-reflect-v1");
  large.storageFields.push_back(makeStorageField("baseColor", "baseColor"));
  large.storageFields.push_back(
      makeTextureSlotField("baseColorTexture", "baseColorTexture"));
  MaterialContractReflection small = makeMaterialContract(
      "memory://materials/small.contract.glsl", "small", "small-reflect-v1");
  small.storageFields.push_back(
      makeTextureSlotField("baseColorTexture", "baseColorTexture"));

  const MaterialHandle largeMaterial =
      table.registerMaterial(makeSourceMaterial(large));
  const MaterialHandle smallMaterial =
      table.registerMaterial(makeSourceMaterial(small));
  registerObject(table, firstMesh, largeMaterial);
  registerObject(table, secondMesh, smallMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  const SceneSourceLocalMaterialStorageView *largeStorage =
      findSourceStorage(view, large.sourceSignature());
  const SceneSourceLocalMaterialStorageView *smallStorage =
      findSourceStorage(view, small.sourceSignature());
  EXPECT(largeStorage != nullptr && smallStorage != nullptr,
         "mixed material sources should create two storages");
  EXPECT(
      largeStorage != nullptr && smallStorage != nullptr &&
          view.sourceMaterialRecords[largeStorage->recordOffset].bytes.size() !=
              view.sourceMaterialRecords[smallStorage->recordOffset]
                  .bytes.size(),
      "test fixture requires different source record strides");

  const IShaderSharedPtr shader =
      std::make_shared<SourceMaterialRecordsShader>();
  const DescriptorResourceList largeResources =
      buildSceneMaterialDescriptorResources(table, largeMaterial, shader);
  const DescriptorResourceList smallResources =
      buildSceneMaterialDescriptorResources(table, smallMaterial, shader);
  const IGpuResource *largeSourceRecords = findBindingResource(
      largeResources, StringID("SceneSourceMaterialRecords"));
  const IGpuResource *smallSourceRecords = findBindingResource(
      smallResources, StringID("SceneSourceMaterialRecords"));

  EXPECT(largeSourceRecords != nullptr,
         "large material should bind source material records");
  EXPECT(smallSourceRecords != nullptr,
         "small material should bind source material records");
  EXPECT(largeSourceRecords != nullptr && largeStorage != nullptr &&
             largeSourceRecords->getByteSize() ==
                 view.sourceMaterialRecords[largeStorage->recordOffset]
                     .bytes.size(),
         "large material descriptor should bind only the large source storage");
  EXPECT(smallSourceRecords != nullptr && smallStorage != nullptr &&
             smallSourceRecords->getByteSize() ==
                 view.sourceMaterialRecords[smallStorage->recordOffset]
                     .bytes.size(),
         "small material descriptor should bind only the small source storage");
}

void testUploadViewRejectsSourceSignatureStorageLayoutConflict() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());

  MaterialContractReflection first =
      makeMaterialContract("memory://materials/conflict.contract.glsl", "matte",
                           "shared-reflect-v1");
  first.storageFields.push_back(makeStorageField("baseColor", "Kd"));

  MaterialContractReflection second = first;
  second.storageFields.clear();
  second.storageFields.push_back(makeStorageField("albedo", "Kd"));

  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(first));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(second));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  EXPECT(buildUploadThrows(table, "source signature conflict"),
         "same source signature with different storage layout should fail "
         "before upload records are produced");
}

void testUploadViewRejectsMaterialSourceSignatureMismatch() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeTriangleMesh());
  MaterialContractReflection contract =
      makeMaterialContract("memory://materials/signature.contract.glsl",
                           "matte", "signature-reflect-v1");
  contract.storageFields.push_back(makeStorageField("baseColor", "Kd"));

  auto material = makeSourceMaterial(contract);
  material->setMaterialSourceSignature(StringID("corrupt-source-signature"));
  const MaterialHandle materialHandle =
      table.registerMaterial(std::move(material));
  registerObject(table, mesh, materialHandle);

  EXPECT(buildUploadThrows(table, "source signature mismatch"),
         "source-contract material with mismatched instance signature should "
         "fail as an invariant violation");
}

void testUploadViewRejectsUnresolvedExplicitTextureSlot() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeTriangleMesh());
  MaterialContractReflection contract =
      makeMaterialContract("memory://materials/textured.contract.glsl", "matte",
                           "textured-reflect-v1");
  contract.parameters.push_back(MaterialContractParameter{
      "Kd", true, {MaterialContractParameterKind::Texture}});
  contract.storageFields.push_back(
      makeTextureSlotField("baseColorTexture", "Kd"));

  auto material = makeSourceMaterial(contract);
  MaterialParameterEnvelope kdTexture;
  kdTexture.kind = MaterialEnvelopeKind::Texture;
  kdTexture.valueType = MaterialEnvelopeValueType::Rgb;
  kdTexture.uri = "memory://textures/missing-kd.png";
  material->setMaterialEnvelope(StringID("Kd"), std::move(kdTexture));

  const MaterialHandle materialHandle =
      table.registerMaterial(std::move(material));
  registerObject(table, mesh, materialHandle);

  EXPECT(buildUploadThrows(table, "unresolved texture slot"),
         "explicit texture parameters must fail if no table texture slot can "
         "be resolved");
}

void testRenderPathGraphRegistrationRejectsMissingFeatureResource() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/missing.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
        ResourceUri("memory://graphs/forward"), std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    rejected = std::string(error.what()).find("missing RenderFeature") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "RenderPathGraph registration must fail when a feature URI has no "
         "registered RenderFeature payload");
  EXPECT(table.renderFeatureCount() == 0,
         "missing feature dependency must not create a placeholder feature");
}

void testRenderPathGraphRegistrationRejectsSceneInputWithoutGeometry() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "InvalidInputContract";
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    [[maybe_unused]] const RenderPathGraphHandle graphHandle =
        table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                      std::move(graph));
  } catch (const std::invalid_argument &error) {
    rejected =
        std::string(error.what()).find("input.geometry") != std::string::npos;
  }
  EXPECT(rejected,
         "RenderPathGraph registration should reject scene-renderables input "
         "without geometry");
}

void testFailedShaderMetadataDoesNotSatisfyRenderPathGraphDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));

  ResourceMetadata failedShader;
  failedShader.type = SceneResourceType::Shader;
  failedShader.uri = ResourceUri("memory://shaders/missing.shader");
  failedShader.state = ResourceState::Failed;
  failedShader.diagnostics.push_back(ResourceDiagnostic{
      .ownerUri = ResourceUri("memory://graphs/forward"),
      .resourceUri = failedShader.uri,
      .parserName = "test",
      .message = "shader source resolution failed",
  });
  const ResourceIdentityHandle failedShaderIdentity =
      table.internResourceMetadata(std::move(failedShader));

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/missing.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
        ResourceUri("memory://graphs/forward"), std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    rejected =
        std::string(error.what()).find("missing Shader") != std::string::npos;
  }

  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(failedShaderIdentity.isValid(),
         "failed shader metadata should be interned for diagnostics");
  EXPECT(table.shaderCount() == 0,
         "failed shader metadata must not create a typed shader descriptor");
  EXPECT(rejected,
         "failed metadata-only shader must not satisfy a graph dependency");
}

void testSourceResolvedShaderWithoutPayloadDoesNotSatisfyGraphDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/source_only.shader"), shaderSourceFixture(),
      nullptr);

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/source_only.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
        ResourceUri("memory://graphs/forward"), std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    const std::string message = error.what();
    rejected = message.find("memory://graphs/forward") != std::string::npos &&
               message.find("memory://shaders/source_only.shader") !=
                   std::string::npos &&
               message.find("compiled/reflected payload") != std::string::npos;
  }

  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(),
         "source-resolved shader metadata fixture should be registered");
  EXPECT(rejected,
         "source-resolved shader descriptors without a live typed payload must "
         "not satisfy a RenderPathGraph dependency");
}

void testUploadViewRejectsSourceResolvedShaderWithoutPayload() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/source_only.shader"), shaderSourceFixture(),
      nullptr);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    const std::string message = error.what();
    rejected = message.find("memory://shaders/source_only.shader") !=
                   std::string::npos &&
               message.find("compiled/reflected payload") != std::string::npos;
  }

  EXPECT(shaderHandle.isValid(),
         "source-resolved shader metadata fixture should be registered");
  EXPECT(rejected,
         "upload view must reject source-resolved shaders without a live typed "
         "payload/reflection");
}

void testUploadViewRejectsReleasedRenderPathGraphFeatureDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));
  table.release(featureHandle);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("missing RenderFeature") !=
               std::string::npos;
  }
  EXPECT(graphHandle.isValid(),
         "graph fixture should be registered before release");
  EXPECT(rejected,
         "upload view must fail when RenderPathGraph feature handle mapping is "
         "missing instead of exporting u32_max");
}

void testUploadViewRejectsReleasedRenderPathGraphShaderDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));
  table.release(shaderHandle);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("missing Shader") != std::string::npos;
  }
  EXPECT(graphHandle.isValid(),
         "graph fixture should be registered before shader release");
  EXPECT(rejected,
         "upload view must fail when RenderPathGraph shader handle mapping is "
         "missing instead of exporting u32_max");
}

void testExportRejectsReleasedRenderPathGraphDependencies() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");
  EXPECT(graphHandle.isValid(), "graph fixture should be registered");

  table.release(featureHandle);

  bool rejected = false;
  try {
    (void)table.exportResourceGraph();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("released RenderFeature") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "resource graph export must fail when a graph dependency was "
         "released instead of exporting a stale dependency handle");
}

void testExportRejectsFailedRenderPathGraphShaderDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"), shaderSourceFixture(),
      shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle = table.registerRenderPathGraph(
      ResourceUri("memory://graphs/forward"), std::move(graph));
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");
  EXPECT(graphHandle.isValid(), "graph fixture should be registered");

  ResourceMetadata failedShader;
  failedShader.type = SceneResourceType::Shader;
  failedShader.uri = ResourceUri("memory://shaders/surface_lit.shader");
  failedShader.state = ResourceState::Failed;
  failedShader.diagnostics.push_back(ResourceDiagnostic{
      .ownerUri = ResourceUri("memory://graphs/forward"),
      .resourceUri = failedShader.uri,
      .parserName = "test",
      .message = "shader compile failed",
  });
  const ResourceIdentityHandle failedShaderIdentity =
      table.internResourceMetadata(std::move(failedShader));
  EXPECT(failedShaderIdentity.isValid(),
         "failed shader metadata fixture should be interned");

  bool rejected = false;
  try {
    (void)table.exportResourceGraph();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("Shader") != std::string::npos &&
               std::string(error.what()).find("non-uploadable state") !=
                   std::string::npos;
  }
  EXPECT(rejected,
         "resource graph export must fail when a graph shader dependency is "
         "failed instead of exporting stale ready metadata");
}

} // namespace

int main() {
  testPackageReadyGraphExport();
  testOverrideIdentityUsesStableHash();
  testRenderPathGraphResourceGraphExportsFeatureAndShaderDependencies();
  testUploadViewExportsRenderPathGraphPassFeatureAndShaderIndices();
  testBuiltinDefaultTexturesAreStableSceneResources();
  testEnvironmentFeatureBuiltinWhiteCubeRegistersLiveSkyboxMap();
  testEnvironmentFeatureMissingUriDoesNotRegisterSkyboxMap();
  testEnvironmentFeatureHdrTextureCubeActivatesSurfaceLightingIbl();
  testRepeatedEnvironmentFeatureRegistrationReusesActiveIblResources();
  testEnvironmentRuntimeStateTracksSceneEnvironmentNode();
  testIblActivationUpdatesUploadViewGenerationOnlyWhenPayloadsReady();
  testIblActivationRejectsMetadataOnlyPayloads();
  testIblDescriptorResourcesUseActiveBakePayloadsWhenActiveIblExists();
  testIblDescriptorResourcesIncludeDefaultSamplersWhenEnvironmentIsMissing();
  testDefaultSceneResourceTableExportsIblSamplerFallbacks();
  testSurfaceLightingReadinessFollowsActiveIblActivation();
  testSurfaceLightingReadinessUsesAlreadyActiveIblOnRegistration();
  testSceneLevelResourcesLeaveIblFallbacksToPassFeatureReads();
  testForwardSceneLevelResourcesIncludeZeroLightUboWithoutLights();
  testLiveCameraSceneLevelResourcesReuseStableCameraUbo();
  testRealtimeSceneObjectTransformUpdatesDirtyStablePayloadResource();
  testIblActivationReplacesOldLiveHandlesOnSuccess();
  testUploadViewGroupsSourceLocalMaterialsWithSameSignature();
  testUploadViewSplitsSourceLocalMaterialsBySignature();
  testUploadViewSourceLocalMaterialRangesAreNotLegacyInterleaved();
  testSourceMaterialDescriptorBindsPerSourceStorage();
  testUploadViewRejectsSourceSignatureStorageLayoutConflict();
  testUploadViewRejectsMaterialSourceSignatureMismatch();
  testUploadViewRejectsUnresolvedExplicitTextureSlot();
  testRenderPathGraphRegistrationRejectsMissingFeatureResource();
  testRenderPathGraphRegistrationRejectsSceneInputWithoutGeometry();
  testFailedShaderMetadataDoesNotSatisfyRenderPathGraphDependency();
  testSourceResolvedShaderWithoutPayloadDoesNotSatisfyGraphDependency();
  testUploadViewRejectsSourceResolvedShaderWithoutPayload();
  testUploadViewRejectsReleasedRenderPathGraphFeatureDependency();
  testUploadViewRejectsReleasedRenderPathGraphShaderDependency();
  testExportRejectsReleasedRenderPathGraphDependencies();
  testExportRejectsFailedRenderPathGraphShaderDependency();
  if (g_failures != 0) {
    std::cerr << g_failures << " resource upload view v2 checks failed\n";
    return 1;
  }
  return 0;
}

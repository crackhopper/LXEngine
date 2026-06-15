#ifndef LX_SCENE_SYSTEM_ABI_GLSL
#define LX_SCENE_SYSTEM_ABI_GLSL

#define LX_SCENE_SYSTEM_DESCRIPTOR_SET 0
#define LX_SCENE_SYSTEM_CAMERA_BINDING 0
#define LX_SCENE_SYSTEM_LIGHT_BINDING 1
#define LX_SCENE_SYSTEM_OBJECT_BINDING 2
#define LX_SCENE_SYSTEM_MATERIAL_BINDING 3

layout(std430, set = LX_SCENE_SYSTEM_DESCRIPTOR_SET, binding = LX_SCENE_SYSTEM_CAMERA_BINDING) readonly buffer SceneCameraData {
  vec4 view;
  vec4 projection;
  vec4 eye;
};

layout(std430, set = LX_SCENE_SYSTEM_DESCRIPTOR_SET, binding = LX_SCENE_SYSTEM_LIGHT_BINDING) readonly buffer SceneLightData {
  vec4 directionIntensity;
  vec4 colorEnvironment;
};

layout(std430, set = LX_SCENE_SYSTEM_DESCRIPTOR_SET, binding = LX_SCENE_SYSTEM_OBJECT_BINDING) readonly buffer SceneObjectData {
  vec4 objectToWorld0;
  vec4 objectToWorld1;
  vec4 objectToWorld2;
  vec4 objectToWorld3;
  vec4 worldToObject0;
  vec4 worldToObject1;
  vec4 worldToObject2;
  vec4 worldToObject3;
};

layout(std430, set = LX_SCENE_SYSTEM_DESCRIPTOR_SET, binding = LX_SCENE_SYSTEM_MATERIAL_BINDING) readonly buffer SceneMaterialInstanceData {
  vec4 baseColor;
  vec4 bsdfParams0;
  vec4 bsdfParams1;
  vec4 textureIndices;
};

#endif

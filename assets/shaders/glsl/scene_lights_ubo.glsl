#ifndef LX_SCENE_LIGHTS_UBO_GLSL
#define LX_SCENE_LIGHTS_UBO_GLSL

const int MaxDirectionalLights = 4;
const int MaxPointLights = 16;
const int MaxSpotLights = 8;

struct SceneDirectionalLight {
    vec4 direction;
    vec4 colorIntensity;
};

struct ScenePointLight {
    vec4 positionRange;
    vec4 colorIntensity;
};

struct SceneSpotLight {
    vec4 positionRange;
    vec4 directionCone;
    vec4 colorIntensity;
};

layout(set = 0, binding = 1) uniform SceneLightsUBO {
    ivec4 counts;
    SceneDirectionalLight directional[MaxDirectionalLights];
    ScenePointLight point[MaxPointLights];
    SceneSpotLight spot[MaxSpotLights];
} sceneLights;

#endif

#version 450

layout(location = 0) out vec4 outColor;

layout(set = 4, binding = 0) uniform ToneMappingUBO {
    float exposure;
    int mode;
} toneMapping;

void main() {
    outColor = vec4(vec3(max(toneMapping.exposure, 0.0)) +
                        vec3(float(toneMapping.mode) * 0.0),
                    1.0);
}

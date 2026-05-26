#version 450

layout(location = 0) out vec3 vDirection;

layout(set = 0, binding = 1) uniform CaptureViewUBO {
    mat4 viewProj;
} captureView;

const vec3 kCubeVertices[3] = vec3[](
    vec3(-1.0, -1.0, 1.0),
    vec3(3.0, -1.0, 1.0),
    vec3(-1.0, 3.0, 1.0)
);

void main() {
    vec3 pos = kCubeVertices[gl_VertexIndex];
    vDirection = pos;
    gl_Position = captureView.viewProj * vec4(pos, 1.0);
}

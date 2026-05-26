#version 450

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vDir;

void main() {
    vDir = inPos;
    gl_Position = vec4(inPos.xy, 0.0, 1.0);
}

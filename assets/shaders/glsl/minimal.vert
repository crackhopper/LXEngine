#version 450

// REQ-DIAG-MIN: minimal vertex shader for the resize-flicker isolation demo.
// No descriptor sets, no UBO, no push constants. Vertices are pre-projected
// in NDC so any visual artifact is a pure swapchain / depth-attachment / cmd
// buffer issue, never a uniform-upload or pipeline-resource issue.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPos, 1.0);
    outColor = inColor;
}

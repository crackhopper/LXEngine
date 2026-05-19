#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform ShadertoyUBO {
    float time;
    vec4 resolution;
    vec4 audioBands;
} shadertoy;
layout(set = 1, binding = 1) uniform sampler2D iChannel0;

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float mapCore(vec3 p) {
    float sampledBass = texture(iChannel0, vec2(0.05, 0.25)).r;
    float bass = max(clamp(shadertoy.audioBands.x, 0.0, 1.0), sampledBass);
    vec3 q = p;
    q.xy *= rot(shadertoy.time * 0.5 + bass);
    q.yz *= rot(shadertoy.time * 0.3);

    for (int i = 0; i < 3; ++i) {
        q = abs(q) - 0.2 - bass * 0.1;
        q.xy *= rot(0.5);
        q.yz *= rot(0.8);
    }

    return sdBox(q, vec3(0.1, 0.5, 0.1));
}

void main() {
    vec2 uv = (vUV - vec2(0.5)) *
              vec2(shadertoy.resolution.x / max(shadertoy.resolution.y, 1.0), 1.0);
    float sampledBass = texture(iChannel0, vec2(0.05, 0.25)).r;
    float sampledMid = texture(iChannel0, vec2(0.4, 0.25)).r;
    float bass = max(clamp(shadertoy.audioBands.x, 0.0, 1.0), sampledBass);
    float mid = max(clamp(shadertoy.audioBands.y, 0.0, 1.0), sampledMid);

    vec3 ro = vec3(0.0, 0.0, -3.0);
    vec3 rd = normalize(vec3(uv, 1.5));
    ro.xz *= rot(shadertoy.time * 0.2);
    rd.xz *= rot(shadertoy.time * 0.2);

    vec3 col = vec3(0.01, 0.01, 0.02);
    float t = 0.0;
    for (int i = 0; i < 100; ++i) {
        vec3 p = ro + rd * t;
        float d = mapCore(p);
        if (d < 0.001 || t > 10.0) {
            break;
        }
        t += d;
    }

    if (t < 10.0) {
        vec3 p = ro + rd * t;
        vec2 e = vec2(0.001, 0.0);
        vec3 n = normalize(mapCore(p) - vec3(mapCore(p - e.xyy),
                                             mapCore(p - e.yxy),
                                             mapCore(p - e.yyx)));

        vec3 lightPos = vec3(2.0, 2.0, -2.0);
        vec3 lDir = normalize(lightPos - p);
        float diff = max(dot(n, lDir), 0.0);
        float spec = pow(max(dot(reflect(-lDir, n), -rd), 0.0), 64.0);

        vec3 baseCol = mix(vec3(0.0, 0.8, 1.0), vec3(1.0, 0.0, 0.5),
                           sin(p.y * 2.0 + shadertoy.time + mid) * 0.5 + 0.5);
        col = baseCol * diff + spec * vec3(1.0);

        float edge = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col += baseCol * edge * 2.0;
    }

    float beam = pow(max(0.0, 1.0 - length(uv * vec2(1.0, 2.0))), 4.0);
    col += vec3(0.1, 0.3, 0.5) * beam * bass;
    col = smoothstep(0.0, 1.0, col);
    col = pow(col, vec3(0.4545));

    outColor = vec4(col, 1.0);
}

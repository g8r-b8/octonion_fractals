// Gyroid triply periodic minimal surface: g(p) = sin(x)cos(y)+sin(y)cos(z)+sin(z)cos(x).
// params.z = frequency scale (multiplies p), params.w = thickness threshold (default 0.2).
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    float freq = pc.params.z;
    if (freq <= 0.0) freq = 1.0;
    float T = pc.params.w;
    vec3 q = p * freq;
    float g = sin(q.x) * cos(q.y) + sin(q.y) * cos(q.z) + sin(q.z) * cos(q.x);
    trap = abs(g);
    return (abs(g) - T) / 1.5;
}

// Mandelbulb Julia: White/Nylander iteration with fixed c = pc.c_lo.xyz.
// params.z = power (default 8.0), params.w = bailout squared.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    vec3 w = p;
    vec3 c = pc.c_lo.xyz;
    float m = dot(w, w);
    float dz = 1.0;
    trap = 1e10;
    float power = pc.params.z;
    float bail = pc.params.w;
    for (int i = 0; i < 12; ++i) {
        dz = power * pow(m, (power - 1.0) * 0.5) * dz + 1.0;
        float r = sqrt(m);
        float b = power * acos(clamp(w.y / r, -1.0, 1.0));
        float a = power * atan(w.x, w.z);
        w = c + pow(r, power) * vec3(sin(b) * sin(a), cos(b), sin(b) * cos(a));
        trap = min(trap, dot(w, w));
        m = dot(w, w);
        if (m > bail) break;
    }
    return 0.25 * log(m) * sqrt(m) / dz;
}

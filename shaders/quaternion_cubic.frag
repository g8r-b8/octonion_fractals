// Quaternion cubic Julia: z -> z^3 + c in H.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

vec4 q_mul(vec4 a, vec4 b) {
    return vec4(
        a.x*b.x - a.y*b.y - a.z*b.z - a.w*b.w,
        a.x*b.y + a.y*b.x + a.z*b.w - a.w*b.z,
        a.x*b.z - a.y*b.w + a.z*b.x + a.w*b.y,
        a.x*b.w + a.y*b.z - a.z*b.y + a.w*b.x);
}

float scene_de(vec3 p, out float trap) {
    vec4 z = vec4(p, 0.0);
    vec4 c = pc.c_lo;
    float dz = 1.0;
    float r2 = 0.0;
    trap = 1e10;
    int N = int(pc.params.z);
    float bail = pc.params.w;
    for (int i = 0; i < N; ++i) {
        r2 = dot(z, z);
        if (r2 > bail) break;
        dz = 3.0 * r2 * dz;
        z = q_mul(q_mul(z, z), z) + c;
        trap = min(trap, dot(z, z));
    }
    float r = sqrt(max(r2, 1e-20));
    return (1.0 / 6.0) * log(r) * r / dz;
}

// Two coupled octonion iterations sharing the spatial point.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

vec4 q_conj(vec4 q) { return vec4(q.x, -q.y, -q.z, -q.w); }
vec4 q_mul(vec4 a, vec4 b) {
    return vec4(
        a.x*b.x - a.y*b.y - a.z*b.z - a.w*b.w,
        a.x*b.y + a.y*b.x + a.z*b.w - a.w*b.z,
        a.x*b.z - a.y*b.w + a.z*b.x + a.w*b.y,
        a.x*b.w + a.y*b.z - a.z*b.y + a.w*b.x);
}

float scene_de(vec3 p, out float trap) {
    vec4 z1a = vec4(p, 0.0);
    vec4 z1b = vec4(0.0);
    vec4 z2a = vec4(p.zxy * 0.7, 0.0);
    vec4 z2b = vec4(0.0);
    vec4 ca = pc.c_lo;
    vec4 cb = pc.c_hi;
    vec4 ca2 = ca.zwxy;
    vec4 cb2 = cb.zwxy;
    float dz1 = 1.0;
    float dz2 = 1.0;
    float r2 = 0.0;
    trap = 1e10;
    int N = int(pc.params.z);
    float bail = pc.params.w;
    for (int i = 0; i < N; ++i) {
        float r1sq = dot(z1a, z1a) + dot(z1b, z1b);
        float r2sq = dot(z2a, z2a) + dot(z2b, z2b);
        r2 = max(r1sq, r2sq);
        if (r2 > bail) break;
        dz1 = 4.0 * r1sq * dz1;
        dz2 = 4.0 * r2sq * dz2;
        vec4 n1a = q_mul(z1a, z1a) - q_mul(q_conj(z1b), z1b);
        vec4 n1b = q_mul(z1b, z1a) + q_mul(z1b, q_conj(z1a));
        vec4 n2a = q_mul(z2a, z2a) - q_mul(q_conj(z2b), z2b);
        vec4 n2b = q_mul(z2b, z2a) + q_mul(z2b, q_conj(z2a));
        vec4 newZ1a = n1a + ca;
        vec4 newZ1b = n1b + cb;
        z2a = n2a + ca2 + newZ1a * 0.1;
        z2b = n2b + cb2 + newZ1b * 0.1;
        z1a = newZ1a;
        z1b = newZ1b;
        trap = min(trap, max(dot(z1a, z1a) + dot(z1b, z1b),
                             dot(z2a, z2a) + dot(z2b, z2b)));
    }
    float r = sqrt(max(r2, 1e-20));
    float dr = sqrt(max(max(dz1, dz2), 1e-20));
    return 0.5 * log(r) * r / dr;
}

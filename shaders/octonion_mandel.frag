// Octonion Mandelbrot: iterate z -> z^2 + c starting from z = 0, where c
// is the 3D point embedded into O via (e0, e1, e2). This visualises the
// parameter-space basin of the z^2 + c family in O (a 3D slice of an 8D set).
// params.z = max iter, params.w = bailout squared.
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
    // c is the point in space; z starts at 0.
    vec4 ca = vec4(p, 0.0);
    vec4 cb = vec4(0.0);
    // Augment c with the host-provided perturbations so the user can wiggle
    // the parameter-space slice via push constants.
    ca += pc.c_lo;
    cb += pc.c_hi;
    cb.w = 0.0;  // c_hi.w is palette phase, not a c-component

    vec4 za = vec4(0.0);
    vec4 zb = vec4(0.0);
    float dz2 = 1.0;
    float r2 = 0.0;
    trap = 1e10;
    int N = int(pc.params.z);
    float bail = pc.params.w;
    for (int i = 0; i < N; ++i) {
        r2 = dot(za, za) + dot(zb, zb);
        if (r2 > bail) break;
        dz2 = 4.0 * r2 * dz2;
        vec4 na = q_mul(za, za) - q_mul(q_conj(zb), zb);
        vec4 nb = q_mul(zb, za) + q_mul(zb, q_conj(za));
        za = na + ca;
        zb = nb + cb;
        trap = min(trap, dot(za, za) + dot(zb, zb));
    }
    float r = sqrt(max(r2, 1e-20));
    float dr = sqrt(max(dz2, 1e-20));
    return 0.5 * log(r) * r / dr;
}

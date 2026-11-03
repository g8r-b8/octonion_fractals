// Hybrid: alternates octonion Julia z^2+c steps with mandelbox-style folds on z.a.xyz.
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
    vec4 za = vec4(p, 0.0);
    vec4 zb = vec4(0.0);
    vec4 ca = pc.c_lo;
    vec4 cb = pc.c_hi;
    float dz2 = 1.0;
    float dr = 1.0;
    float r2 = 0.0;
    const float SCALE = -2.0;
    const float FOLD = 1.0;
    const float MR2 = 0.25;
    trap = 1e10;
    int N = int(pc.params.z);
    for (int i = 0; i < N; ++i) {
        r2 = dot(za, za) + dot(zb, zb);
        if (r2 > 256.0) break;
        if ((i & 1) == 0) {
            dz2 = 4.0 * r2 * dz2;
            vec4 na = q_mul(za, za) - q_mul(q_conj(zb), zb);
            vec4 nb = q_mul(zb, za) + q_mul(zb, q_conj(za));
            za = na + ca;
            zb = nb + cb;
        } else {
            vec3 zf = za.xyz;
            zf = clamp(zf, -FOLD, FOLD) * 2.0 - zf;
            float rf2 = dot(zf, zf);
            if (rf2 < MR2)      { zf *= (1.0 / MR2); }
            else if (rf2 < 1.0) { zf *= (1.0 / rf2); }
            zf = zf * SCALE;
            za.xyz = zf;
            dr = dr * (abs(SCALE) + 1.0);
        }
        trap = min(trap, dot(za, za) + dot(zb, zb));
    }
    float r = sqrt(max(r2, 1e-20));
    float drTotal = sqrt(max(dz2, 1e-20)) * dr;
    return 0.5 * log(max(r, 1.0)) * r / drTotal;
}

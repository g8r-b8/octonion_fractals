// Octonion tricorn cubic Julia: z -> conj(z)^3 + c in O.
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

struct Oct { vec4 a; vec4 b; };

Oct o_mul(Oct x, Oct y) {
    Oct r;
    r.a = q_mul(x.a, y.a) - q_mul(q_conj(y.b), x.b);
    r.b = q_mul(y.b, x.a) + q_mul(x.b, q_conj(y.a));
    return r;
}

Oct o_conj(Oct z) {
    Oct r;
    r.a = q_conj(z.a);
    r.b = -z.b;
    return r;
}

float scene_de(vec3 p, out float trap) {
    Oct z;
    z.a = vec4(p, 0.0);
    z.b = vec4(0.0);
    Oct c;
    c.a = pc.c_lo;
    c.b = pc.c_hi;
    c.b.w = 0.0;

    float dz2 = 1.0;
    float r2 = 0.0;
    trap = 1e10;
    int N = int(pc.params.z);
    float bail = pc.params.w;
    for (int i = 0; i < N; ++i) {
        r2 = dot(z.a, z.a) + dot(z.b, z.b);
        if (r2 > bail) break;
        dz2 = 9.0 * r2 * r2 * dz2;
        Oct zc = o_conj(z);
        Oct zc2 = o_mul(zc, zc);
        Oct zc3 = o_mul(zc2, zc);
        z.a = zc3.a + c.a;
        z.b = zc3.b + c.b;
        trap = min(trap, dot(z.a, z.a) + dot(z.b, z.b));
    }
    float r = sqrt(max(r2, 1e-20));
    float dr = sqrt(max(dz2, 1e-20));
    return (1.0 / 6.0) * log(r) * r / dr;
}

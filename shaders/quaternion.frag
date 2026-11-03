// Quaternion Julia: z -> z^2 + c in H. Hamilton product.
// params.z = max iter, params.w = bailout squared.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

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
        dz = 2.0 * sqrt(r2) * dz;
        // quaternion square: (a+v)^2 = (a^2 - |v|^2, 2 a v)
        z = vec4(z.x*z.x - dot(z.yzw, z.yzw), 2.0 * z.x * z.yzw) + c;
        trap = min(trap, dot(z, z));
    }
    float r = sqrt(max(r2, 1e-20));
    return 0.5 * log(r) * r / dz;
}

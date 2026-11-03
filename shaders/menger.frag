// Menger sponge by iterative cross-cuts.
// params.z = iterations (3..6), params.w unused.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float sdBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0));
}

float scene_de(vec3 p, out float trap) {
    float d = sdBox(p, vec3(1.0));
    trap = d;
    float s = 1.0;
    int N = int(pc.params.z);
    for (int i = 0; i < N; ++i) {
        vec3 a = mod(p * s, 2.0) - 1.0;
        s *= 3.0;
        vec3 r = abs(1.0 - 3.0 * abs(a));
        float da = max(r.x, r.y);
        float db = max(r.y, r.z);
        float dc = max(r.z, r.x);
        float c = (min(da, min(db, dc)) - 1.0) / s;
        d = max(d, c);
        trap = min(trap, d * d);
    }
    return d;
}

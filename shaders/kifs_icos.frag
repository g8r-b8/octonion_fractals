// Kaleidoscopic IFS with icosahedral symmetry: reflect across 3 planes, scale toward half-cube vertex.
// params.z = iterations (default 10), params.w unused.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    const float PHI = (1.0 + sqrt(5.0)) * 0.5;
    vec3 n1 = normalize(vec3(1.0, PHI, 0.0));
    vec3 n2 = normalize(vec3(0.0, 1.0, PHI));
    vec3 n3 = normalize(vec3(PHI, 0.0, 1.0));
    const float SCALE = 2.0;
    const vec3 VERT = vec3(0.5, 0.5, 0.5);
    int N = int(pc.params.z);

    trap = 1e10;
    for (int i = 0; i < N; ++i) {
        float d1 = dot(p, n1); if (d1 < 0.0) p -= 2.0 * d1 * n1;
        float d2 = dot(p, n2); if (d2 < 0.0) p -= 2.0 * d2 * n2;
        float d3 = dot(p, n3); if (d3 < 0.0) p -= 2.0 * d3 * n3;
        p = p * SCALE - VERT * (SCALE - 1.0);
        trap = min(trap, dot(p, p));
    }
    return length(p) * pow(SCALE, -float(N));
}

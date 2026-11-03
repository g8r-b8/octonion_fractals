// Apollonian gasket via iterative sphere inversion in a periodic unit cell.
// params.z = iterations (default 10), params.w = scale parameter (default 1.0).
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    int N = int(pc.params.z);
    float SCALE = pc.params.w;
    float scale = 1.0;
    trap = 1e10;
    for (int i = 0; i < N; ++i) {
        p = mod(p + 1.0, 2.0) - 1.0;
        float r2 = dot(p, p);
        float k = max(SCALE / r2, 1.0);
        p *= k;
        scale *= k;
        trap = min(trap, dot(p, p));
    }
    return 0.25 * abs(p.y) / scale;
}

// Sierpinski tetrahedron via folding IFS.
// params.z = iterations, params.w = rotation angle (radians).
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    const float SCALE = 2.0;
    int N = int(pc.params.z);
    float ang = pc.params.w;
    float ca = cos(ang), sa = sin(ang);
    mat3 R = mat3(ca, 0.0, sa,  0.0, 1.0, 0.0,  -sa, 0.0, ca);

    trap = 1e10;
    for (int i = 0; i < N; ++i) {
        p = R * p;
        // Reflect into the canonical tetrahedral fundamental domain.
        if (p.x + p.y < 0.0) p.xy = -p.yx;
        if (p.x + p.z < 0.0) p.xz = -p.zx;
        if (p.y + p.z < 0.0) p.yz = -p.zy;
        p = p * SCALE - vec3(SCALE - 1.0);
        trap = min(trap, dot(p, p));
    }
    return length(p) * pow(SCALE, -float(N));
}

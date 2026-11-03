// Mandelbox with aggressive scale = -3: box-fold + sphere-fold + scale.
// params.z = scale (default -3.0), params.w = fold limit (1.0).
#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

float scene_de(vec3 p, out float trap) {
    vec3 z = p;
    vec3 c = p;
    float dr = 1.0;
    float SCALE = pc.params.z;
    float FOLD  = pc.params.w;
    const float MR2 = 0.25;
    trap = 1e10;
    for (int i = 0; i < 14; ++i) {
        z = clamp(z, -FOLD, FOLD) * 2.0 - z;
        float r2 = dot(z, z);
        if (r2 < MR2)        { z *= (1.0 / MR2); dr *= (1.0 / MR2); }
        else if (r2 < 1.0)   { z *= (1.0 / r2);  dr *= (1.0 / r2);  }
        z = z * SCALE + c;
        dr = dr * abs(SCALE) + 1.0;
        trap = min(trap, dot(z, z));
    }
    return length(z) / abs(dr);
}

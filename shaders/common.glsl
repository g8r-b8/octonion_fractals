// Shared frag-shader scaffolding. Each fractal-specific shader does:
//   #version 450
//   #extension GL_GOOGLE_include_directive : require
//   #include "common.glsl"
//   float scene_de(vec3 p, out float trap) { ... }
//
// `common.glsl` provides the layout decls, push constants, ray march,
// normal estimator, palette, and main(). Push-constant fields have
// per-fractal meaning (see params.z / params.w docs in each shader).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    vec4 cam_pos;      // xyz pos, w = fov scale (tan(fov/2))
    vec4 cam_right;
    vec4 cam_up;
    vec4 cam_fwd;
    vec4 c_lo;         // fractal-specific (e.g. quaternion/octonion c)
    vec4 c_hi;
    vec4 params;       // x = time, y = aspect, z/w = fractal-specific
    vec4 hud;          // x = fractal index (float), y = show_help, z = paused, w = unused
} pc;

#include "font_data.glsl"

// Returns 1.0 inside a glyph pixel, 0.0 outside.
// `cellSize` is in screen-uv units (the glyph occupies a 3x5 grid within it).
float glyphPixel(int glyphId, vec2 uvCell) {
    // uvCell expected in [0,1] x [0,1]; glyph is 3x5 within that.
    if (glyphId < 0) return 0.0;
    int cx = int(floor(uvCell.x * 3.0));
    int cy = int(floor(uvCell.y * 5.0));
    if (cx < 0 || cx > 2 || cy < 0 || cy > 4) return 0.0;
    uint row = (kFont[glyphId] >> (cy * 3)) & 7u;
    // Leftmost pixel = bit 2 (per gen_font.py).
    return float((row >> uint(2 - cx)) & 1u);
}

// Draw glyph row of `len` glyphs starting at top-left `origin` in screen-uv
// (with origin.x growing to the right and origin.y growing downward).
// Each glyph cell is `cellSize` wide; glyph itself fills the cell with a 1-pixel
// gutter built in via the cellSize choice. Returns coverage 0..1 at uv.
float textCoverage(vec2 uv, vec2 origin, vec2 cellSize, int strIndex) {
    vec2 local = uv - origin;
    if (local.x < 0.0 || local.y < 0.0) return 0.0;
    if (local.y >= cellSize.y) return 0.0;
    int slot = int(floor(local.x / cellSize.x));
    int len = kStringLen[strIndex];
    if (slot < 0 || slot >= len) return 0.0;
    int gid = kStrings[strIndex * STR_MAXLEN + slot];
    if (gid < 0) return 0.0;
    vec2 cellUv = vec2(
        fract(local.x / cellSize.x),
        local.y / cellSize.y
    );
    // Trim horizontal gutter: glyphs are 3 columns wide; we want a tight 4-col cell
    // with the rightmost column being the gutter. Map [0, 0.75] of cell to glyph.
    if (cellUv.x > 0.75) return 0.0;
    cellUv.x /= 0.75;
    return glyphPixel(gid, cellUv);
}

// Each shader implements this.
float scene_de(vec3 p, out float trap);

struct Hit { float t; int steps; float trap; };

Hit march(vec3 ro, vec3 rd) {
    float t = 0.0;
    float trap = 1e10;
    int steps = 0;
    const int MAX_STEPS = 220;
    const float MAX_DIST = 40.0;
    const float HIT_EPS_BASE = 4e-4;
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 p = ro + rd * t;
        float ot;
        float de = scene_de(p, ot);
        trap = min(trap, ot);
        float eps = HIT_EPS_BASE * max(t, 1.0);
        if (de < eps) { steps = i; break; }
        if (t > MAX_DIST) { steps = i; t = -1.0; break; }
        t += de;
        steps = i;
    }
    return Hit(t, steps, trap);
}

vec3 estimate_normal(vec3 p) {
    float h = 1e-3;
    vec2 k = vec2(1.0, -1.0);
    float ot;
    return normalize(
        k.xyy * scene_de(p + k.xyy * h, ot) +
        k.yyx * scene_de(p + k.yyx * h, ot) +
        k.yxy * scene_de(p + k.yxy * h, ot) +
        k.xxx * scene_de(p + k.xxx * h, ot)
    );
}

// Inigo Quilez cosine palette. Phase shift via c_hi.w lets each shader pick
// a colour scheme without recompiling.
vec3 palette(float t) {
    vec3 a = vec3(0.5);
    vec3 b = vec3(0.5);
    vec3 d_ = vec3(0.0, 0.33, 0.67) + vec3(pc.c_hi.w);
    vec3 e_ = vec3(0.2, 0.6, 0.9);
    return a + b * cos(6.28318 * (e_ * t + d_));
}

void main() {
    vec2 uv = v_uv * 2.0 - 1.0;
    uv.x *= pc.params.y;

    vec3 ro = pc.cam_pos.xyz;
    float fov = pc.cam_pos.w;
    vec3 rd = normalize(
        pc.cam_fwd.xyz +
        pc.cam_right.xyz * uv.x * fov +
        pc.cam_up.xyz    * uv.y * fov);

    Hit h = march(ro, rd);

    vec3 col;
    if (h.t > 0.0) {
        vec3 p = ro + rd * h.t;
        vec3 n = estimate_normal(p);

        vec3 light = normalize(vec3(0.6, 0.8, 0.4));
        float diff = clamp(dot(n, light), 0.0, 1.0);
        float ao = clamp(1.0 - float(h.steps) / 220.0, 0.0, 1.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);

        float trap_mix = clamp(h.trap, 0.0, 1.0);
        float palette_t = 0.10 + 0.55 * ao + 0.25 * trap_mix + 0.04 * sin(pc.params.x * 0.3);
        vec3 base = palette(palette_t);

        col = base * (0.15 + 0.85 * diff) * ao + rim * vec3(0.9, 0.6, 0.3) * 0.4;

        float fog = 1.0 - exp(-0.07 * h.t);
        col = mix(col, vec3(0.02, 0.02, 0.04), fog);
    } else {
        float v = uv.y * 0.5 + 0.5;
        col = mix(vec3(0.015, 0.02, 0.04), vec3(0.05, 0.06, 0.09), v);
        col *= 1.0 - 0.3 * length(uv) * 0.3;
    }

    col = col / (col + 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    // ---- HUD overlay (in screen-uv space; v_uv is [0,1] with y up) ----
    // Flip y so positive y = down (matches text-layout convention).
    vec2 suv = vec2(v_uv.x, 1.0 - v_uv.y);

    // Title: current fractal name, top-left.
    int fractalIdx = int(pc.hud.x + 0.5);
    int helpOn = int(pc.hud.y + 0.5);
    int paused = int(pc.hud.z + 0.5);

    // Cell size proportional to width so text scales with window.
    float cellW = 0.012;
    vec2 cellTitle = vec2(cellW, cellW * (5.0 / 3.0) * pc.params.y);
    // Background pill behind title
    {
        vec2 origin = vec2(0.016, 0.022);
        int len = kStringLen[fractalIdx];
        float pillW = cellTitle.x * float(len + 1);
        float pillH = cellTitle.y * 1.6;
        if (suv.x >= origin.x - cellTitle.x*0.4 && suv.x < origin.x + pillW &&
            suv.y >= origin.y - cellTitle.y*0.3 && suv.y < origin.y + pillH)
        {
            col = mix(col, vec3(0.0), 0.55);
        }
        float cover = textCoverage(suv, origin, cellTitle, fractalIdx);
        col = mix(col, vec3(1.0, 0.92, 0.7), cover);
    }

    // Controls overlay. Help-on draws two lines (main + advanced); help-off
    // collapses to a single short hint.
    {
        float cw = 0.0078;
        vec2 cellCtrl = vec2(cw, cw * (5.0 / 3.0) * pc.params.y);
        float bottomMargin = cellCtrl.y * 0.6;
        if (helpOn == 1) {
            vec2 origin2 = vec2(0.016, 1.0 - cellCtrl.y * 1.4 - bottomMargin);
            vec2 origin1 = vec2(0.016, origin2.y - cellCtrl.y * 1.6);
            int len1 = kStringLen[STR_CONTROLS];
            int len2 = kStringLen[STR_ADVANCED];
            float pillW = cellCtrl.x * float(max(len1, len2) + 1);
            if (suv.x >= origin1.x - cellCtrl.x*0.4 && suv.x < origin1.x + pillW &&
                suv.y >= origin1.y - cellCtrl.y*0.3 && suv.y < 1.0)
            {
                col = mix(col, vec3(0.0), 0.55);
            }
            float c1 = textCoverage(suv, origin1, cellCtrl, STR_CONTROLS);
            float c2 = textCoverage(suv, origin2, cellCtrl, STR_ADVANCED);
            col = mix(col, vec3(0.85, 0.92, 1.0), max(c1, c2));
        } else {
            vec2 origin = vec2(0.016, 1.0 - cellCtrl.y * 1.6);
            int len = kStringLen[STR_HINT];
            float pillW = cellCtrl.x * float(len + 1);
            if (suv.x >= origin.x - cellCtrl.x*0.4 && suv.x < origin.x + pillW &&
                suv.y >= origin.y - cellCtrl.y*0.3 && suv.y < 1.0)
            {
                col = mix(col, vec3(0.0), 0.45);
            }
            float cover = textCoverage(suv, origin, cellCtrl, STR_HINT);
            col = mix(col, vec3(0.85, 0.92, 1.0), cover);
        }
    }

    // Paused indicator: small bar in top-right.
    if (paused == 1) {
        vec2 d = suv - vec2(0.97, 0.03);
        if (abs(d.x - 0.0) < 0.006 && abs(d.y) < 0.015) col = mix(col, vec3(1.0, 0.5, 0.3), 0.8);
        if (abs(d.x + 0.018) < 0.006 && abs(d.y) < 0.015) col = mix(col, vec3(1.0, 0.5, 0.3), 0.8);
    }

    outColor = vec4(col, 1.0);
}

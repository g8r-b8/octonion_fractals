#version 450

// Fullscreen triangle: 3 vertices cover the screen without a vertex buffer.
// Coordinates picked so the triangle's bounding triangle exactly contains [-1,1]^2.
vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = positions[gl_VertexIndex];
    // Vulkan NDC is y-down. Flip v_uv.y here so the fragment shader can treat
    // v_uv.y as growing upward (standard math convention) for ray dirs, and
    // (1 - v_uv.y) as screen-y growing downward for HUD layout.
    v_uv = vec2(0.5 * (p.x + 1.0), 0.5 * (1.0 - p.y));
    gl_Position = vec4(p, 0.0, 1.0);
}

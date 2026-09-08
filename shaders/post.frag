#version 450
// Upscale the low-res frame with grain, scanlines, vignette, and fade.
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0, std140) uniform Post {
    vec4 params; // x = time, y = grain, z = vignette, w = fade (0 black .. 1 visible)
    vec4 res;    // xy = internal resolution
};
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec3 c = texture(tex, v_uv).rgb;
    vec2 px = floor(v_uv * res.xy);
    c += (hash(px + params.x * 7.31) - 0.5) * params.y;
    c *= 1.0 - 0.06 * mod(px.y, 2.0);
    vec2 d = v_uv - 0.5;
    c *= 1.0 - params.z * dot(d, d) * 1.6;
    c *= params.w;
    o_color = vec4(c, 1.0);
}

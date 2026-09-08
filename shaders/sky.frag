#version 450
layout(set = 3, binding = 0, std140) uniform Sky {
    mat4 inv_view_proj;
    vec4 cam_pos;
    vec4 sun_dir;      // xyz
    vec4 sun_color;    // rgb, a = glow size
    vec4 zenith;       // rgb
    vec4 horizon;      // rgb
    vec4 ground;       // rgb
    vec4 params;       // x = time, y = star amount, z = fog blend, w = unused
    vec4 fog_color;
};
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o_color;
float hash(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
void main() {
    vec4 a = inv_view_proj * vec4(v_ndc.x, -v_ndc.y, 1.0, 1.0);
    vec4 b = inv_view_proj * vec4(v_ndc.x, -v_ndc.y, 0.0, 1.0);
    vec3 dir = normalize(a.xyz / a.w - b.xyz / b.w);
    float up = dir.y;
    vec3 c = up >= 0.0 ? mix(horizon.rgb, zenith.rgb, pow(clamp(up, 0.0, 1.0), 0.6))
                       : mix(horizon.rgb, ground.rgb, pow(clamp(-up, 0.0, 1.0), 0.5));
    // Stars: sparse hash on a coarse grid, twinkling
    if (params.y > 0.0 && up > 0.0) {
        vec3 g = floor(dir * 180.0);
        float h = hash(g);
        float star = step(1.0 - params.y * 0.01, h) * (0.5 + 0.5 * sin(params.x * 3.0 + h * 40.0));
        vec3 cell = fract(dir * 180.0) - 0.5;
        star *= smoothstep(0.35, 0.0, length(cell));
        c += vec3(star) * 2.5 * pow(clamp(up, 0.0, 1.0), 0.4);
    }
    // Sun / moon glow
    float s = clamp(dot(dir, -sun_dir.xyz), 0.0, 1.0);
    c += sun_color.rgb * (pow(s, 256.0) * 6.0 + pow(s, 8.0) * sun_color.a);
    // Horizon haze toward the fog colour
    c = mix(c, fog_color.rgb, params.z * pow(1.0 - clamp(abs(up), 0.0, 1.0), 4.0));
    o_color = vec4(c, 1.0);
}

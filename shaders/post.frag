#version 450
// Composite: HDR + bloom, colour grade, ACES tone map, vignette, grain, flash, fade.
layout(set = 2, binding = 0) uniform sampler2D hdr;
layout(set = 2, binding = 1) uniform sampler2D bloom;
layout(set = 3, binding = 0, std140) uniform Post {
    vec4 params;    // x = time, y = grain, z = vignette, w = fade
    vec4 res;       // xy = resolution
    vec4 flash;     // rgb, a = amount
    vec4 grade;     // x = exposure, y = saturation, z = contrast, w = bloom strength
    vec4 lift;      // rgb added to shadows
    vec4 gain;      // rgb multiplied into highlights
};
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    vec3 c = texture(hdr, v_uv).rgb + texture(bloom, v_uv).rgb * grade.w;
    c *= grade.x;
    // grade in linear-ish space: lift shadows, gain highlights
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(lum), c, grade.y);
    c = c * gain.rgb + lift.rgb * (1.0 - clamp(lum, 0.0, 1.0));
    c = (c - 0.5) * grade.z + 0.5;
    c = max(c, 0.0);
    c = aces(c);
    // vignette
    vec2 d = v_uv - 0.5;
    c *= 1.0 - params.z * dot(d, d) * 1.4;
    // grain
    c += (hash(floor(v_uv * res.xy) + params.x * 7.31) - 0.5) * params.y;
    c = mix(c, flash.rgb, flash.a);
    c *= params.w;
    o_color = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}

#version 450
// Composite: HDR + bloom, colour grade, ACES tone map, vignette, grain, flash, fade.
layout(set = 2, binding = 0) uniform sampler2D hdr;
layout(set = 2, binding = 1) uniform sampler2D bloom;
layout(set = 2, binding = 2) uniform sampler2D depth;    // scene depth, for outlines
layout(set = 3, binding = 0, std140) uniform Post {
    vec4 params;    // x = time, y = grain, z = vignette, w = fade
    vec4 res;       // xy = resolution
    vec4 flash;     // rgb, a = amount
    vec4 grade;     // x = exposure, y = saturation, z = contrast, w = bloom strength
    vec4 lift;      // rgb added to shadows
    vec4 gain;      // rgb multiplied into highlights
    vec4 style;     // x = palette snap 0..1, y = outline 0..1, z = colour levels (0 off), w = world pixel size (1 off)
    vec4 pal[64];   // the game's palette (assets/palette.txt)
    ivec4 npal;
};

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    // world pixelation: sample the scene on a coarser grid (characters already come pixelated)
    vec2 uv = v_uv;
    if (style.w > 1.5) { vec2 g = res.xy / style.w; uv = (floor(v_uv * g) + 0.5) / g; }
    vec3 c = texture(hdr, uv).rgb + texture(bloom, uv).rgb * grade.w;
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
    vec3 g = pow(max(c, 0.0), vec3(1.0 / 2.2));
    // style: crunch the world's colours to our palette / a few levels, and ink the depth edges
    if (style.z > 0.5) g = mix(g, floor(g * style.z + 0.5) / style.z, clamp(style.x, 0.0, 1.0));
    else if (style.x > 0.0) {
        float best = 1e9; vec3 pick = g;
        for (int i = 0; i < npal.x; i++) { vec3 d = pal[i].rgb - g; float e = dot(d, d); if (e < best) { best = e; pick = pal[i].rgb; } }
        g = mix(g, pick, style.x);
    }
    if (style.y > 0.0) {
        vec2 px = (style.w > 1.5 ? style.w : 1.0) / res.xy;
        float d0 = texture(depth, uv).r;
        float dx = max(abs(texture(depth, uv + vec2(px.x, 0)).r - d0), abs(texture(depth, uv - vec2(px.x, 0)).r - d0));
        float dy = max(abs(texture(depth, uv + vec2(0, px.y)).r - d0), abs(texture(depth, uv - vec2(0, px.y)).r - d0));
        float edge = smoothstep(0.0008, 0.004, max(dx, dy) * (1.0 - d0 * 0.9));   // nearer surfaces need a smaller step
        g *= 1.0 - edge * style.y * 0.85;
    }
    g *= params.w;
    o_color = vec4(g, 1.0);
}

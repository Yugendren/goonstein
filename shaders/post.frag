#version 450
// Composite: HDR + bloom, colour grade, ACES tone map, vignette, grain, flash, fade, and the
// style layers a level can switch on from its `look` lines:
//   camcorder  chroma bleed, ordered dither into a coarse colour crunch, vignette, a little grain
//   flat       thick depth-edge ink over toon-banded, flat-coloured materials (the `look flat` mix
//              itself happens in lit.frag; here it is only the ink)
//   sketch     depth + luminance edges that wobble on an 8 Hz step, screen-space hatching in the
//              shadows, and a paper texture multiplied over everything
// Every one of them is off at zero, which is the plain look.
layout(set = 2, binding = 0) uniform sampler2D hdr;
layout(set = 2, binding = 1) uniform sampler2D bloom;
layout(set = 2, binding = 2) uniform sampler2D depth;    // scene depth, for outlines
layout(set = 2, binding = 3) uniform sampler2D paper;    // repeating paper grain (assets/textures/paper.png)
layout(set = 3, binding = 0, std140) uniform Post {
    vec4 params;    // x = time, y = grain, z = vignette, w = fade
    vec4 res;       // xy = resolution
    vec4 flash;     // rgb, a = amount
    vec4 grade;     // x = exposure, y = saturation, z = contrast, w = bloom strength
    vec4 lift;      // rgb added to shadows
    vec4 gain;      // rgb multiplied into highlights
    vec4 style;     // x = palette snap 0..1, y = outline 0..1, z = colour levels (0 off), w = world pixel size (1 off)
    vec4 ink;       // x = ink width in pixels, y = edge wobble in pixels, z = luminance-edge weight, w = paper amount
    vec4 film;      // x = chroma bleed in pixels, y = ordered dither amount, z = hatching amount, w = paper tile size in pixels
    vec4 pal[64];   // the game's palette (assets/palette.txt)
    ivec4 npal;
};

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
// HDR luminance squashed into 0..1, so an edge test means the same thing in a dark room and in
// full sun: a raw difference in linear HDR is ten times bigger on a lit wall than in shadow.
float luma(vec3 c) { float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); return l / (1.0 + l); }

void main() {
    // world pixelation: sample the scene on a coarser grid (characters already come pixelated)
    vec2 uv = v_uv;
    if (style.w > 1.5) { vec2 g = res.xy / style.w; uv = (floor(v_uv * g) + 0.5) / g; }
    // Camcorder chroma bleed: red and blue drift apart, and further apart toward the corners,
    // which is what a cheap lens and a composite cable did between them.
    vec3 c;
    if (film.x > 0.0) {
        vec2 off = (v_uv - 0.5) * (film.x * 2.0) / res.xy;
        c = vec3(texture(hdr, uv + off).r, texture(hdr, uv).g, texture(hdr, uv - off).b);
    } else {
        c = texture(hdr, uv).rgb;
    }
    if (grade.w > 0.0) c += texture(bloom, uv).rgb * grade.w;   // zero also means the bloom passes were skipped
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

    vec2 sp = v_uv * res.xy;
    // Hatching: two crossed rulings that only exist where the frame already went dark, so shadow
    // reads as pencil rather than as black. The second, denser ruling comes in further down,
    // which is how a drawing gets darker -- more lines, not darker lines.
    if (film.z > 0.0) {
        float gl = dot(g, vec3(0.2126, 0.7152, 0.0722));
        float shade = 1.0 - smoothstep(0.12, 0.50, gl);
        float deep  = 1.0 - smoothstep(0.04, 0.26, gl);
        // ~16 px between rulings on the diagonal: any finer and it stops being pencil and becomes
        // a moire screen, which is exactly what a fly screen looks like when the camera moves.
        float h1 = sin((sp.x + sp.y) * 0.28);
        float h2 = sin((sp.x - sp.y) * 0.31 + 1.7);
        float lines = max(smoothstep(0.45, 0.92, h1), smoothstep(0.55, 0.95, h2) * deep);
        g *= 1.0 - lines * shade * film.z * 0.6;
    }
    // style: crunch the world's colours to our palette / a few levels, and ink the depth edges.
    // The ordered dither goes in first: without it a 32-level crunch bands a dusk sky in stripes.
    if (film.y > 0.0) {
        float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        ivec2 bp = ivec2(mod(sp, 4.0));
        g += ((bayer[bp.y * 4 + bp.x] + 0.5) / 16.0 - 0.5) * film.y;
    }
    if (style.z > 0.5) g = mix(g, floor(clamp(g, 0.0, 1.0) * style.z + 0.5) / style.z, clamp(style.x, 0.0, 1.0));
    else if (style.x > 0.0) {
        float best = 1e9; vec3 pick = g;
        for (int i = 0; i < npal.x; i++) { vec3 dd = pal[i].rgb - g; float e = dot(dd, dd); if (e < best) { best = e; pick = pal[i].rgb; } }
        g = mix(g, pick, style.x);
    }
    if (style.y > 0.0) {
        float w = max(ink.x, 1.0) * (style.w > 1.5 ? style.w : 1.0);
        vec2 px = w / res.xy;
        // Boil: the whole edge test is taken from a point that wanders, stepped at 8 Hz rather
        // than run per frame, so the lines redraw themselves at a drawing's pace instead of
        // shimmering at the monitor's. Two frequencies per axis stop it looking like a slide.
        vec2 juv = uv;
        if (ink.y > 0.0) {
            float t = floor(params.x * 8.0);
            vec2 wob = vec2(sin(sp.y * 0.070 + t * 1.7) + sin(sp.y * 0.021 - t * 2.3),
                            cos(sp.x * 0.063 - t * 1.3) + cos(sp.x * 0.017 + t * 3.1));
            juv += wob * 0.5 * ink.y / res.xy;
        }
        float d0 = texture(depth, juv).r;
        float dx = max(abs(texture(depth, juv + vec2(px.x, 0)).r - d0), abs(texture(depth, juv - vec2(px.x, 0)).r - d0));
        float dy = max(abs(texture(depth, juv + vec2(0, px.y)).r - d0), abs(texture(depth, juv - vec2(0, px.y)).r - d0));
        // A raw depth step means nothing on its own: the buffer is 1/z, so with a 600 m far plane
        // its values crush toward 1 and a silhouette 40 m away differs by a hundredth of what the
        // same silhouette differs by at 4 m. Dividing by (1 - d) undoes exactly that, leaving a
        // RELATIVE step in view depth -- dz/z -- so one threshold inks a whole island. Dividing by
        // the line width as well keeps a 3 px line from also being a three times more eager one.
        // The floor of 4e-4 is where this stops: with the fixed 0.1 m near plane it corresponds to
        // roughly 250 m out, past which the buffer has no precision left to argue with and the
        // sea's depth is within a rounding error of the sky's. Without it the horizon comes out as
        // a black bar across the whole frame.
        float rel = max(dx, dy) / (max(1.0 - d0, 4e-4) * w);
        float edge = smoothstep(0.07, 0.25, rel);
        // Creases the depth buffer cannot see -- a folded arm, a window set into a wall, the line
        // where two toon bands meet -- come from a step in luminance across the same four taps.
        if (ink.z > 0.0) {
            float l0 = luma(texture(hdr, juv).rgb);
            float lx = max(abs(luma(texture(hdr, juv + vec2(px.x, 0)).rgb) - l0), abs(luma(texture(hdr, juv - vec2(px.x, 0)).rgb) - l0));
            float ly = max(abs(luma(texture(hdr, juv + vec2(0, px.y)).rgb) - l0), abs(luma(texture(hdr, juv - vec2(0, px.y)).rgb) - l0));
            edge = max(edge, smoothstep(0.045, 0.30, max(lx, ly)) * ink.z);
        }
        g *= 1.0 - edge * style.y * 0.85;
    }
    // Paper: multiplied, so the whites of the frame become the sheet's tooth and the ink stays ink.
    if (ink.w > 0.0) {
        // The sheet itself is deliberately low contrast (tools/make_paper.py keeps it inside
        // 0.85..1.0 of white, because anything punchier multiplies into visible banding). Push its
        // tooth back out here, where it is the last thing that happens and cannot band anything.
        vec3 p = texture(paper, sp / max(film.w, 1.0)).rgb;
        p = clamp((p - 0.92) * 3.0 + 0.96, 0.0, 1.1);
        g = mix(g, g * p, ink.w);
    }
    g *= params.w;
    o_color = vec4(g, 1.0);
}

#version 450
// Pixel-art composite: characters were rendered into a small target (one texel = one art pixel).
// This pass paints that layer over the full-resolution world with nearest sampling, a sub-texel
// shift that undoes the camera snap (so the world stays smooth while the pixels stay put),
// crunches the colours to a few levels or to a fixed palette, and adds a one-pixel outline.
// Depth comes along, so walls still hide characters and particles still sort against them.
layout(set = 2, binding = 0) uniform sampler2D pix;
layout(set = 2, binding = 1) uniform sampler2D pixdepth;
layout(set = 3, binding = 0, std140) uniform Pix {
    vec4 res;      // xy = layer resolution
    vec4 offset;   // xy = sub-texel shift in uv
    vec4 params;   // x = colour levels (0 = off), y = outline (0..1), z = palette (0 = levels, 1 = the palette file), w = inner line strength
    vec4 pal[64];  // the game's palette (assets/palette.txt), rgb, w unused
    ivec4 npal;    // x = colours in use
};
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;


vec3 to_gamma(vec3 c) { return pow(max(c, 0.0), vec3(1.0 / 2.2)); }
vec3 to_linear(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }

// Crunch a lit colour to the art palette. The lighting brightness above 1 (bloom range) is kept
// as a multiplier so glowing tells still glow.
vec3 crunch(vec3 c) {
    float k = max(max(c.r, c.g), c.b);
    float over = k > 1.0 ? k : 1.0;
    vec3 g = to_gamma(c / over);
    if (params.z > 0.5) {
        float best = 1e9; vec3 pick = g;
        for (int i = 0; i < npal.x; i++) { vec3 d = pal[i].rgb - g; float e = dot(d, d); if (e < best) { best = e; pick = pal[i].rgb; } }
        g = pick;
    } else if (params.x > 0.5) {
        g = floor(g * params.x + 0.5) / params.x;
    }
    return to_linear(g) * over;
}

void main() {
    vec2 uv = v_uv + offset.xy;
    ivec2 sz = ivec2(res.xy);
    ivec2 p = ivec2(floor(uv * res.xy));
    ivec2 pc = clamp(p, ivec2(0), sz - 1);
    vec4 c = texelFetch(pix, pc, 0);
    float d = texelFetch(pixdepth, pc, 0).r;
    bool outside = p.x < 0 || p.y < 0 || p.x >= sz.x || p.y >= sz.y;
    if (outside) { c = vec4(0); d = 1.0; }
    bool full = c.a > 0.5;
    // neighbours (for the silhouette outline and inner creases)
    ivec2 n4[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
    float nd = 1.0; vec3 nc = vec3(0); bool near = false; float crease = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 q = clamp(p + n4[i], ivec2(0), sz - 1);
        vec4 qc = texelFetch(pix, q, 0); float qd = texelFetch(pixdepth, q, 0).r;
        if (qc.a > 0.5) { near = true; if (qd < nd) { nd = qd; nc = qc.rgb; } }
        if (full && qc.a > 0.5 && qd > d + 0.0015) crease = 1.0;   // this pixel sits in front of its neighbour
    }
    if (!full) {
        if (!near || params.y <= 0.001) discard;
        vec3 line = mix(crunch(nc) * 0.18, vec3(0.01, 0.01, 0.015), 0.5);
        o_color = vec4(mix(crunch(nc), line, params.y), 1.0);
        gl_FragDepth = nd;
        return;
    }
    vec3 col = crunch(c.rgb);
    col = mix(col, col * 0.45, crease * params.w);
    o_color = vec4(col, 1.0);
    gl_FragDepth = d;
}

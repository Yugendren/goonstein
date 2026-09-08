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
    vec4 params;   // x = colour levels (0 = off), y = outline (0..1), z = palette (0 = levels, 1 = Endesga 32), w = inner line strength
};
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

const vec3 PAL[32] = vec3[32](
    vec3(0.745, 0.294, 0.176), vec3(0.847, 0.459, 0.239), vec3(0.918, 0.616, 0.412), vec3(0.980, 0.882, 0.702),
    vec3(0.243, 0.216, 0.184), vec3(0.353, 0.290, 0.235), vec3(0.510, 0.408, 0.325), vec3(0.667, 0.545, 0.443),
    vec3(0.212, 0.239, 0.298), vec3(0.365, 0.451, 0.510), vec3(0.545, 0.635, 0.702), vec3(0.769, 0.847, 0.882),
    vec3(1.000, 1.000, 1.000), vec3(0.518, 0.541, 0.545), vec3(0.353, 0.361, 0.365), vec3(0.239, 0.239, 0.243),
    vec3(0.114, 0.118, 0.129), vec3(0.204, 0.098, 0.110), vec3(0.259, 0.173, 0.204), vec3(0.384, 0.263, 0.298),
    vec3(0.545, 0.376, 0.365), vec3(0.706, 0.478, 0.412), vec3(0.867, 0.627, 0.478), vec3(1.000, 0.855, 0.671),
    vec3(0.180, 0.365, 0.408), vec3(0.239, 0.510, 0.427), vec3(0.310, 0.667, 0.416), vec3(0.510, 0.808, 0.376),
    vec3(0.808, 0.925, 0.565), vec3(0.878, 0.776, 0.318), vec3(0.980, 0.635, 0.290), vec3(0.929, 0.353, 0.361));

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
        for (int i = 0; i < 32; i++) { vec3 d = PAL[i] - g; float e = dot(d, d); if (e < best) { best = e; pick = PAL[i]; } }
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

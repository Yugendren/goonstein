#version 450
// world.vert with the model matrix and the tint moved off the uniform block and onto a per-instance
// vertex stream, so a hundred palms -- thirty boxes and cylinders each, every frond a slightly
// different green -- are two draw calls instead of three thousand.
//
// The uniform block keeps view_proj (one per pass) and the flags; `model` and `tint` in it are
// ignored by this shader and left in place only so the C side can push the same VSUniforms struct
// to either pipeline. The per-instance tint multiplies the vertex colour, which is exactly where
// lit.frag already multiplies the material tint in (t = texture * v_color * tint), so an instanced
// prop and a singly drawn one come out the same colour.
layout(set = 1, binding = 0, std140) uniform VS { mat4 view_proj; mat4 model; vec4 uv_xform; vec4 flags; };   // flags.x = planar world mapping (tile = uv_xform.x), flags.y = the frame's time in seconds
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
// Per-instance stream (vertex buffer slot 1, input rate INSTANCE): the model matrix as four rows
// and the instance tint.
layout(location = 4) in vec4 i_m0;
layout(location = 5) in vec4 i_m1;
layout(location = 6) in vec4 i_m2;
layout(location = 7) in vec4 i_m3;
layout(location = 8) in vec4 i_tint;
layout(location = 0) out vec3 v_wpos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;

// ---------------------------------------------------------------- wind
// Foliage sway. The weight per vertex comes in the vertex colour's ALPHA, inverted: alpha 1 --
// which is what every mesh in this game has, and what glTF defines when a mesh has no COLOR_0 at
// all -- is rigid, and alpha 0 bends the most. The palm generator writes 0.30 at the top of a
// trunk and 1.0 at a frond's tip (tools/palm/make_palm.py).
//
// It rides in the vertex stream, and not in a material flag or a level keyword, because that is
// the only channel that reaches BOTH this shader and the instanced one without the draw path
// having to know which meshes are plants: the instancer batches by mesh and texture, and a
// per-batch uniform would have had to become part of that key. It also costs nothing: the alpha
// channel of the vertex colour was multiplied into the alpha test and was 1 everywhere, so
// lit.frag now takes opacity from the texture and the material tint alone.
//
// The motion is one travelling gust front with a faster ripple on it, keyed to world position so
// neighbouring plants are out of step, and to flags.y, which is the frame's time in seconds.
// Squaring the weight is the stiffness curve: the bend belongs to the tips.
vec3 wind_offset(vec3 wp, float weight, float t) {
    if (weight <= 0.002) return vec3(0.0);
    float phase = dot(wp.xz, vec2(0.115, 0.062)) - t * 1.15;
    float gust = 0.55 + 0.45 * sin(dot(wp.xz, vec2(0.0135, 0.0210)) - t * 0.21);
    float s = (sin(phase) + 0.34 * sin(phase * 2.37 + 1.7)) * gust;
    float k = weight * weight * 0.30;
    return vec3(0.87 * s * k, -abs(s) * k * 0.22, 0.49 * s * k);
}

void main() {
    mat4 im = mat4(i_m0, i_m1, i_m2, i_m3);   // column-major, same layout as Mat4's m[16]
    vec4 wp = im * vec4(a_pos, 1.0);
    wp.xyz += wind_offset(wp.xyz, 1.0 - a_color.w, flags.y);
    // mat3(im), not its inverse transpose, because that is what world.vert does. A squashed frond
    // therefore gets a slightly wrong normal -- and gets exactly the SAME slightly wrong normal it
    // had before this shader existed, which is the point: instancing is a draw-call change, not a
    // look change, and the bench screenshots have to come out pixel for pixel as they were.
    vec3 n = mat3(im) * a_normal;
    v_wpos = wp.xyz; v_normal = n; v_color = a_color * i_tint;
    if (flags.x > 0.5) {   // planar world mapping, tile = uv_xform.x
        vec3 an = abs(normalize(n)); float tile = uv_xform.x;
        if (an.x > an.y && an.x > an.z) v_uv = wp.zy * tile;
        else if (an.y > an.z)           v_uv = wp.xz * tile;
        else                            v_uv = wp.xy * tile;
    } else v_uv = a_uv * uv_xform.xy + uv_xform.zw;
    gl_Position = view_proj * wp;
}

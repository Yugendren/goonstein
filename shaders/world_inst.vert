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
layout(set = 1, binding = 0, std140) uniform VS { mat4 view_proj; mat4 model; vec4 uv_xform; vec4 flags; };   // flags.x = planar world mapping, tile = uv_xform.x
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
void main() {
    mat4 im = mat4(i_m0, i_m1, i_m2, i_m3);   // column-major, same layout as Mat4's m[16]
    vec4 wp = im * vec4(a_pos, 1.0);
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

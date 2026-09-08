#version 450
layout(set = 1, binding = 0, std140) uniform VS { mat4 view_proj; mat4 model; vec4 uv_xform; vec4 flags; };   // flags.x = planar world mapping, tile = uv_xform.x
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(location = 0) out vec3 v_wpos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;
void main() {
    vec4 wp = model * vec4(a_pos, 1.0);
    vec3 n = mat3(model) * a_normal;
    v_wpos = wp.xyz; v_normal = n; v_color = a_color;
    if (flags.x > 0.5) {   // planar world mapping, tile = uv_xform.x
        vec3 an = abs(normalize(n)); float tile = uv_xform.x;
        if (an.x > an.y && an.x > an.z) v_uv = wp.zy * tile;
        else if (an.y > an.z)           v_uv = wp.xz * tile;
        else                            v_uv = wp.xy * tile;
    } else v_uv = a_uv * uv_xform.xy + uv_xform.zw;
    gl_Position = view_proj * wp;
}

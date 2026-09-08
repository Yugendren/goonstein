#version 450
// World geometry: vertex-lit, per-vertex fog, PS2 style.
layout(set = 1, binding = 0, std140) uniform Frame {
    mat4 view_proj;
    mat4 model;
    vec4 tint;        // rgb multiply, a = alpha
    vec4 fog_params;  // x = near, y = far
    vec4 light_dir;   // xyz normalised (direction light travels), w = ambient
    vec4 light_color; // rgb
    vec4 uv_xform;    // xy = scale, zw = offset
};
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_fog;
void main() {
    vec4 wp = model * vec4(a_pos, 1.0);
    vec4 cp = view_proj * wp;
    vec3 n  = normalize(mat3(model) * a_normal);
    float nl = clamp(dot(n, -light_dir.xyz), 0.0, 1.0);
    vec3 lit = light_color.rgb * nl + light_dir.w;
    gl_Position = cp;
    if (uv_xform.x < 0.0) {
        // World-space planar mapping picked by the dominant normal axis: textures stay
        // continuous across blocks and never stretch, whatever the box proportions.
        vec3 an = abs(n);
        float tile = -uv_xform.x;
        if (an.x > an.y && an.x > an.z)      v_uv = wp.zy * tile;
        else if (an.y > an.z)                v_uv = wp.xz * tile;
        else                                 v_uv = wp.xy * tile;
    } else {
        v_uv = a_uv * uv_xform.xy + uv_xform.zw;
    }
    v_color = vec4(a_color.rgb * tint.rgb * lit, a_color.a * tint.a);
    v_fog = clamp((cp.w - fog_params.x) / (fog_params.y - fog_params.x), 0.0, 1.0);
}

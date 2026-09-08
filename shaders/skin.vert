#version 450
// Skinned geometry: same lighting/fog as world.vert, plus GPU joint skinning.
layout(set = 1, binding = 0, std140) uniform Frame {
    mat4 view_proj;
    mat4 model;
    vec4 tint;        // rgb multiply, a = alpha
    vec4 fog_params;  // x = near, y = far
    vec4 light_dir;   // xyz normalised (direction light travels), w = ambient
    vec4 light_color; // rgb
    vec4 uv_xform;    // xy = scale, zw = offset
};
layout(set = 1, binding = 1, std140) uniform Joints { mat4 joints[64]; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uvec4 a_joints;
layout(location = 4) in vec4 a_weights;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_fog;
void main() {
    // Touch the Frame block before Joints: glslc preserves this order in the SPIR-V,
    // and spirv-cross assigns MSL buffer indices by first-use order, so this keeps
    // Frame at buffer(0)/slot 0 and Joints at buffer(1)/slot 1 to match gfx_draw_skinned.
    mat4 mdl = model;
    mat4 skin = a_weights.x * joints[a_joints.x] + a_weights.y * joints[a_joints.y] +
                a_weights.z * joints[a_joints.z] + a_weights.w * joints[a_joints.w];
    vec4 wp = mdl * skin * vec4(a_pos, 1.0);
    vec4 cp = view_proj * wp;
    vec3 n  = normalize(mat3(mdl) * mat3(skin) * a_normal);
    float nl = clamp(dot(n, -light_dir.xyz), 0.0, 1.0);
    vec3 lit = light_color.rgb * nl + light_dir.w;
    gl_Position = cp;
    v_uv = a_uv * uv_xform.xy + uv_xform.zw;
    v_color = vec4(tint.rgb * lit, tint.a);
    v_fog = clamp((cp.w - fog_params.x) / (fog_params.y - fog_params.x), 0.0, 1.0);
}

#version 450
layout(set = 1, binding = 0, std140) uniform VS { mat4 view_proj; mat4 model; vec4 uv_xform; };
layout(set = 1, binding = 1, std140) uniform Joints { mat4 joints[64]; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uvec4 a_joints;
layout(location = 4) in vec4 a_weights;
layout(location = 0) out vec3 v_wpos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;
void main() {
    mat4 mdl = model;
    mat4 skin = a_weights.x * joints[a_joints.x] + a_weights.y * joints[a_joints.y]
              + a_weights.z * joints[a_joints.z] + a_weights.w * joints[a_joints.w];
    vec4 wp = mdl * skin * vec4(a_pos, 1.0);
    v_wpos = wp.xyz;
    v_normal = mat3(mdl) * mat3(skin) * a_normal;
    v_uv = a_uv; v_color = vec4(1.0);
    gl_Position = view_proj * wp;
}

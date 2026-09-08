#version 450
layout(set = 1, binding = 0, std140) uniform VS { mat4 view_proj; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() { v_uv = a_uv; v_color = a_color; gl_Position = view_proj * vec4(a_pos, 1.0); }

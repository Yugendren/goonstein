#version 450
layout(set = 1, binding = 0, std140) uniform UI { vec4 screen; };
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() {
    gl_Position = vec4(a_pos.x / screen.x * 2.0 - 1.0, 1.0 - a_pos.y / screen.y * 2.0, 0.0, 1.0);
    v_uv = a_uv; v_color = a_color;
}

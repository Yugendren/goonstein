#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;
void main() { vec4 t = texture(tex, v_uv); o_color = vec4(v_color.rgb * t.rgb, v_color.a * t.a); }

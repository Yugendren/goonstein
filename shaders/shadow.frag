#version 450
// Depth-only pass for the sun's shadow map: nothing to write but depth.
layout(location = 0) in vec3 v_wpos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_color;
void main() { }
